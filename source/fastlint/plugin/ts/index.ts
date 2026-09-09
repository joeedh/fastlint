// The package surface for TypeScript rules over the N-API addon (task 7.2): the
// rule and context types, the config loader and compiler, and the file driver. A
// host wraps the built `.node` addon and calls `lintFiles`; a rule author imports
// `Rule` and `defineConfig`.

export type {
  Addon,
  LintMessage,
  ReportDescriptor,
  ResolvedRule,
  Rule,
  RuleContext,
  Visitors,
} from "./runtime.ts";
export { lint, resolveRule } from "./runtime.ts";

// The config: the shape it is written in, the loader that reads one, and the
// compiler that binds its plugins and resolves it per file (task 8.2).
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
export { defineConfig } from "./schema.ts";

export { configNames, findConfig, loadCompiledConfig, loadConfigFile } from "./config.ts";

export type { CompiledConfig, Plugin, ResolvedFile } from "./compile.ts";
export {
  compileConfig,
  isIgnored,
  nativeConfig,
  nativeConfigName,
  nativeConfigPath,
  resolveFile,
} from "./compile.ts";

export { validateConfigFile } from "./validate.ts";

export type { FileMessages, LintFilesOptions } from "./driver.ts";
export { lintFiles, lintOne } from "./driver.ts";

export { NodeKind, kindNames } from "../generated/ts/views.ts";
export type { Node } from "../generated/ts/views.ts";
