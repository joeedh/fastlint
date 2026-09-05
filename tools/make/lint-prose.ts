import type { CommandModule } from "yargs";
import { color, fail, info, step } from "./lib/log.ts";
import { repoRoot, sourceFiles } from "./lib/paths.ts";
import { run } from "./lib/spawn.ts";
import { clangFormatMajor, minClangFormatMajor, toolchain } from "./lib/toolchain.ts";

export function runProseLint(cargs: string[]) {
  return run("node", ["node_modules/comment-lint/bin/commentlint.js", ...cargs], {
    cwd: repoRoot,
  });
}

interface Args {
  concise: boolean;
  json: boolean;
}

export const command: CommandModule<object, Args> = {
  command : "lint:prose",
  describe:
    "run comment-lint to check prose in the project, pass args to commentlint with -- <args>",
  builder: (yargs) =>
    yargs
      .parserConfiguration({
        "populate--": true,
      })
      .option("concise", {
        type    : "boolean",
        default : true,
        describe: "use concise output",
      })
      .option("json", {
        type    : "boolean",
        default : false,
        describe: "use json output, mutually incompatible with --concise",
      }),
  handler: async (argv) => {
    console.log(argv["--"]);
    const cargs = (argv["--"] ?? []) as string[];
    if (argv.json) {
      cargs.splice(0, 0, "--json");
    } else if (argv.concise) {
      cargs.splice(0, 0, "--concise");
    }

    await runProseLint(cargs);
  },
};
