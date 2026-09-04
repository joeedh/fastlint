#!/usr/bin/env node
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import yargs from "yargs";
import { hideBin } from "yargs/helpers";
import type { CommandModule } from "yargs";

const here = path.dirname(fileURLToPath(import.meta.url));
const commandDir = path.join(here, "tools", "make");

/** Every `tools/make/<cmd>.ts`, in the order they should appear in --help. */
async function loadCommands(): Promise<CommandModule<object, never>[]> {
  const names = fs
    .readdirSync(commandDir, { withFileTypes: true })
    .filter((entry) => entry.isFile() && entry.name.endsWith(".ts"))
    .map((entry) => entry.name)
    .sort();

  const commands: CommandModule<object, never>[] = [];
  for (const name of names) {
    const module = (await import(pathToFileURL(path.join(commandDir, name)).href)) as {
      command?: CommandModule<object, never>;
    };
    if (module.command) commands.push(module.command);
  }
  return commands;
}

const cli = yargs(hideBin(process.argv))
  .scriptName("node make.ts")
  .usage("$0 <command> [options]")
  .strict()
  .demandCommand(1, "run `node make.ts --help` for the command list")
  // `run --version` should reach fastlint rather than printing make.ts's own.
  .version(false)
  .help()
  .alias("h", "help")
  .wrap(Math.min(100, process.stdout.columns ?? 100));

for (const command of await loadCommands()) {
  cli.command(command);
}

await cli.parseAsync();
