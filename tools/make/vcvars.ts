import type { CommandModule } from "yargs";
import { info, table } from "./lib/log.ts";
import { captureVcvars } from "./lib/toolchain.ts";

export const command: CommandModule<object, { arch: string; refresh: boolean }> = {
  command : "vcvars",
  describe: "run vcvarsall.bat and cache the environment it sets",
  builder: (yargs) =>
    yargs
      .option("arch", {
        type    : "string",
        default : "x64",
        describe: "vcvarsall architecture",
      })
      .option("refresh", {
        type    : "boolean",
        default : false,
        describe: "re-run vcvarsall",
      }),
  handler: (argv) => {
    const vcvars = captureVcvars(argv.arch, argv.refresh);
    const names = Object.keys(vcvars.changed).sort();
    info(
      `${names.length} variables changed by vcvarsall ${vcvars.arch} (VS ${vcvars.vsVersion})`
    );
    table(
      ["variable", "value"],
      names.map((name) => {
        const value = vcvars.changed[name] ?? "";
        return [name, value.length > 100 ? `${value.slice(0, 97)}...` : value];
      })
    );
  },
};
