import type { CommandModule } from "yargs";
import { step } from "./lib/log.ts";
import { presetOptions, resolvePreset, type PresetArgs } from "./lib/preset.ts";
import { buildDir, repoRoot } from "./lib/paths.ts";
import { run } from "./lib/spawn.ts";
import { buildEnv, toolchain } from "./lib/toolchain.ts";
import { configureWasm, wasmPreset } from "./lib/wasm.ts";
import { configureNapi, napiRuntimes, type NapiRuntime } from "./lib/napi.ts";

interface Args extends PresetArgs {
  wasm: boolean;
  napi: boolean;
  release: boolean;
  runtime: string;
  runtimeVersion?: string;
}

export const command: CommandModule<object, Args> = {
  command : "configure",
  describe: "run cmake configure into build/<preset>",
  builder: (yargs) =>
    presetOptions(yargs)
      .option("wasm", {
        type    : "boolean",
        default : false,
        describe: "configure the emscripten build into build/wasm",
      })
      .option("napi", {
        type    : "boolean",
        default : false,
        describe: "configure the N-API addon into build/napi, through cmake-js",
      })
      .option("release", {
        type    : "boolean",
        default : false,
        describe: "with --wasm, configure build/wasm-release instead",
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
      }) as never,
  handler: async (argv) => {
    if (argv.wasm && argv.napi) {
      throw new Error("--wasm and --napi configure different trees; run one at a time");
    }
    if (argv.wasm) {
      await configureWasm(wasmPreset(argv.release));
      return;
    }
    if (argv.napi) {
      await configureNapi({
        runtime       : argv.runtime as NapiRuntime,
        runtimeVersion: argv.runtimeVersion,
      });
      return;
    }
    const preset = resolvePreset(argv);
    const tools = toolchain();
    step(`configure ${preset} -> ${buildDir(preset)}`);
    await run(tools.cmake, ["--preset", preset], {
      cwd: repoRoot,
      env: buildEnv(),
    });
  },
};
