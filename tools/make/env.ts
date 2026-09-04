import type { CommandModule } from "yargs";
import { table } from "./lib/log.ts";
import { toolchain } from "./lib/toolchain.ts";

export const command: CommandModule<object, { refresh: boolean }> = {
  command : "env",
  describe: "locate the Visual Studio toolchain and cache it in .cache/env.json",
  builder: (yargs) =>
    yargs.option("refresh", {
      type    : "boolean",
      default : false,
      describe: "re-probe instead of reading the cache",
    }),
  handler: (argv) => {
    const tools = toolchain(argv.refresh);
    table(
      ["tool", "path"],
      [
        ["visual studio", `${tools.vsDisplayName} (${tools.vsVersion})`],
        ["install", tools.vsInstallPath],
        ["cmake", tools.cmake],
        ["ctest", tools.ctest],
        ["ninja", tools.ninja],
        ["clang-format", tools.clangFormat],
        ["clang-cl", tools.clangCl ?? "(not installed)"],
        ["vcvarsall", tools.vcvarsall],
        ["node", tools.node],
      ]
    );
  },
};
