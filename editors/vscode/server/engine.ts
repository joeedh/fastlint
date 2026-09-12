// The engine the server lints with (task 9.3): the bundled WASM module, loaded
// once, running the built-in rules through `lintText` and the plugin rules
// through the TypeScript runtime over the same parse. A native serve mode
// joins in task 9.5.

import v8 from "node:v8";

import type { CompiledConfig } from "../../../source/fastlint/plugin/ts/compile.ts";
import { nativeConfig, resolveFile } from "../../../source/fastlint/plugin/ts/compile.ts";
import type { FileReport } from "../../../source/fastlint/plugin/ts/engine.ts";
import { findWasmModule } from "../../../source/fastlint/plugin/ts/engine.ts";
import { mergeReports } from "../../../source/fastlint/plugin/ts/report.ts";
import type { FileMessages } from "../../../source/fastlint/plugin/ts/driver.ts";
import { lint, type Addon } from "../../../source/fastlint/plugin/ts/runtime.ts";
import { loadWasmAddon } from "../../../source/fastlint/plugin/ts/wasm_addon.ts";

type LintingAddon = Addon & { lintText: NonNullable<Addon["lintText"]> };

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
   * report without a parse.
   */
  lint(
    source: string,
    filePath: string,
    compiled: CompiledConfig | undefined
  ): FileReport {
    const merged = (native: FileReport[], plugin: FileMessages[]): FileReport =>
      mergeReports([filePath], native, plugin)[0]!;

    if (compiled === undefined) {
      const native = JSON.parse(this.addon.lintText(source, filePath)) as FileReport[];
      return merged(native, []);
    }
    const resolved = resolveFile(compiled, filePath);
    if (resolved.ignored) return merged([], []);

    const native = JSON.parse(
      this.addon.lintText(source, filePath, handoffFor(compiled), compiled.baseDir)
    ) as FileReport[];
    if (resolved.rules.length === 0) return merged(native, []);

    try {
      const messages = lint(this.addon, source, filePath, resolved.rules);
      return merged(native, [{ filename: filePath, messages }]);
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      return merged(native, [
        { filename: filePath, messages: [], error: `plugin rules: ${message}` },
      ]);
    }
  }
}
