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
import { applyFixes, maxFixPasses, nonOverlapping } from "./fixes.ts";
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

/**
 * True when `file` is a shim npm generated for this package's own `bin`, which
 * shares the name `lintrix` with the native executable and sits first on PATH
 * under `npm exec`, `pnpm` and package scripts. A shim's real path is inside a
 * `node_modules` tree, whether the project's `.bin` or a global prefix's.
 */
export function isPackageShim(file: string): boolean {
  let real = file;
  try {
    real = fs.realpathSync(file);
  } catch {
    return true;
  }
  return real.split(/[\\/]/).includes("node_modules");
}

/**
 * The native executable called `name` on PATH, or undefined. Only a real
 * executable counts: Windows takes `.exe` alone, since `.cmd` and `.bat` there
 * are npm's shims and `spawnSync` refuses them without a shell, and a candidate
 * `isPackageShim` names is skipped on every platform.
 */
export function onPath(name: string): string | undefined {
  for (const dir of (process.env["PATH"] ?? "").split(path.delimiter)) {
    if (dir.length === 0) continue;
    const candidate = path.join(dir, name + exeSuffix);
    if (!fs.existsSync(candidate) || !fs.statSync(candidate).isFile()) continue;
    if (isPackageShim(candidate)) continue;
    return candidate;
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
 * the directory the config sits in. With `fix` the binary rewrites each file
 * and reports what its fixes left behind.
 *
 * Exit code 1 only says problems were found. Anything above that is the binary
 * failing, and its stderr is raised rather than read as an empty report.
 */
export function lintWithBinary(
  binary: string,
  configPath: string,
  compiled: CompiledConfig,
  files: readonly string[],
  fix = false
): FileReport[] {
  const handoff = nativeConfigPath(configPath);
  fs.writeFileSync(handoff, `${JSON.stringify(nativeConfig(compiled), undefined, 2)}\n`);

  const args = ["lint", "--config", handoff, "--format", "json"];
  if (fix) args.push("--fix");
  const result = spawnSync(binary, [...args, ...files], {
    encoding : "utf8",
    maxBuffer: 256 * 1024 * 1024,
  });
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
 *
 * With `fix` each file is linted and fixed until a pass finds nothing fixable
 * (or `maxFixPasses` is reached), written back when it changed, and reported
 * as the last pass saw it, which is what the native `--fix` does.
 */
export async function lintWithWasm(
  modulePath: string,
  compiled: CompiledConfig,
  files: readonly string[],
  fix = false
): Promise<FileReport[]> {
  const addon: Addon = await loadWasmAddon(modulePath);
  if (!addon.lintText) {
    throw new Error(`${modulePath}: the module exports no lintText`);
  }
  const configJson = JSON.stringify(nativeConfig(compiled));
  const lintText = (source: string, file: string): FileReport => {
    const json = addon.lintText!(source, file, configJson, compiled.baseDir);
    const parsed = JSON.parse(json) as FileReport[];
    return { ...(parsed[0] ?? emptyReport(file)), filePath: file };
  };

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
    let report = lintText(source, file);
    if (fix) {
      let text = source;
      for (let pass = 1; pass < maxFixPasses; pass++) {
        const fixes = nonOverlapping(report.messages);
        if (fixes.length === 0) break;
        text = applyFixes(text, fixes);
        report = lintText(text, file);
      }
      if (text !== source) {
        try {
          fs.writeFileSync(file, text);
        } catch (error) {
          report = fatalReport(
            file,
            `cannot write: ${error instanceof Error ? error.message : String(error)}`
          );
        }
      }
    }
    reports.push(report);
  }
  return reports;
}
