// Loads a config from disk (task 8.2). A `.json` config is parsed and validated;
// a `.ts`, `.js` or `.mjs` one is an ordinary module the host imports, and its
// default export is validated the same way. Either path yields the one shape
// docs/rules.md "Config" documents, which compile.ts then binds to rules.

import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

import { compileConfig, type CompiledConfig } from "./compile.ts";
import type { FastlintConfigFile } from "./schema.ts";
import { assertConfigFile } from "./validate.ts";

/** The config file names, in the order a search prefers them. An authored
 * config wins over a compiled one, since the compiled one is its output. */
export const configNames = [
  "lintrix.config.ts",
  "lintrix.config.mts",
  "lintrix.config.js",
  "lintrix.config.mjs",
  "lintrix.config.json",
];

/** The nearest config walking up from `dir`, or undefined when there is none. */
export function findConfig(dir: string): string | undefined {
  for (let at = path.resolve(dir); ; ) {
    for (const name of configNames) {
      const candidate = path.join(at, name);
      if (fs.existsSync(candidate)) return candidate;
    }
    const parent = path.dirname(at);
    if (parent === at) return undefined;
    at = parent;
  }
}

export interface LoadOptions {
  /** Re-read a module config that Node has already imported. A long-lived host
   * (the language server) passes this when the file changed on disk, since
   * Node's import cache never sees the change. Each fresh load is a new module
   * instance, so a host that loads often is holding every earlier one. */
  fresh?: boolean;
}

/**
 * Reads and validates the config at `configPath`. A module config is imported,
 * and its rules are read from the default export or from a named `config`
 * export; the import is cached by Node, so re-reading one file is free unless
 * `fresh` is set.
 */
export async function loadConfigFile(
  configPath: string,
  options: LoadOptions = {}
): Promise<FastlintConfigFile> {
  if (configPath.endsWith(".json")) {
    const text = fs.readFileSync(configPath, "utf8");
    let parsed: unknown;
    try {
      parsed = JSON.parse(text);
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      throw new Error(`${configPath}: ${message}`);
    }
    return assertConfigFile(parsed, configPath);
  }

  const url = pathToFileURL(configPath);
  if (options.fresh) url.searchParams.set("t", String(Date.now()));
  const module = (await import(url.href)) as Record<string, unknown>;
  const candidate = module["default"] ?? module["config"];
  if (candidate === undefined) {
    throw new Error(`${configPath}: config has no default export`);
  }
  return assertConfigFile(candidate, configPath);
}

/** Loads `configPath` and imports the plugins it declares. Globs and tsconfig
 * paths anchor at the config file's own directory. */
export async function loadCompiledConfig(
  configPath: string,
  options: LoadOptions = {}
): Promise<CompiledConfig> {
  const file = await loadConfigFile(configPath, options);
  return compileConfig(file, path.dirname(path.resolve(configPath)));
}
