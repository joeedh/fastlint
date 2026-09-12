// The engine the server lints with (task 9.3): the bundled WASM module, loaded
// once, running the built-in rules through `lintText` and the plugin rules
// through the TypeScript runtime over the same parse. Given a native run
// (task 9.5) the built-in rules go to the resident binary instead, which is
// where the type-aware ones run; the plugin rules stay here either way.

import v8 from "node:v8";

import type { CompiledConfig } from "../../../source/fastlint/plugin/ts/compile.ts";
import { nativeConfig, resolveFile } from "../../../source/fastlint/plugin/ts/compile.ts";
import type {
  EngineKind,
  FileReport,
} from "../../../source/fastlint/plugin/ts/engine.ts";
import { findWasmModule } from "../../../source/fastlint/plugin/ts/engine.ts";
import { mergeReports } from "../../../source/fastlint/plugin/ts/report.ts";
import type { FileMessages } from "../../../source/fastlint/plugin/ts/driver.ts";
import { lint, type Addon } from "../../../source/fastlint/plugin/ts/runtime.ts";
import { loadWasmAddon } from "../../../source/fastlint/plugin/ts/wasm_addon.ts";
import type { NativeRun } from "./native.ts";

type LintingAddon = Addon & { lintText: NonNullable<Addon["lintText"]> };

/** One lint's report and which engine produced it. */
export interface LintResult {
  report: FileReport;
  engine: EngineKind;
  /** Type-aware rules ran; only a native run can say so. */
  typed: boolean;
  /** Why they did not, when the native binary said. */
  typeError?: string;
}

/** The native handoff document, serialized once per compiled config. */
const handoffJson = new WeakMap<CompiledConfig, string>();

function handoffFor(compiled: CompiledConfig): string {
  let json = handoffJson.get(compiled);
  if (json === undefined) {
    json = JSON.stringify(nativeConfig(compiled));
    handoffJson.set(compiled, json);
  }
  return json;
}

export class Engine {
  private readonly addon: LintingAddon;
  /** Where the module was loaded from, for the log. */
  readonly modulePath: string;

  private constructor(addon: LintingAddon, modulePath: string) {
    this.addon = addon;
    this.modulePath = modulePath;
  }

  /** Loads the bundled module, or returns undefined when none is bundled. */
  static async load(): Promise<Engine | undefined> {
    const modulePath = findWasmModule();
    if (modulePath === undefined) return undefined;
    // V8 tiers up hot WASM functions on background threads, and on Windows a
    // process.exit() while one of those jobs is posting back trips a libuv
    // assertion (Node 24.14, seen after a handful of lints). The language
    // server library ends every run with process.exit, so the tiering is
    // turned off here: functions compile optimized on first call instead,
    // which costs nothing measurable. An unknown flag is ignored by V8.
    v8.setFlagsFromString("--no-wasm-dynamic-tiering");
    const addon = await loadWasmAddon(modulePath);
    if (!addon.lintText) {
      throw new Error(`${modulePath}: the module exports no lintText`);
    }
    return new Engine(addon as LintingAddon, modulePath);
  }

  /**
   * Lints `source` as `filePath`. Without a config the recommended preset
   * applies and no plugin rule runs. A file the config ignores gets an empty
   * report without a parse. With `native` the built-in rules run in the
   * resident binary, which sees `source` in place of the file on disk.
   */
  async lint(
    source: string,
    filePath: string,
    compiled: CompiledConfig | undefined,
    native?: NativeRun
  ): Promise<LintResult> {
    const merged = (builtIn: FileReport[], plugin: FileMessages[]): FileReport =>
      mergeReports([filePath], builtIn, plugin)[0]!;
    const kind: EngineKind = native === undefined ? "wasm" : "native";
    const result = (report: FileReport, typed = false, typeError?: string): LintResult =>
      typeError === undefined
        ? { report, engine: kind, typed }
        : { report, engine: kind, typed, typeError };

    const resolved = compiled === undefined ? undefined : resolveFile(compiled, filePath);
    if (resolved?.ignored) return result(merged([], []));

    let builtIn: FileReport[];
    let typed = false;
    let typeError: string | undefined;
    if (native !== undefined) {
      const answer = await native.client.lint(filePath, source, native.handoff);
      builtIn = answer.results.map((report) => ({ ...report, filePath }));
      typed = answer.typed;
      typeError = answer.typeError;
    } else if (compiled === undefined) {
      builtIn = JSON.parse(this.addon.lintText(source, filePath)) as FileReport[];
    } else {
      builtIn = JSON.parse(
        this.addon.lintText(source, filePath, handoffFor(compiled), compiled.baseDir)
      ) as FileReport[];
    }
    if (resolved === undefined || resolved.rules.length === 0) {
      return result(merged(builtIn, []), typed, typeError);
    }

    try {
      const messages = lint(this.addon, source, filePath, resolved.rules);
      return result(
        merged(builtIn, [{ filename: filePath, messages }]),
        typed,
        typeError
      );
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      return result(
        merged(builtIn, [
          { filename: filePath, messages: [], error: `plugin rules: ${message}` },
        ]),
        typed,
        typeError
      );
    }
  }
}
