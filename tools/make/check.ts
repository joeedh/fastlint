import type { CommandModule } from "yargs";
import { color, fail, info, step } from "./lib/log.ts";
import { formatAll } from "./format.ts";
import { presetOptions, resolvePreset, type PresetArgs } from "./lib/preset.ts";
import { buildPreset } from "./build.ts";
import { runCppTests, runTsTests } from "./test.ts";

interface Args extends PresetArgs {
  all: boolean;
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

    if (!cppOk || !tsOk || tsc.code !== 0) fail("check failed");
    info(color.green("check passed"));
  },
};
