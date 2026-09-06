import type { CommandModule } from "yargs";
import { color, fail, info } from "./lib/log.ts";
import { generate } from "../gen-ast.ts";

interface Args {
  check: boolean;
}

export const command: CommandModule<object, Args> = {
  command : "gen-ast",
  describe: "regenerate source/fastlint/ast/generated/ from nodes.def",
  builder: (yargs) =>
    yargs.option("check", {
      type    : "boolean",
      default : false,
      describe: "fail if any generated file is stale instead of writing it",
    }),
  handler: async (argv) => {
    const result = generate(argv.check);
    for (const file of result.changed) {
      info(`${argv.check ? "stale" : "wrote"} ${file}`);
    }
    if (argv.check && result.changed.length > 0) {
      fail("generated AST files are stale; run `node make.ts gen-ast`");
    }
    if (result.changed.length === 0) info(color.green("generated AST files are current"));
  },
};
