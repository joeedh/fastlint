import type { CommandModule } from "yargs";
import { color, fail, info, step } from "./lib/log.ts";
import { formatAll } from "./format.ts";
import { presetOptions, resolvePreset, type PresetArgs } from "./lib/preset.ts";
import { buildPreset } from "./build.ts";
import { generate } from "../gen-ast.ts";
import { runCppTests, runTsTests } from "./test.ts";

interface Args extends PresetArgs {
  all: boolean;
}

/** Typechecks the VS Code extension. Its `vscode` types live in its own
 * node_modules, so a checkout that has not run `vsix` yet is skipped with a
 * note rather than failed. */
async function typecheckExtension(): Promise<boolean> {
  const { run } = await import("./lib/spawn.ts");
  const { repoRoot } = await import("./lib/paths.ts");
  const { extensionDir } = await import("./vsix.ts");
  const fs = await import("node:fs");
  const path = await import("node:path");
  if (!fs.existsSync(path.join(extensionDir, "node_modules"))) {
    info(
      color.yellow(
        "editors/vscode has no node_modules; run `node make.ts vsix` to check it"
      )
    );
    return true;
  }
  step("typecheck editors/vscode");
  const result = await run(
    process.execPath,
    [
      "node_modules/typescript/lib/tsc.js",
      "--noEmit",
      "-p",
      "editors/vscode/tsconfig.json",
    ],
    { cwd: repoRoot, allowFailure: true, quiet: true }
  );
  return result.code === 0;
}

export const command: CommandModule<object, Args> = {
  command : "check",
  describe: "format --check, build, C++ tests, TypeScript tests",
  builder: (yargs) =>
    presetOptions(yargs).option("all", {
      type    : "boolean",
      default : false,
      describe: "include the slow and bench test tiers",
    }) as never,
  handler: async (argv) => {
    const preset = resolvePreset(argv);

    await formatAll(true);
    step("gen-ast --check");
    const generated = generate(true);
    if (generated.changed.length > 0)
      fail("generated AST files are stale; run `node make.ts gen-ast`");
    await buildPreset(preset);

    const cppOk = await runCppTests(preset, {
      preset,
      all    : argv.all,
      update : false,
      ts     : false,
      ctest  : false,
      list   : false,
      isolate: false,
    });
    const tsOk = await runTsTests();

    step("typecheck");
    const { run } = await import("./lib/spawn.ts");
    const { repoRoot } = await import("./lib/paths.ts");
    const tsc = await run(
      process.execPath,
      ["node_modules/typescript/lib/tsc.js", "--noEmit", "-p", "tsconfig.json"],
      { cwd: repoRoot, allowFailure: true, quiet: true }
    );
    const extensionOk = await typecheckExtension();

    if (!cppOk || !tsOk || tsc.code !== 0 || !extensionOk) fail("check failed");
    info(color.green("check passed"));
  },
};
