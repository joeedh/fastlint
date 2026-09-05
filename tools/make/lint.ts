/** Dispatches to all lint: commands */
import type { CommandModule } from "yargs";
import { runProseLint } from "./lint-prose.ts";

export const command: CommandModule<object, {}> = {
  command : "lint",
  describe: "run lint (currently just prose lint, see lint:prose)",
  handler: async (argv) => {
    await runProseLint(["--concise"]);
  },
};
