import type { CommandModule } from "yargs";
import { color, fail, info, step } from "./lib/log.ts";
import { repoRoot, sourceFiles } from "./lib/paths.ts";
import { run } from "./lib/spawn.ts";
import { toolchain } from "./lib/toolchain.ts";

interface Args {
  check: boolean;
}

const prettierBin = "node_modules/@pathtx/prettier/bin/prettier.cjs";
const prettierGlobs = ["make.ts", "tools/**/*.ts", "*.json", ".prettierrc"];

export async function formatAll(check: boolean): Promise<void> {
  const tools = toolchain();
  const files = sourceFiles();

  step(check ? "clang-format --dry-run" : "clang-format -i");
  if (files.length === 0) {
    info("no C++ sources yet");
  } else {
    const args = check ? ["--dry-run", "--Werror", ...files] : ["-i", ...files];
    const result = await run(tools.clangFormat, args, {
      cwd         : repoRoot,
      allowFailure: true,
      quiet       : true,
    });
    if (result.code !== 0) fail("clang-format found unformatted C++ sources");
  }

  step(check ? "prettier --check" : "prettier --write");
  const result = await run(
    process.execPath,
    [prettierBin, check ? "--check" : "--write", ...prettierGlobs],
    { cwd: repoRoot, allowFailure: true, quiet: true }
  );
  if (result.code !== 0) fail("prettier found unformatted files");
  info(color.green(check ? "format check passed" : "formatted"));
}

export const command: CommandModule<object, Args> = {
  command : "format",
  describe: "clang-format over source/, prettier over the TypeScript",
  builder: (yargs) =>
    yargs.option("check", {
      type    : "boolean",
      default : false,
      describe: "fail instead of rewriting",
    }),
  handler: async (argv) => {
    await formatAll(argv.check);
  },
};
