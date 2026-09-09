import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import type { CommandModule } from "yargs";
import { buildWasm } from "./lib/wasm.ts";
import { color, fail, info, step } from "./lib/log.ts";
import { npm } from "./lib/npm.ts";
import { buildDir, repoRoot } from "./lib/paths.ts";
import { run } from "./lib/spawn.ts";
import { readVersions } from "./lib/version.ts";

interface Args {
  wasm: boolean;
  smoke: boolean;
  tarball: boolean;
  install: boolean;
}

const distDir = path.join(repoRoot, "dist");

/** Where a built tarball waits for `release` to upload it and `publish` to send
 * it. Not `dist/`, which the compile step wipes. */
export const releaseDir = path.join(repoRoot, "build", "release");

/** What `npm pack` calls the tarball. A scope becomes part of the filename with
 * its punctuation flattened, so `@acme/lintrix` packs as `acme-lintrix`. */
export function tarballName(name: string, version: string): string {
  return `${name.replace(/^@/, "").replace("/", "-")}-${version}.tgz`;
}

/** Compiles the plugin surface to `dist/`, rewriting the `.ts` specifiers the
 * sources import each other by. */
async function compile(): Promise<void> {
  step("tsc -p tsconfig.package.json");
  fs.rmSync(distDir, { recursive: true, force: true });
  const result = await run(
    process.execPath,
    ["node_modules/typescript/lib/tsc.js", "-p", "tsconfig.package.json"],
    { cwd: repoRoot, allowFailure: true }
  );
  if (result.code !== 0) fail("the package did not compile");
}

/** Copies the built WASM module in, which is the engine the package falls back
 * to when no native binary is installed. */
function bundleWasm(preset: "wasm" | "wasm-release"): void {
  const from = path.join(buildDir(preset), "bin");
  const to = path.join(distDir, "wasm");
  fs.mkdirSync(to, { recursive: true });
  for (const name of ["fastlint.js", "fastlint.wasm"]) {
    const source = path.join(from, name);
    if (!fs.existsSync(source)) {
      fail(`${name} not built; run \`node make.ts build --wasm\` first`);
    }
    fs.copyFileSync(source, path.join(to, name));
    const size = (fs.statSync(source).size / (1024 * 1024)).toFixed(1);
    step(`bundled ${name} (${size} MB)`);
  }
}

/** Lints a throwaway project through the built package, which is the only way a
 * missing file or a broken specifier shows up. */
async function smoke(): Promise<void> {
  step("smoke test the package CLI");
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "lintrix-pack-"));
  try {
    fs.writeFileSync(
      path.join(dir, "lintrix.config.json"),
      '{"extends": "lintrix:recommended", "rules": {"curly": "warn"}}\n'
    );
    fs.writeFileSync(path.join(dir, "a.ts"), "if (a) b();\ndebugger;\n");
    const cli = path.join(distDir, "ts", "cli.js");
    const result = await run(
      process.execPath,
      [cli, "--engine", "wasm", "--format", "json", "--no-color", "a.ts"],
      { cwd: dir, allowFailure: true, quiet: true, capture: true }
    );
    // Exit code 1 is the run reporting problems, which is what it should find.
    if (result.code !== 1) fail(`the CLI exited ${result.code}, expected 1`);
    const reports = JSON.parse(result.stdout) as {
      messages: { ruleId: string | null }[];
    }[];
    const rules = (reports[0]?.messages ?? []).map((message) => message.ruleId);
    if (!rules.includes("curly") || !rules.includes("no-debugger")) {
      fail(`expected curly and no-debugger, got ${JSON.stringify(rules)}`);
    }
    info(`ok: the packaged CLI reported ${rules.join(", ")} through the WASM engine`);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

/** Builds `dist/` and reports whether the release WASM engine went into it. A
 * debug engine is several times the size and slower to parse with, so a package
 * carrying one is for trying the pipeline rather than for publishing. */
export async function buildPackage(options: {
  wasm: boolean;
  smoke: boolean;
}): Promise<boolean> {
  if (options.wasm) await buildWasm("wasm-release");
  const released = fs.existsSync(
    path.join(buildDir("wasm-release"), "bin", "fastlint.js")
  );
  if (!released) {
    info(
      color.yellow(
        "no wasm-release build; bundling the debug engine. Pass --wasm to build the release one."
      )
    );
  }
  await compile();
  bundleWasm(released ? "wasm-release" : "wasm");
  if (options.smoke) await smoke();
  return released;
}

/**
 * The tarball `npm publish` would send, built into `build/release/`. Package
 * scripts are off because `dist/` is already built: running `prepack` here would
 * rebuild it, and could bundle a different engine than the one just checked.
 */
export async function packTarball(): Promise<string> {
  step("npm pack");
  fs.mkdirSync(releaseDir, { recursive: true });
  const { name, manifest } = readVersions();
  const tarball = path.join(releaseDir, tarballName(name, manifest));
  fs.rmSync(tarball, { force: true });
  await npm(["pack", "--ignore-scripts", "--pack-destination", releaseDir], {
    cwd    : repoRoot,
    capture: true,
  });
  if (!fs.existsSync(tarball)) fail(`npm pack did not write ${tarball}`);
  const size = (fs.statSync(tarball).size / (1024 * 1024)).toFixed(1);
  info(`${path.relative(repoRoot, tarball)} (${size} MB)`);
  return tarball;
}

const installTestConfig = `{
  "extends": "lintrix:recommended",
  "rules": { "curly": "error" }
}
`;

/**
 * Installs `tarball` into a throwaway project and lints through the `lintrix`
 * command npm links. It is the check that covers what the tarball left out, the
 * `bin` wiring and the shim npm generates from it.
 *
 * The project gets its own HOME and npm cache, so nothing installed globally can
 * stand in for something the package should have shipped.
 */
export async function installTest(tarball: string): Promise<void> {
  step("install the tarball into a throwaway project");
  const scratch = fs.mkdtempSync(path.join(os.tmpdir(), "lintrix-install-"));
  // The shim runs through cmd.exe on Windows, which does not quote what it is
  // handed, so a space anywhere in this path would split the command.
  if (/\s/.test(scratch)) fail(`the temp path has a space in it: ${scratch}`);
  const project = path.join(scratch, "project");
  const home = path.join(scratch, "home");
  fs.mkdirSync(path.join(project, "src"), { recursive: true });
  fs.mkdirSync(home, { recursive: true });

  fs.writeFileSync(
    path.join(project, "package.json"),
    `${JSON.stringify(
      { name: "lintrix-install-test", private: true, type: "module" },
      undefined,
      2
    )}\n`
  );
  fs.writeFileSync(path.join(project, "lintrix.config.json"), installTestConfig);
  fs.writeFileSync(path.join(project, "src", "a.ts"), "if (a) b();\ndebugger;\n");

  // Node reads USERPROFILE first on Windows and HOME on POSIX, so both are set
  // to cover either host without a platform test here.
  const env = {
    ...process.env,
    HOME            : home,
    USERPROFILE     : home,
    npm_config_cache: path.join(scratch, "npm-cache"),
  };

  const shim =
    process.platform === "win32"
      ? "node_modules\\.bin\\lintrix.cmd"
      : "./node_modules/.bin/lintrix";
  const shell = process.platform === "win32";

  try {
    const installed = await npm(
      ["install", tarball, "--no-audit", "--no-fund", "--ignore-scripts"],
      { cwd: project, env, allowFailure: true, capture: true }
    );
    if (installed.code !== 0) fail(`npm install failed; the project is at ${project}`);

    const printed = await run(shim, ["--version"], {
      cwd: project,
      env,
      shell,
      capture     : true,
      quiet       : true,
      allowFailure: true,
    });
    const expected = readVersions().manifest;
    if (printed.stdout.trim() !== expected) {
      fail(
        `the installed CLI printed version '${printed.stdout.trim()}', expected ${expected}`
      );
    }

    const linted = await run(shim, ["--format", "json", "--no-color", "src"], {
      cwd: project,
      env,
      shell,
      capture     : true,
      quiet       : true,
      allowFailure: true,
    });
    if (linted.code !== 1) fail(`the installed CLI exited ${linted.code}, expected 1`);
    const reports = JSON.parse(linted.stdout) as {
      messages: { ruleId: string | null }[];
    }[];
    const rules = (reports[0]?.messages ?? []).map((message) => message.ruleId);
    if (!rules.includes("curly") || !rules.includes("no-debugger")) {
      fail(`expected curly and no-debugger, got ${JSON.stringify(rules)}`);
    }
    info(`ok: the installed CLI ${expected} reported ${rules.join(", ")}`);
  } finally {
    fs.rmSync(scratch, { recursive: true, force: true });
  }
}

export const command: CommandModule<object, Args> = {
  command : "pack",
  describe: "build the publishable npm package into dist/",
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
        describe: "lint a throwaway project through the built package",
      })
      .option("tarball", {
        type    : "boolean",
        default : false,
        describe: "also write the publishable tarball to build/release/",
      })
      .option("install", {
        type    : "boolean",
        default : false,
        describe: "install that tarball in a throwaway project and lint through it",
      })
      .example(
        "$0 pack --wasm --smoke",
        "build everything the package ships and check it"
      )
      .example("$0 pack --install", "check the tarball the way a user installs it")
      .epilogue(
        `See ${color.cyan("docs/embedding.md")} "The npm package" for what ships and why.`
      ) as never,
  handler: async (argv) => {
    await buildPackage({ wasm: argv.wasm, smoke: argv.smoke });
    if (argv.tarball || argv.install) {
      const tarball = await packTarball();
      if (argv.install) await installTest(tarball);
    }
    info(`dist/ holds the package; \`npm publish\` sends what "files" lists`);
  },
};
