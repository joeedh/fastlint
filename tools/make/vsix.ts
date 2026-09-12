import fs from "node:fs";
import path from "node:path";
import type { CommandModule } from "yargs";
import { color, fail, info, step } from "./lib/log.ts";
import { buildRoot, readJson, repoRoot } from "./lib/paths.ts";
import { run } from "./lib/spawn.ts";
import { bundleWasm, wasmPresetToBundle } from "./pack.ts";

interface Args {
  wasm: boolean;
  smoke: boolean;
  install: boolean;
}

/** The extension's directory, a package of its own with its own lockfile. */
export const extensionDir = path.join(repoRoot, "editors", "vscode");

/** Where the packaged extension lands, beside the npm tarball's directory. */
const vsixDir = path.join(buildRoot, "vsix");

/** `.cmd` shims cannot be spawned without a shell on Windows (lib/spawn.ts). */
const shell = process.platform === "win32";

/** Installs the extension's dependencies. pnpm is fast when nothing changed, so
 * the step runs every time rather than guessing from `node_modules`. */
async function installDeps(): Promise<void> {
  step("pnpm install (editors/vscode)");
  await run("pnpm", ["install"], { cwd: extensionDir, shell });
}

/** Copies in what the VSIX ships from outside its directory: the WASM engine,
 * the config schema `jsonValidation` points at, and the license. */
function stage(preset: "wasm" | "wasm-release"): void {
  const wasmDir = path.join(extensionDir, "wasm");
  bundleWasm(preset, wasmDir);
  // The Emscripten module is ESM. The extension's own package.json declares no
  // type, so without this Node reparses the file and warns on every start.
  fs.writeFileSync(path.join(wasmDir, "package.json"), '{ "type": "module" }\n');
  const schema = "lintrix.config.schema.json";
  fs.mkdirSync(path.join(extensionDir, "schema"), { recursive: true });
  fs.copyFileSync(
    path.join(repoRoot, "schema", schema),
    path.join(extensionDir, "schema", schema)
  );
  fs.copyFileSync(path.join(repoRoot, "LICENSE"), path.join(extensionDir, "LICENSE"));
}

/** Bundles the client and the server into `out/`. */
async function bundle(): Promise<void> {
  step("esbuild");
  await run(process.execPath, ["esbuild.mts"], { cwd: extensionDir });
  for (const name of ["client.js", "server.js"]) {
    if (!fs.existsSync(path.join(extensionDir, "out", name))) {
      fail(`esbuild wrote no out/${name}`);
    }
  }
}

/** Packages `editors/vscode` with vsce. Dependencies are already inside the
 * bundles, so vsce is told not to walk `node_modules`, which it cannot read
 * under pnpm's layout anyway. */
async function packageVsix(): Promise<string> {
  step("vsce package");
  const manifest = readJson<{ name: string; version: string }>(
    path.join(extensionDir, "package.json")
  );
  if (!manifest) fail("editors/vscode/package.json did not parse");
  fs.mkdirSync(vsixDir, { recursive: true });
  const out = path.join(vsixDir, `${manifest.name}-${manifest.version}.vsix`);
  const vsce = path.join(extensionDir, "node_modules", "@vscode", "vsce", "vsce");
  await run(process.execPath, [vsce, "package", "--no-dependencies", "--out", out], {
    cwd: extensionDir,
  });
  if (!fs.existsSync(out)) fail(`vsce wrote no ${out}`);
  const size = (fs.statSync(out).size / (1024 * 1024)).toFixed(1);
  info(`${path.relative(repoRoot, out)} (${size} MB)`);
  return out;
}

/** Runs test/suite.ts inside a VS Code that test-electron downloads into
 * editors/vscode/.vscode-test on first use, over a fixture workspace. The
 * only check that exercises the real extension host. */
async function smoke(): Promise<void> {
  step("smoke test in VS Code");
  const result = await run(process.execPath, [path.join("test", "run.mts")], {
    cwd         : extensionDir,
    allowFailure: true,
  });
  if (result.code !== 0) fail("the extension smoke test failed");
  info("ok: the extension linted, offered fixes and fixed all in VS Code");
}

/** Installs the VSIX into the `code` on PATH, replacing the installed one. */
async function install(vsix: string): Promise<void> {
  step("code --install-extension");
  if (shell && /\s/.test(vsix)) fail(`the VSIX path has a space in it: ${vsix}`);
  await run("code", ["--install-extension", vsix, "--force"], { shell });
}

export const command: CommandModule<object, Args> = {
  command : "vsix",
  describe: "build the VS Code extension into build/vsix/",
  builder: (yargs) =>
    yargs
      .option("wasm", {
        type    : "boolean",
        default : false,
        describe: "build the WASM engine first instead of using the built one",
      })
      .option("smoke", {
        type    : "boolean",
        default : false,
        describe: "run the extension's smoke test in a downloaded VS Code",
      })
      .option("install", {
        type    : "boolean",
        default : false,
        describe: "install the result into the `code` on PATH",
      })
      .example("$0 vsix --wasm --install", "build everything and try it in VS Code")
      .example("$0 vsix --smoke", "build and run the smoke test in a downloaded VS Code")
      .epilogue(
        `See ${color.cyan("docs/vscode-extension.md")} for the extension's layout.`
      ) as never,
  handler: async (argv) => {
    const preset = await wasmPresetToBundle(argv.wasm);
    await installDeps();
    stage(preset);
    await bundle();
    if (argv.smoke) await smoke();
    const vsix = await packageVsix();
    if (argv.install) await install(vsix);
  },
};
