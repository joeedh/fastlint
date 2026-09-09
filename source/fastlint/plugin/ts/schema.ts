// The unified config shape (task 8.1), mirrored in TypeScript. `fastlint.config.json`
// is the on-disk form both consumers read: the native binary parses it in C++
// (lint/config.cc), and the npm CLI compiles a `.ts` or `.js` config down to it.
// schema/fastlint.config.schema.json validates the same shape in an editor, and
// docs/rules.md "Config" documents what each key means and which side reads it.
//
// `defineConfig` here is the typed authoring wrapper for the new shape. The
// package surface still exports the pre-8.1 rule-list wrapper from config.ts;
// index.ts swaps over when the loader lands (task 8.2).

/** A severity as a name, the spelling `defineConfig` should prefer. */
export type SeverityName = "off" | "warn" | "error";

/** A severity in either spelling; 0, 1 and 2 mirror ESLint's numbers. */
export type Severity = SeverityName | 0 | 1 | 2;

/** A rule's setting: a bare severity, or the severity followed by its options. */
export type RuleSetting = Severity | readonly [Severity, ...unknown[]];

/** A preset `extends` names. Both resolve against the C++ rule registry. */
export type Preset = "fastlint:recommended" | "fastlint:all";

/** One glob or a list of them. */
export type Globs = string | readonly string[];

/** Rule settings by rule name. A plugin rule is named `prefix/rule`. */
export type RuleSettings = Readonly<Record<string, RuleSetting>>;

/** Rule settings applied to the files one entry's globs match. */
export interface Override {
  readonly files: Globs;
  readonly rules?: RuleSettings;
}

/** The tsconfig the type-aware rules use for the files a glob matches. */
export interface ProjectMapping {
  readonly files: Globs;
  readonly project: string;
}

/** The whole config, as it is written on disk. */
export interface FastlintConfigFile {
  /** schema/fastlint.config.schema.json, for editor validation. */
  readonly $schema?: string;
  readonly extends?: Preset | readonly Preset[];
  /** A prefix to the module defining its rules, so `prefix/rule` resolves. */
  readonly plugins?: Readonly<Record<string, string>>;
  readonly rules?: RuleSettings;
  readonly overrides?: readonly Override[];
  readonly ignores?: readonly string[];
  readonly project?: string;
  readonly projects?: readonly ProjectMapping[];
  readonly reportUnusedDisableDirectives?: Severity | boolean;
  readonly eslintDirectives?: boolean;
  /** The native executable the npm CLI drives; the bundled WASM build runs
   * when it names none. */
  readonly binary?: string;
}

/** Identity helper a config file calls so its literal is type-checked at authoring. */
export function defineConfig(config: FastlintConfigFile): FastlintConfigFile {
  return config;
}
