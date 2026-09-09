// One worker of the driver's pool (task 7.2). It loads the addon and the config
// once, then lints whatever file the driver hands it and posts the problems
// back. The TypeScript rules run here, on this worker's own thread, while the
// driver keeps every worker fed; that is the whole of the parallelism.

import fs from "node:fs";
import { createRequire } from "node:module";
import { parentPort, workerData } from "node:worker_threads";

import { loadConfig } from "./config.ts";
import { lint, type Addon } from "./runtime.ts";
import type { LintRequest, LintResult } from "./driver.ts";

const data = workerData as { addonPath: string; configPath: string };
const require = createRequire(import.meta.url);
const addon = require(data.addonPath) as Addon;

// Loaded once; every file this worker lints waits on the same promise.
const ready = loadConfig(data.configPath);

const port = parentPort;
if (!port) throw new Error("lint_worker must run as a worker thread");

port.on("message", (request: LintRequest) => {
  void (async () => {
    const config = await ready;
    let messages;
    try {
      const source = fs.readFileSync(request.filename, "utf8");
      messages = lint(addon, source, request.filename, config.rules);
    } catch (error) {
      const result: LintResult = {
        id: request.id,
        filename: request.filename,
        messages: [],
        error: error instanceof Error ? error.message : String(error),
      };
      port.postMessage(result);
      return;
    }
    const result: LintResult = { id: request.id, filename: request.filename, messages };
    port.postMessage(result);
  })();
});
