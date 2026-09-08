import fs from "node:fs";
import type { CommandModule } from "yargs";
import { info } from "./lib/log.ts";
import { presetOptions, resolvePreset, type PresetArgs } from "./lib/preset.ts";
import { buildDir, buildRoot, cacheDir, napiDir, wasmPresets } from "./lib/paths.ts";

interface Args extends PresetArgs {
  all: boolean;
  wasm: boolean;
  napi: boolean;
}

export const command: CommandModule<object, Args> = {
  command : "clean",
  describe: "remove build/<preset>, or everything with --all",
  builder: (yargs) =>
    presetOptions(yargs)
      .option("all", {
        type    : "boolean",
        default : false,
        describe: "remove build/ and .cache/ entirely",
      })
      .option("wasm", {
        type    : "boolean",
        default : false,
        describe: "remove the emscripten build directories",
      })
      .option("napi", {
        type    : "boolean",
        default : false,
        describe: "remove the N-API addon's build directory",
      }) as never,
  handler: (argv) => {
    // The emsdk install under vendor/ survives every one of these: it is a
    // download measured in hundreds of megabytes, and nothing in a build
    // directory can leave it stale.
    let targets: string[];
    if (argv.all) targets = [buildRoot, cacheDir];
    else if (argv.wasm) targets = wasmPresets.map(buildDir);
    else if (argv.napi) targets = [napiDir];
    else targets = [buildDir(resolvePreset(argv))];

    for (const dir of targets) {
      if (fs.existsSync(dir)) {
        fs.rmSync(dir, { recursive: true, force: true });
        info(`removed ${dir}`);
      } else {
        info(`${dir} does not exist`);
      }
    }
  },
};
