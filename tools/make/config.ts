import fs from "node:fs";
import path from "node:path";
import type { CommandModule } from "yargs";
import { color, fail, info, step } from "./lib/log.ts";
import { repoRoot } from "./lib/paths.ts";

import { nativeConfig } from "../../source/fastlint/plugin/ts/compile.ts";
import {
  findConfig,
  loadCompiledConfig,
} from "../../source/fastlint/plugin/ts/config.ts";

interface Args {
  file?: string;
  native: boolean;
  out?: string;
}

export const command: CommandModule<object, Args> = {
  command : "config [file]",
  describe: "resolve fastlint.config.{ts,js,json} and print the JSON it compiles to",
  builder: (yargs) =>
    yargs
      .positional("file", {
        type    : "string",
        describe: "the config to read; found by walking up from here otherwise",
      })
      .option("native", {
        type    : "boolean",
        default : false,
        describe: "emit the document the native binary is handed through --config",
      })
      .option("out", {
        type    : "string",
        describe: "write the JSON to this file instead of stdout",
      })
      .example("$0 config", "print the resolved config as JSON")
      .example("$0 config --out fastlint.config.json", "compile a .ts config to JSON")
      .example(
        "$0 config --native --out native.config.json",
        "emit the native handoff beside the config it came from"
      )
      .epilogue(
        `See ${color.cyan("docs/rules.md")} "Config" for the schema and what each key means.`
      ) as never,
  handler: async (argv) => {
    const configPath = argv.file ? path.resolve(argv.file) : findConfig(process.cwd());
    if (!configPath) {
      fail("no fastlint.config.{ts,mts,js,mjs,json} here or above");
    }
    if (!fs.existsSync(configPath)) {
      fail(`${configPath} does not exist`);
    }

    const compiled = await loadCompiledConfig(configPath);
    for (const name of compiled.unknownRules) {
      info(color.yellow(`rule '${name}' is not defined by its plugin`));
    }
    const document = argv.native ? nativeConfig(compiled) : compiled.file;
    const json = `${JSON.stringify(document, undefined, 2)}\n`;

    if (!argv.out) {
      process.stdout.write(json);
      return;
    }
    const out = path.resolve(argv.out);
    fs.mkdirSync(path.dirname(out), { recursive: true });
    fs.writeFileSync(out, json);
    step(`wrote ${path.relative(repoRoot, out)}`);
    // Globs and tsconfig paths are read relative to the config's own directory,
    // so a native document elsewhere would resolve them against the wrong root.
    if (argv.native && path.dirname(out) !== path.dirname(configPath)) {
      info(
        color.yellow(
          "the native binary anchors globs at the config's directory; write this beside the config it came from"
        )
      );
    }
  },
};
