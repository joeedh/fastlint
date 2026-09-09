// An example fastlint config, loaded by the driver's smoke (task 7.2). A real
// project's `fastlint.config.ts` looks like this: it declares the plugins whose
// rules it wants and sets a severity for each, and `defineConfig` type-checks
// the shape.

import { defineConfig } from "./schema.ts";

export default defineConfig({
  plugins: { example: "./rules/index.ts" },
  rules: {
    "example/no-debugger": "error",
    "example/no-console": "warn",
    "example/no-var": "error",
    "example/eqeqeq": ["error", "always"],
    "example/no-empty": "error",
  },
  overrides: [
    { files: ["**/*.test.ts"], rules: { "example/no-debugger": "off" } },
  ],
  ignores: ["**/generated/**"],
});
