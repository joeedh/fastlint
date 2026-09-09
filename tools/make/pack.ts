import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import type { CommandModule } from "yargs";
import { buildWasm } from "./lib/wasm.ts";
import { color, fail, info, step } from "./lib/log.ts";
import { buildDir, repoRoot } from "./lib/paths.ts";
import { run } from "./lib/spawn.ts";

interface Args {
  wasm: boolean;
  smoke: boolean;
}

const distDir = path.join(repoRoot, "dist");

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
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fastlint-pack-"));
  try {
    fs.writeFileSync(
      path.join(dir, "fastlint.config.json"),
      '{"extends": "fastlint:recommended", "rules": {"curly": "warn"}}\n'
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
      .example(
        "$0 pack --wasm --smoke",
        "build everything the package ships and check it"
      )
      .epilogue(
        `See ${color.cyan("docs/embedding.md")} "The npm package" for what ships and why.`
      ) as never,
  handler: async (argv) => {
    if (argv.wasm) await buildWasm("wasm-release");
    const released = fs.existsSync(
      path.join(buildDir("wasm-release"), "bin", "fastlint.js")
    );
    if (!released) {
      // A debug module is several times the size and slower to parse with, so a
      // package built from one is for trying the pipeline, not for publishing.
      info(
        color.yellow(
          "no wasm-release build; bundling the debug engine. Pass --wasm to build the release one."
        )
      );
    }
    const preset = released ? "wasm-release" : "wasm";
    await compile();
    bundleWasm(preset);
    if (argv.smoke) await smoke();
    info(`dist/ holds the package; \`npm publish\` sends what "files" lists`);
  },
};
