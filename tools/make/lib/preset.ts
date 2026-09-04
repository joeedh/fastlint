import fs from "node:fs";
import { fail } from "./log.ts";
import { buildDir, presets, type Preset } from "./paths.ts";

export interface PresetArgs {
  preset: string;
  asan?: boolean;
}

/** Resolves `--preset` plus the `--asan` shorthand into one preset name. */
export function resolvePreset(argv: PresetArgs): Preset {
  const name = argv.asan && argv.preset === "debug" ? "asan" : argv.preset;
  if (!(presets as readonly string[]).includes(name)) {
    fail(`unknown preset ${name}; expected one of ${presets.join(", ")}`);
  }
  return name as Preset;
}

export function requireConfigured(preset: string): string {
  const dir = buildDir(preset);
  if (!fs.existsSync(`${dir}/CMakeCache.txt`)) {
    fail(
      `preset ${preset} is not configured; run: node make.ts configure --preset ${preset}`
    );
  }
  return dir;
}

export function presetOptions<T>(yargs: T): T {
  // Typed loosely because yargs' builder type does not survive being factored
  // out of the command object.
  const y = yargs as unknown as {
    option(name: string, config: object): unknown;
  };
  y.option("preset", {
    type    : "string",
    default : "debug",
    choices : [...presets],
    describe: "build preset",
  });
  y.option("asan", {
    type    : "boolean",
    default : false,
    describe: "shorthand for --preset asan",
  });
  return yargs;
}
