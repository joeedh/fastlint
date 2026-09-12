// The two engines that run the built-in rules (task 8.3). The native binary is
// the fast one: the CLI writes it the handoff config and hands it the file list.
// The WASM build bundled with the package is the fallback, so `npm i lintrix`
// lints with no native binary installed. Both answer in the ESLint-shaped JSON
// `--format json` prints, so the CLI merges either one with the plugin results.

import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { nativeConfig, nativeConfigPath, type CompiledConfig } from "./compile.ts";
import type { Addon } from "./runtime.ts";
import { loadWasmAddon } from "./wasm_addon.ts";

/** A text replacement: `range` is UTF-16 offsets into the source. */
export interface EslintFix {
  range: [number, number];
  text: string;
}

/** One suggestion a message offers, applied by its `fix` when it has one. */
export interface EslintSuggestion {
  messageId?: string;
  desc: string;
  fix?: EslintFix;
}

/** One problem, in ESLint's JSON shape (docs/rules.md "Output"). */
export interface EslintMessage {
  ruleId: string | null;
  /** The rule's documentation page; absent on a syntax error. */
  url?: string;
  severity: 1 | 2;
  message: string;
  line: number;
  column: number;
  endLine?: number;
  endColumn?: number;
  messageId?: string;
  fatal?: boolean;
  fixable?: boolean;
  fix?: EslintFix;
  suggestions?: EslintSuggestion[];
}

/** One file's problems, in ESLint's JSON shape. */
export interface FileReport {
  filePath: string;
  messages: EslintMessage[];
  errorCount: number;
  warningCount: number;
  fixableErrorCount: number;
  fixableWarningCount: number;
  output?: string;
}

/** Which engine ran the built-in rules. */
export type EngineKind = "native" | "wasm";

const exeSuffix = process.platform === "win32" ? ".exe" : "";

/** `name` found on PATH, or undefined. Windows tries each PATHEXT suffix. */
function onPath(name: string): string | undefined {
  const suffixes =
    process.platform === "win32"
      ? (process.env["PATHEXT"] ?? ".EXE;.CMD;.BAT").split(";")
      : [""];
  for (const dir of (process.env["PATH"] ?? "").split(path.delimiter)) {
    if (dir.length === 0) continue;
    for (const suffix of suffixes) {
      const candidate = path.join(dir, name + suffix);
      if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) return candidate;
    }
  }
  return undefined;
}

/**
 * The native binary to drive, or undefined when there is none. The config's
 * `binary` names it, relative to the config's own directory or absolute; a bare
 * name and the fallback are looked up on PATH.
 */
export function resolveBinary(compiled: CompiledConfig): string | undefined {
  const named = compiled.file.binary;
  if (named !== undefined) {
    if (named.includes("/") || named.includes("\\") || path.isAbsolute(named)) {
      const resolved = path.resolve(compiled.baseDir, named);
      for (const candidate of [resolved, resolved + exeSuffix]) {
        if (fs.existsSync(candidate)) return candidate;
      }
      return undefined;
    }
    return onPath(named);
  }
  return onPath("lintrix");
}

/** The bundled WASM module, or undefined when this checkout has not built one.
 * The published package ships it beside the compiled sources; a run from the
 * repository picks up `build/wasm/bin`. */
export function findWasmModule(): string | undefined {
  const here = path.dirname(fileURLToPath(import.meta.url));
  const candidates = [
    path.join(here, "..", "wasm", "fastlint.js"),
    path.join(here, "..", "..", "..", "..", "build", "wasm", "bin", "fastlint.js"),
    path.join(here, "..", "..", "..", "..", "build", "wasm-release", "bin", "fastlint.js"),
  ];
  return candidates.find((candidate) => fs.existsSync(candidate));
}

/** An empty report for a file nothing was found in. */
function emptyReport(filePath: string): FileReport {
  return {
    filePath,
    messages: [],
    errorCount: 0,
    warningCount: 0,
    fixableErrorCount: 0,
    fixableWarningCount: 0,
  };
}

/** A fatal report, the shape a parse failure takes, so one bad file does not
 * take the run down. */
export function fatalReport(filePath: string, message: string): FileReport {
  return {
    filePath,
    messages: [{ ruleId: null, severity: 2, message, line: 1, column: 1, fatal: true }],
    errorCount: 1,
    warningCount: 0,
    fixableErrorCount: 0,
    fixableWarningCount: 0,
  };
}

/**
 * Lints `files` through the native binary. The handoff config is written beside
 * the config it came from, since the binary anchors globs and tsconfig paths at
 * the directory the config sits in.
 *
 * Exit code 1 only says problems were found. Anything above that is the binary
 * failing, and its stderr is raised rather than read as an empty report.
 */
export function lintWithBinary(
  binary: string,
  configPath: string,
  compiled: CompiledConfig,
  files: readonly string[]
): FileReport[] {
  const handoff = nativeConfigPath(configPath);
  fs.writeFileSync(handoff, `${JSON.stringify(nativeConfig(compiled), undefined, 2)}\n`);

  const result = spawnSync(
    binary,
    ["lint", "--config", handoff, "--format", "json", ...files],
    { encoding: "utf8", maxBuffer: 256 * 1024 * 1024 }
  );
  if (result.error) throw result.error;
  if ((result.status ?? 0) > 1) {
    throw new Error(`${binary}: ${result.stderr.trim() || `exit ${result.status}`}`);
  }
  if (result.stderr.length > 0) process.stderr.write(result.stderr);
  return JSON.parse(result.stdout || "[]") as FileReport[];
}

/**
 * Lints `files` through the bundled WASM build, one buffer at a time. The whole
 * config goes with each call, so the same rules and severities apply as the
 * native binary would give; the type-aware rules do not run, because an
 * embedding has no tsgo process (docs/embedding.md).
 */
export async function lintWithWasm(
  modulePath: string,
  compiled: CompiledConfig,
  files: readonly string[]
): Promise<FileReport[]> {
  const addon: Addon = await loadWasmAddon(modulePath);
  if (!addon.lintText) {
    throw new Error(`${modulePath}: the module exports no lintText`);
  }
  const configJson = JSON.stringify(nativeConfig(compiled));
  const reports: FileReport[] = [];
  for (const file of files) {
    let source: string;
    try {
      source = fs.readFileSync(file, "utf8");
    } catch (error) {
      reports.push(
        fatalReport(file, error instanceof Error ? error.message : String(error))
      );
      continue;
    }
    const json = addon.lintText(source, file, configJson, compiled.baseDir);
    const parsed = JSON.parse(json) as FileReport[];
    const report = parsed[0] ?? emptyReport(file);
    reports.push({ ...report, filePath: file });
  }
  return reports;
}
