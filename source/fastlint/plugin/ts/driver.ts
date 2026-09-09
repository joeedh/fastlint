// Lints a set of files with a config's TypeScript rules (task 7.2). This is the
// "node CLI hosts the native core" half of the design: the JS side owns the file
// list and the config, and the addon does the parse and the accessors. Files are
// sharded across a worker pool so the rules run in parallel, one file per worker
// at a time; a single file or `concurrency: 1` stays in this thread.

import fs from "node:fs";
import { createRequire } from "node:module";
import os from "node:os";
import { Worker } from "node:worker_threads";

import { loadConfig } from "./config.ts";
import { lint, type Addon, type LintMessage } from "./runtime.ts";

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
}

export interface LintFilesOptions {
  /** The `fastlint.config.*` module to load the rules from. */
  configPath: string;
  /** The built `.node` addon to parse through. */
  addonPath: string;
  /** Worker count; defaults to the CPU count, capped at the file count. `1`
   * runs in this thread with no workers. */
  concurrency?: number;
}

/** Lints `files` in this thread, without spawning a worker. */
async function lintInProcess(
  files: readonly string[],
  options: LintFilesOptions
): Promise<FileMessages[]> {
  const require = createRequire(import.meta.url);
  const addon = require(options.addonPath) as Addon;
  const config = await loadConfig(options.configPath);
  return files.map((filename) => {
    try {
      const source = fs.readFileSync(filename, "utf8");
      return { filename, messages: lint(addon, source, filename, config.rules) };
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      return { filename, messages: [], error: message };
    }
  });
}

/**
 * Lints `files` and returns one entry per file, in the input order. With more
 * than one file and a concurrency above one, a worker pool does the work: each
 * worker loads the addon and the config once, and the driver hands it the next
 * file as soon as it returns the last, so a slow file never idles the others.
 */
export async function lintFiles(
  files: readonly string[],
  options: LintFilesOptions
): Promise<FileMessages[]> {
  const workers = Math.max(
    1,
    Math.min(options.concurrency ?? os.cpus().length, files.length)
  );
  if (workers <= 1 || files.length <= 1) {
    return lintInProcess(files, options);
  }

  const results: FileMessages[] = new Array(files.length);
  const url = new URL("./lint_worker.ts", import.meta.url);
  const pool: Worker[] = [];
  let next = 0;
  let done = 0;

  try {
    await new Promise<void>((resolve, reject) => {
      const dispatch = (worker: Worker): void => {
        if (next >= files.length) return;
        const id = next++;
        worker.postMessage({ id, filename: files[id]! } satisfies LintRequest);
      };
      for (let i = 0; i < workers; i++) {
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
