// Loads a `fastlint.config.ts` (or `.js`, `.mjs`) and hands back the rules it
// declares (task 7.2). The config is an ordinary module the host imports, so a
// rule is just a value the config references; there is no plugin resolution
// protocol beyond `import`.

import { pathToFileURL } from "node:url";

import type { Rule } from "./runtime.ts";

/** A fastlint config: the TypeScript rules to run, and the files they apply to. */
export interface FastlintConfig {
  readonly rules: readonly Rule[];
  /** Optional file list or globs; the CLI resolves them, the API may ignore them. */
  readonly files?: readonly string[];
}

/** Identity helper a config file calls so its literal is type-checked at authoring. */
export function defineConfig(config: FastlintConfig): FastlintConfig {
  return config;
}

/** True when `value` has the shape of a `Rule`, so a bad config fails loudly. */
function isRule(value: unknown): value is Rule {
  if (typeof value !== "object" || value === null) return false;
  const rule = value as Partial<Rule>;
  return typeof rule.name === "string" && typeof rule.create === "function";
}

/**
 * Imports the config module at `configPath` and returns it. The rules are read
 * from the default export, or a named `config`/`rules` export as a fallback. A
 * config whose rules are not rule-shaped is rejected here rather than at the
 * first visit.
 */
export async function loadConfig(configPath: string): Promise<FastlintConfig> {
  const module = (await import(pathToFileURL(configPath).href)) as Record<string, unknown>;
  const candidate = module["default"] ?? module["config"];
  const rules =
    candidate && typeof candidate === "object" && "rules" in candidate
      ? (candidate as { rules: unknown }).rules
      : module["rules"];

  if (!Array.isArray(rules)) {
    throw new Error(`${configPath}: config has no 'rules' array`);
  }
  rules.forEach((rule, index) => {
    if (!isRule(rule)) {
      throw new Error(`${configPath}: rules[${index}] is not a rule`);
    }
  });

  const files =
    candidate && typeof candidate === "object" && "files" in candidate
      ? (candidate as { files?: readonly string[] }).files
      : undefined;

  return { rules: rules as Rule[], files };
}
