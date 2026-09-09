// Checks a loaded config against the schema (task 8.2). The native binary
// validates the same document while it parses (lint/config.cc), so the messages
// here are worded the way its errors are: a `.json` config that passes on one
// side passes on the other.

import type { FastlintConfigFile, RuleSetting, Severity } from "./schema.ts";

/** Prefixes the C++ registry strips from a rule name, which a plugin may not claim. */
const reservedPrefixes = ["@typescript-eslint", "typescript-eslint"];

const presets = ["fastlint:recommended", "fastlint:all"];

function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

/** True for `"off"`, `"warn"`, `"error"` and 0 to 2, the spellings a severity takes. */
export function isSeverity(value: unknown): value is Severity {
  if (typeof value === "string") return ["off", "warn", "error"].includes(value);
  return value === 0 || value === 1 || value === 2;
}

/** The name a severity is reported and compared by. */
export function severityName(value: Severity): "off" | "warn" | "error" {
  if (typeof value === "string") return value;
  return (["off", "warn", "error"] as const)[value]!;
}

/** True for the `[severity, ...options]` spelling of a setting. */
export function isSettingArray(
  setting: RuleSetting
): setting is readonly [Severity, ...unknown[]] {
  return Array.isArray(setting);
}

/** The options that follow the severity, empty for a bare one. */
export function settingOptions(setting: RuleSetting): readonly unknown[] {
  return isSettingArray(setting) ? setting.slice(1) : [];
}

/** The severity a setting carries, in either spelling. */
export function settingSeverity(setting: RuleSetting): "off" | "warn" | "error" {
  return severityName(isSettingArray(setting) ? setting[0] : setting);
}

function checkRules(rules: unknown, where: string, errors: string[]): void {
  if (!isObject(rules)) {
    errors.push(`config: ${where} must be an object`);
    return;
  }
  for (const [name, setting] of Object.entries(rules)) {
    const severity = Array.isArray(setting) ? setting[0] : setting;
    if (Array.isArray(setting) && setting.length === 0) {
      errors.push(`config: rule "${name}" needs a severity`);
    } else if (!isSeverity(severity)) {
      errors.push(
        `config: rule "${name}" needs a severity ("off", "warn", "error" or 0-2)`
      );
    }
  }
}

function checkGlobs(files: unknown, where: string, errors: string[]): void {
  if (typeof files === "string" && files.length > 0) return;
  if (Array.isArray(files) && files.length > 0 && files.every((f) => typeof f === "string"))
    return;
  errors.push(`config: ${where} must be a glob or a list of globs`);
}

/**
 * Every problem with `value` as a config, empty when it is valid. A key neither
 * side knows is left alone, so an older reader takes a config a newer writer
 * wrote; the JSON Schema is what flags a stray key in an editor.
 */
export function validateConfigFile(value: unknown): string[] {
  const errors: string[] = [];
  if (!isObject(value)) {
    return ["config: the top level must be an object"];
  }
  const config = value as Record<string, unknown>;

  if ("extends" in config) {
    const names = Array.isArray(config["extends"])
      ? config["extends"]
      : [config["extends"]];
    if (!names.every((name) => typeof name === "string")) {
      errors.push('config: "extends" must be a string or an array of strings');
    } else {
      for (const name of names) {
        if (!presets.includes(name as string)) {
          errors.push(`config: unknown preset "${String(name)}"`);
        }
      }
    }
  }

  if ("plugins" in config) {
    const plugins = config["plugins"];
    if (!isObject(plugins)) {
      errors.push('config: "plugins" must map a prefix to a module specifier');
    } else {
      for (const [prefix, specifier] of Object.entries(plugins)) {
        if (typeof specifier !== "string" || specifier.length === 0) {
          errors.push(`config: plugin "${prefix}" must name a module specifier string`);
        }
        if (reservedPrefixes.includes(prefix)) {
          errors.push(
            `config: plugin prefix "${prefix}" is reserved for the built-in rules`
          );
        }
      }
    }
  }

  if ("rules" in config) checkRules(config["rules"], '"rules"', errors);

  if ("overrides" in config) {
    const overrides = config["overrides"];
    if (!Array.isArray(overrides)) {
      errors.push('config: "overrides" must be an array');
    } else {
      overrides.forEach((entry, index) => {
        if (!isObject(entry)) {
          errors.push(`config: overrides[${index}] must be an object`);
          return;
        }
        checkGlobs(entry["files"], `overrides[${index}] "files"`, errors);
        if ("rules" in entry) {
          checkRules(entry["rules"], `overrides[${index}] "rules"`, errors);
        }
      });
    }
  }

  if ("ignores" in config) {
    const ignores = config["ignores"];
    if (!Array.isArray(ignores) || !ignores.every((g) => typeof g === "string")) {
      errors.push('config: "ignores" must be an array of globs');
    }
  }

  if ("project" in config && typeof config["project"] !== "string") {
    errors.push('config: "project" must be a tsconfig path string');
  }

  if ("projects" in config) {
    const projects = config["projects"];
    if (!Array.isArray(projects)) {
      errors.push('config: "projects" must be an array');
    } else {
      projects.forEach((entry, index) => {
        if (!isObject(entry)) {
          errors.push(`config: projects[${index}] must be an object`);
          return;
        }
        checkGlobs(entry["files"], `projects[${index}] "files"`, errors);
        if (typeof entry["project"] !== "string") {
          errors.push(
            `config: projects[${index}] needs a "project" tsconfig path`
          );
        }
      });
    }
  }

  if ("reportUnusedDisableDirectives" in config) {
    const setting = config["reportUnusedDisableDirectives"];
    if (typeof setting !== "boolean" && !isSeverity(setting)) {
      errors.push('config: "reportUnusedDisableDirectives" must be a severity');
    }
  }

  if ("eslintDirectives" in config && typeof config["eslintDirectives"] !== "boolean") {
    errors.push('config: "eslintDirectives" must be true or false');
  }

  if ("binary" in config && typeof config["binary"] !== "string") {
    errors.push('config: "binary" must be a path to the fastlint executable');
  }

  return errors;
}

/** Throws with every problem `path` has as a config, or returns it typed. */
export function assertConfigFile(value: unknown, path: string): FastlintConfigFile {
  const errors = validateConfigFile(value);
  if (errors.length > 0) {
    throw new Error(`${path}:\n  ${errors.join("\n  ")}`);
  }
  return value as FastlintConfigFile;
}
