// An example fastlint config, loaded by the driver's smoke (task 7.2). A real
// project's `fastlint.config.ts` looks like this: it imports rules and lists
// them, and `defineConfig` type-checks the shape.

import { defineConfig } from "./config.ts";
import { noConsole, noDebugger } from "./rules/no-debugger.ts";

export default defineConfig({
  rules: [noDebugger, noConsole],
});
