// The package surface for TypeScript rules over the N-API addon (task 7.2): the
// rule and context types, the config loader, and the file driver. A host wraps
// the built `.node` addon and calls `lintFiles`; a rule author imports `Rule`
// and `defineConfig`.

export type {
  Addon,
  LintMessage,
  ReportDescriptor,
  Rule,
  RuleContext,
  Visitors,
} from "./runtime.ts";
export { lint } from "./runtime.ts";

export type { FastlintConfig } from "./config.ts";
export { defineConfig, loadConfig } from "./config.ts";

// The unified config shape (task 8.1). Its `defineConfig` replaces the rule-list
// one above once the loader reads this shape (task 8.2).
export type {
  FastlintConfigFile,
  Globs,
  Override,
  Preset,
  ProjectMapping,
  RuleSetting,
  RuleSettings,
  Severity,
  SeverityName,
} from "./schema.ts";

export type { FileMessages, LintFilesOptions } from "./driver.ts";
export { lintFiles } from "./driver.ts";

export { NodeKind, kindNames } from "../generated/ts/views.ts";
export type { Node } from "../generated/ts/views.ts";
