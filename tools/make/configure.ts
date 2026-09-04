import type { CommandModule } from "yargs";
import { step } from "./lib/log.ts";
import { presetOptions, resolvePreset, type PresetArgs } from "./lib/preset.ts";
import { buildDir, repoRoot } from "./lib/paths.ts";
import { run } from "./lib/spawn.ts";
import { buildEnv, toolchain } from "./lib/toolchain.ts";

interface Args extends PresetArgs {
  wasm: boolean;
}

export const command: CommandModule<object, Args> = {
  command : "configure",
  describe: "run cmake configure into build/<preset>",
  builder: (yargs) =>
    presetOptions(yargs).option("wasm", {
      type    : "boolean",
      default : false,
      describe: "configure the WASM build (task 7.3)",
    }) as never,
  handler: async (argv) => {
    const preset = resolvePreset(argv);
    if (argv.wasm) {
      throw new Error("the WASM build lands with task 7.3");
    }
    const tools = toolchain();
    step(`configure ${preset} -> ${buildDir(preset)}`);
    await run(tools.cmake, ["--preset", preset], {
      cwd: repoRoot,
      env: buildEnv(),
    });
  },
};
