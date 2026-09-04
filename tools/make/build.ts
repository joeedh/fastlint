import fs from "node:fs";
import type { CommandModule } from "yargs";
import { step } from "./lib/log.ts";
import { presetOptions, resolvePreset, type PresetArgs } from "./lib/preset.ts";
import { buildDir, repoRoot } from "./lib/paths.ts";
import { run } from "./lib/spawn.ts";
import { buildEnv, toolchain } from "./lib/toolchain.ts";

interface Args extends PresetArgs {
  target?: string;
  jobs?: number;
}

/**
 * Builds the preset, configuring it first when its build directory has no
 * cache. Other commands call this so `test` and `run` work from a fresh clone.
 */
export async function buildPreset(
  preset: string,
  options: { target?: string; jobs?: number } = {}
): Promise<void> {
  const tools = toolchain();
  const env = buildEnv();
  const dir = buildDir(preset);
  if (!fs.existsSync(`${dir}/CMakeCache.txt`)) {
    step(`configure ${preset}`);
    await run(tools.cmake, ["--preset", preset], { cwd: repoRoot, env });
  }
  step(`build ${preset}${options.target ? ` (${options.target})` : ""}`);
  const args = ["--build", dir];
  if (options.target) args.push("--target", options.target);
  if (options.jobs) args.push("--parallel", String(options.jobs));
  await run(tools.cmake, args, { cwd: repoRoot, env });
}

export const command: CommandModule<object, Args> = {
  command : "build",
  describe: "build with ninja, configuring the preset first if needed",
  builder: (yargs) =>
    presetOptions(yargs)
      .option("target", { type: "string", describe: "build one target instead of all" })
      .option("jobs", { type: "number", alias: "j", describe: "parallel jobs" }) as never,
  handler: async (argv) => {
    await buildPreset(resolvePreset(argv), { target: argv.target, jobs: argv.jobs });
  },
};
