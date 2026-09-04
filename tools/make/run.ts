import path from "node:path";
import type { CommandModule } from "yargs";
import { buildPreset } from "./build.ts";
import { presetOptions, resolvePreset, type PresetArgs } from "./lib/preset.ts";
import { buildDir, exeSuffix, repoRoot } from "./lib/paths.ts";
import { run as spawnRun } from "./lib/spawn.ts";
import { asanEnv } from "./lib/asan.ts";

interface Args extends PresetArgs {
  args?: (string | number)[];
}

export const command: CommandModule<object, Args> = {
  command : "run [args..]",
  describe: "build, then run fastlint with the remaining arguments",
  builder: (yargs) =>
    // Without this, everything after `--` lands in argv["--"] rather than in
    // the `args` positional, so `run -- --version` would reach fastlint empty.
    presetOptions(yargs)
      // fastlint's own flags are not make.ts's to validate.
      .strict(false)
      .parserConfiguration({ "populate--": false, "unknown-options-as-args": true })
      .positional("args", {
        array   : true,
        describe: "arguments passed through to fastlint",
      }) as never,
  handler: async (argv) => {
    const preset = resolvePreset(argv);
    await buildPreset(preset, { target: "fastlint" });
    const exe = path.join(buildDir(preset), "bin", `fastlint${exeSuffix}`);
    // With `populate--` off, tokens after `--` land in `_` behind the command
    // name rather than in the positional.
    const extra = (argv._ ?? []).slice(1).map(String);
    const args = [...(argv.args ?? []).map(String), ...extra];
    await spawnRun(exe, args, {
      cwd: repoRoot,
      env: asanEnv(preset),
    });
  },
};
