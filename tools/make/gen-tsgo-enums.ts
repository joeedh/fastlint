import type { CommandModule } from "yargs";
import { color, fail, info } from "./lib/log.ts";
import { generate } from "../gen-tsgo-enums.ts";

interface Args {
  check: boolean;
}

export const command: CommandModule<object, Args> = {
  command : "gen-tsgo-enums",
  describe:
    "regenerate source/fastlint/tsgo/generated/enums.h from the typescript package",
  builder: (yargs) =>
    yargs.option("check", {
      type    : "boolean",
      default : false,
      describe: "fail if the generated header is stale instead of writing it",
    }),
  handler: async (argv) => {
    const result = await generate(argv.check);
    for (const file of result.changed) {
      info(`${argv.check ? "stale" : "wrote"} ${file}`);
    }
    if (argv.check && result.changed.length > 0) {
      fail("generated tsgo enums are stale; run `node make.ts gen-tsgo-enums`");
    }
    if (result.changed.length === 0)
      info(color.green("generated tsgo enums are current"));
  },
};
