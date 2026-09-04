import fs from "node:fs";
import type { CommandModule } from "yargs";
import { info } from "./lib/log.ts";
import { presetOptions, resolvePreset, type PresetArgs } from "./lib/preset.ts";
import { buildDir, buildRoot, cacheDir } from "./lib/paths.ts";

interface Args extends PresetArgs {
  all: boolean;
}

export const command: CommandModule<object, Args> = {
  command : "clean",
  describe: "remove build/<preset>, or everything with --all",
  builder: (yargs) =>
    presetOptions(yargs).option("all", {
      type    : "boolean",
      default : false,
      describe: "remove build/ and .cache/ entirely",
    }) as never,
  handler: (argv) => {
    const targets = argv.all ? [buildRoot, cacheDir] : [buildDir(resolvePreset(argv))];
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
