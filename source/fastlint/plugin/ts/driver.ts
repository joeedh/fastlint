// Lints a set of files with a config's TypeScript rules (task 7.2). This is the
// "node CLI hosts the native core" half of the design: the JS side owns the file
// list and the config, and the addon does the parse and the accessors. Files are
// sharded across a worker pool so the rules run in parallel, one file per worker
// at a time; a single file or `concurrency: 1` stays in this thread.
//
// The config decides what each file is linted with (task 8.2): `ignores` skips a
// file outright, and the rules come from the base layer plus every override the
// file matches.

import fs from "node:fs";
import { createRequire } from "node:module";
import os from "node:os";
import { Worker } from "node:worker_threads";

import { resolveFile, type CompiledConfig } from "./compile.ts";
import { loadCompiledConfig } from "./config.ts";
import { findWasmModule } from "./engine.ts";
import { lint, type Addon, type LintMessage } from "./runtime.ts";
import { loadWasmAddon } from "./wasm_addon.ts";

/** A file handed to a worker. */
export interface LintRequest {
  id: number;
  filename: string;
}

/** A worker's answer for one file; `error` is set when the file could not be read. */
export interface LintResult {
  id: number;
  filename: string;
  messages: LintMessage[];
  error?: string;
}

/** The problems found in one file. */
export interface FileMessages {
  filename: string;
  messages: LintMessage[];
  error?: string;
  /** Matched an `ignores` glob, so no rule ran over it. */
  ignored?: boolean;
}

export interface LintFilesOptions {
  /** The `fastlint.config.*` file to take the rules from. */
  configPath: string;
  /** What to parse through: a built `.node` addon, or a WASM module's
   * `fastlint.js`. Defaults to the bundled WASM build. */
  addonPath?: string;
  /** Worker count; defaults to the CPU count for the addon and to one for the
   * WASM build, whose every worker would hold a heap of its own. `1` runs in
   * this thread with no workers. */
  concurrency?: number;
}

/** Loads whichever embedding `addonPath` names, or the bundled WASM build. */
export async function loadAddon(addonPath?: string): Promise<Addon> {
  const resolved = addonPath ?? findWasmModule();
  if (!resolved) {
    throw new Error("no addon given and no bundled WASM build to fall back to");
  }
  if (resolved.endsWith(".node")) {
    return createRequire(import.meta.url)(resolved) as Addon;
  }
  return loadWasmAddon(resolved);
}

/** Lints one file against the rules the config resolves for it. */
export function lintOne(
  addon: Addon,
  compiled: CompiledConfig,
  filename: string
): FileMessages {
  const resolved = resolveFile(compiled, filename);
  if (resolved.ignored) {
    return { filename, messages: [], ignored: true };
  }
  try {
    const source = fs.readFileSync(filename, "utf8");
    return { filename, messages: lint(addon, source, filename, resolved.rules) };
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    return { filename, messages: [], error: message };
  }
}

/** Lints `files` in this thread, without spawning a worker. */
async function lintInProcess(
  files: readonly string[],
  compiled: CompiledConfig,
  addonPath: string | undefined
): Promise<FileMessages[]> {
  const addon = await loadAddon(addonPath);
  return files.map((filename) => lintOne(addon, compiled, filename));
}

/**
 * Lints `files` and returns one entry per file, in the input order. With more
 * than one file and a concurrency above one, a worker pool does the work: each
 * worker loads the addon and the config once, and the driver hands it the next
 * file as soon as it returns the last, so a slow file never idles the others.
 *
 * Nothing is written to stderr here. A caller that wants to report a rule name
 * the plugin does not define reads `CompiledConfig.unknownRules`, as the CLI
 * does.
 */
export async function lintFiles(
  files: readonly string[],
  options: LintFilesOptions
): Promise<FileMessages[]> {
  const compiled = await loadCompiledConfig(options.configPath);

  // A WASM worker would hold a heap of its own, so the pool is for the addon.
  const isAddon = options.addonPath?.endsWith(".node") === true;
  const workers = Math.max(
    1,
    Math.min(options.concurrency ?? (isAddon ? os.cpus().length : 1), files.length)
  );
  if (workers <= 1 || files.length <= 1) {
    return lintInProcess(files, compiled, options.addonPath);
  }

  const results: FileMessages[] = new Array(files.length);
  // The published package ships compiled JavaScript, so the worker is named by
  // the extension this module itself is running as.
  const worker = import.meta.url.endsWith(".ts") ? "./lint_worker.ts" : "./lint_worker.js";
  const url = new URL(worker, import.meta.url);
  const pool: Worker[] = [];
  let next = 0;
  let done = 0;

  // An ignored file needs no worker, and its entry is filled in here so the
  // caller still gets one result per file.
  const pending: number[] = [];
  files.forEach((filename, id) => {
    const resolved = resolveFile(compiled, filename);
    if (resolved.ignored) {
      results[id] = { filename, messages: [], ignored: true };
      done++;
    } else {
      pending.push(id);
    }
  });
  if (pending.length === 0) return results;

  try {
    await new Promise<void>((resolve, reject) => {
      const dispatch = (worker: Worker): void => {
        if (next >= pending.length) return;
        const id = pending[next++]!;
        worker.postMessage({ id, filename: files[id]! } satisfies LintRequest);
      };
      for (let i = 0; i < Math.min(workers, pending.length); i++) {
        const worker = new Worker(url, {
          workerData: { addonPath: options.addonPath, configPath: options.configPath },
        });
        pool.push(worker);
        worker.on("message", (result: LintResult) => {
          results[result.id] = {
            filename: result.filename,
            messages: result.messages,
            error: result.error,
          };
          if (++done === files.length) {
            resolve();
          } else {
            dispatch(worker);
          }
        });
        worker.on("error", reject);
        dispatch(worker);
      }
    });
  } finally {
    await Promise.all(pool.map((worker) => worker.terminate()));
  }
  return results;
}
