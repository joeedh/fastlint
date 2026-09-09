// One worker of the driver's pool (task 7.2). It loads the addon and the config
// once, then lints whatever file the driver hands it and posts the problems
// back. The TypeScript rules run here, on this worker's own thread, while the
// driver keeps every worker fed; that is the whole of the parallelism.

import { createRequire } from "node:module";
import { parentPort, workerData } from "node:worker_threads";

import { loadCompiledConfig } from "./config.ts";
import { lintOne, type LintRequest, type LintResult } from "./driver.ts";
import type { Addon } from "./runtime.ts";

const data = workerData as { addonPath: string; configPath: string };
const require = createRequire(import.meta.url);
const addon = require(data.addonPath) as Addon;

// Loaded once; every file this worker lints waits on the same promise.
const ready = loadCompiledConfig(data.configPath);

const port = parentPort;
if (!port) throw new Error("lint_worker must run as a worker thread");

port.on("message", (request: LintRequest) => {
  void (async () => {
    const compiled = await ready;
    const file = lintOne(addon, compiled, request.filename);
    const result: LintResult = {
      id: request.id,
      filename: request.filename,
      messages: file.messages,
      error: file.error,
    };
    port.postMessage(result);
  })();
});
