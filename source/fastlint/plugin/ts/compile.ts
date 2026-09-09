// The config compiler (task 8.2). It imports the modules `plugins` names, binds
// each `prefix/rule` to the rule object it stands for, resolves the layers a
// file is linted with, and emits the document the native binary is handed.
//
// The split of labour is the point: the native binary runs the C++ rules and
// this side runs the TypeScript ones, over one config that describes both. The
// layering here mirrors lint/config.cc `Config::resolve` so a rule's severity
// does not depend on which side reads it.

import path from "node:path";
import { pathToFileURL } from "node:url";

import { globMatch, pathCaseInsensitive, relativePath } from "./glob.ts";
import type { FastlintConfigFile, RuleSetting } from "./schema.ts";
import type { ResolvedRule, Rule } from "./runtime.ts";
import { settingOptions, settingSeverity } from "./validate.ts";

/** What a plugin module exports: its rules, keyed by their unprefixed names. */
export interface Plugin {
  readonly rules: Readonly<Record<string, Rule>>;
}

/** A config with its plugins imported and its rules bound. */
export interface CompiledConfig {
  /** The config as written, after validation. */
  readonly file: FastlintConfigFile;
  /** The directory globs and relative paths are anchored at. */
  readonly baseDir: string;
  /** Every rule a plugin defines, keyed by its namespaced name. */
  readonly rules: ReadonlyMap<string, Rule>;
  /** Configured names under a declared prefix that its plugin does not define. */
  readonly unknownRules: readonly string[];
}

/** What one file is linted with on this side. */
export interface ResolvedFile {
  /** The plugin rules, including those set to `off`, in configured order. */
  readonly rules: ResolvedRule[];
  /** Matched an `ignores` glob. */
  readonly ignored: boolean;
}

/** True when `value` has the shape of a `Rule`, so a bad plugin fails loudly. */
function isRule(value: unknown): value is Rule {
  if (typeof value !== "object" || value === null) return false;
  const rule = value as Partial<Rule>;
  return typeof rule.name === "string" && typeof rule.create === "function";
}

/** The `plugins` prefix `name` carries, or undefined when no prefix claims it.
 * The prefix runs to the last slash, so a scoped package keeps the slash of its
 * own name (`@acme/pack/no-foo` carries the prefix `@acme/pack`). */
export function pluginPrefixOf(
  name: string,
  prefixes: readonly string[]
): string | undefined {
  const slash = name.lastIndexOf("/");
  if (slash < 0) return undefined;
  const prefix = name.slice(0, slash);
  return prefixes.includes(prefix) ? prefix : undefined;
}

/** Resolves a plugin specifier: a bare name is left to Node, a relative path is
 * anchored at the config's directory. */
function specifierUrl(specifier: string, baseDir: string): string {
  if (specifier.startsWith("./") || specifier.startsWith("../")) {
    return pathToFileURL(path.resolve(baseDir, specifier)).href;
  }
  if (path.isAbsolute(specifier)) return pathToFileURL(specifier).href;
  return specifier;
}

/** The rules of the plugin module at `specifier`, read from its default export
 * or from a named `plugin` or `rules` export. */
async function importPlugin(
  prefix: string,
  specifier: string,
  baseDir: string
): Promise<Readonly<Record<string, Rule>>> {
  const module = (await import(specifierUrl(specifier, baseDir))) as Record<
    string,
    unknown
  >;
  const candidate = module["default"] ?? module["plugin"] ?? module;
  const rules =
    candidate && typeof candidate === "object" && "rules" in candidate
      ? (candidate as { rules: unknown }).rules
      : module["rules"];
  if (typeof rules !== "object" || rules === null || Array.isArray(rules)) {
    throw new Error(`plugin "${prefix}" (${specifier}) exports no 'rules' object`);
  }
  for (const [name, rule] of Object.entries(rules)) {
    if (!isRule(rule)) {
      throw new Error(`plugin "${prefix}" (${specifier}): rule '${name}' is not a rule`);
    }
  }
  return rules as Record<string, Rule>;
}

/** Every rule name the config configures, base layer then overrides, once each. */
function configuredNames(file: FastlintConfigFile): string[] {
  const names: string[] = [];
  const add = (rules: Readonly<Record<string, RuleSetting>> | undefined): void => {
    for (const name of Object.keys(rules ?? {})) {
      if (!names.includes(name)) names.push(name);
    }
  };
  add(file.rules);
  for (const override of file.overrides ?? []) add(override.rules);
  return names;
}

/**
 * Imports every plugin the config declares and binds the namespaced rule names
 * to what they define. A name under a declared prefix that the plugin does not
 * define is collected rather than thrown: the native binary reports an unknown
 * native rule the same way, once per linted file.
 */
export async function compileConfig(
  file: FastlintConfigFile,
  baseDir: string
): Promise<CompiledConfig> {
  const prefixes = Object.keys(file.plugins ?? {});
  const rules = new Map<string, Rule>();
  for (const prefix of prefixes) {
    const specifier = file.plugins![prefix]!;
    for (const [name, rule] of Object.entries(
      await importPlugin(prefix, specifier, baseDir)
    )) {
      rules.set(`${prefix}/${name}`, rule);
    }
  }
  const unknownRules = configuredNames(file).filter(
    (name) => pluginPrefixOf(name, prefixes) !== undefined && !rules.has(name)
  );
  return { file, baseDir, rules, unknownRules };
}

/** True when `filename` matches an `ignores` glob, which needs no plugin. */
export function isIgnored(
  file: FastlintConfigFile,
  baseDir: string,
  filename: string
): boolean {
  const relative = relativePath(baseDir, filename);
  return (file.ignores ?? []).some((glob) =>
    globMatch(glob, relative, pathCaseInsensitive)
  );
}

/**
 * The plugin rules `filename` is linted with, after every matching override.
 * A later layer's bare severity keeps the options an earlier layer gave, as it
 * does natively.
 */
export function resolveFile(compiled: CompiledConfig, filename: string): ResolvedFile {
  const relative = relativePath(compiled.baseDir, filename);
  const resolved: ResolvedRule[] = [];

  const apply = (id: string, setting: RuleSetting): void => {
    const rule = compiled.rules.get(id);
    if (!rule) return;
    const severity = settingSeverity(setting);
    const options = settingOptions(setting);
    const found = resolved.find((entry) => entry.id === id);
    if (!found) {
      resolved.push({ id, rule, severity, options });
      return;
    }
    found.severity = severity;
    if (Array.isArray(setting)) found.options = options;
  };

  for (const [name, setting] of Object.entries(compiled.file.rules ?? {})) {
    apply(name, setting);
  }
  for (const override of compiled.file.overrides ?? []) {
    const globs = typeof override.files === "string" ? [override.files] : override.files;
    if (!globs.some((glob) => globMatch(glob, relative, pathCaseInsensitive))) continue;
    for (const [name, setting] of Object.entries(override.rules ?? {})) {
      apply(name, setting);
    }
  }

  return {
    rules: resolved,
    ignored: isIgnored(compiled.file, compiled.baseDir, filename),
  };
}

/** What the native handoff document is called. The leading dot marks it as
 * generated: it is written next to the config it came from, and a project
 * ignores it rather than committing it. */
export const nativeConfigName = ".fastlint.native.json";

/** Where `configPath`'s handoff document belongs. Globs and tsconfig paths
 * anchor at the config file's directory, so it goes beside the config. */
export function nativeConfigPath(configPath: string): string {
  return path.join(path.dirname(path.resolve(configPath)), nativeConfigName);
}

/**
 * The config the native binary is handed through `--config`. It drops the keys
 * only this side reads and every rule a plugin owns, so the native run reports
 * no rule it cannot run and still reports a name nothing defines.
 *
 * Write it at `nativeConfigPath`, since anywhere else resolves the relative
 * globs and tsconfig paths against the wrong directory.
 */
export function nativeConfig(compiled: CompiledConfig): FastlintConfigFile {
  const prefixes = Object.keys(compiled.file.plugins ?? {});
  const strip = (
    rules: Readonly<Record<string, RuleSetting>> | undefined
  ): Record<string, RuleSetting> =>
    Object.fromEntries(
      Object.entries(rules ?? {}).filter(
        ([name]) => pluginPrefixOf(name, prefixes) === undefined
      )
    );

  // Everything else is copied through, including a key this version does not
  // know, so a newer config still reaches an older binary whole.
  const native: Record<string, unknown> = { ...compiled.file };
  delete native["$schema"];
  delete native["plugins"];
  delete native["binary"];
  if (compiled.file.rules) native["rules"] = strip(compiled.file.rules);
  if (compiled.file.overrides) {
    native["overrides"] = compiled.file.overrides.map((override) =>
      override.rules ? { ...override, rules: strip(override.rules) } : override
    );
  }
  return native as FastlintConfigFile;
}
