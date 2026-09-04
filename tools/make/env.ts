import type { CommandModule } from "yargs";
import { table } from "./lib/log.ts";
import { isWindows, toolchain } from "./lib/toolchain.ts";

export const command: CommandModule<object, { refresh: boolean }> = {
  command : "env",
  describe: "locate the build toolchain and cache it in .cache/env.json",
  builder: (yargs) =>
    yargs.option("refresh", {
      type    : "boolean",
      default : false,
      describe: "re-probe instead of reading the cache",
    }),
  handler: (argv) => {
    const tools = toolchain(argv.refresh);
    const rows: [string, string][] = [];
    if (isWindows) {
      rows.push(["visual studio", `${tools.vsDisplayName} (${tools.vsVersion})`]);
      rows.push(["install", tools.vsInstallPath ?? ""]);
    } else {
      rows.push(["platform", process.platform]);
    }
    table(
      ["tool", "path"],
      [
        ...rows,
        ["cmake", tools.cmake],
        ["ctest", tools.ctest],
        ["ninja", tools.ninja],
        ["clang-format", tools.clangFormat ?? "(not installed)"],
        ["clang-cl", tools.clangCl ?? "(not installed)"],
        ...(isWindows ? [["vcvarsall", tools.vcvarsall ?? ""] as [string, string]] : []),
        ["node", tools.node],
      ]
    );
  },
};
