import fs from "node:fs";
import type { CommandModule } from "yargs";
import { step } from "./lib/log.ts";
import { presetOptions, resolvePreset, type PresetArgs } from "./lib/preset.ts";
import { buildDir, repoRoot } from "./lib/paths.ts";
import { run } from "./lib/spawn.ts";
import { buildEnv, toolchain } from "./lib/toolchain.ts";
import { buildWasm, smokeWasm, wasmPreset } from "./lib/wasm.ts";
import { buildNapi, napiRuntimes, smokeNapi, type NapiRuntime } from "./lib/napi.ts";

interface Args extends PresetArgs {
  target?: string;
  jobs?: number;
  wasm: boolean;
  napi: boolean;
  release: boolean;
  runtime: string;
  runtimeVersion?: string;
  smoke: boolean;
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
      .option("jobs", { type: "number", alias: "j", describe: "parallel jobs" })
      .option("wasm", {
        type    : "boolean",
        default : false,
        describe: "build the emscripten module instead of the native tree",
      })
      .option("napi", {
        type    : "boolean",
        default : false,
        describe: "build the N-API addon instead of the native tree",
      })
      .option("release", {
        type    : "boolean",
        default : false,
        describe: "with --wasm, build build/wasm-release instead",
      })
      .option("runtime", {
        type    : "string",
        default : "node",
        choices : [...napiRuntimes],
        describe: "with --napi, the runtime ABI to build against",
      })
      .option("runtime-version", {
        type    : "string",
        describe: "with --napi, override the runtime version to build against",
      })
      .option("smoke", {
        type    : "boolean",
        default : false,
        describe: "with --wasm or --napi, load the result and lint one line through it",
      }) as never,
  handler: async (argv) => {
    if (argv.wasm && argv.napi) {
      throw new Error("--wasm and --napi build different trees; run one at a time");
    }
    if (argv.wasm) {
      const preset = wasmPreset(argv.release);
      await buildWasm(preset);
      if (argv.smoke) await smokeWasm(preset);
      return;
    }
    if (argv.napi) {
      await buildNapi({
        runtime       : argv.runtime as NapiRuntime,
        runtimeVersion: argv.runtimeVersion,
      });
      if (argv.smoke) await smokeNapi();
      return;
    }
    await buildPreset(resolvePreset(argv), { target: argv.target, jobs: argv.jobs });
  },
};
