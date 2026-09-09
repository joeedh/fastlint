// One worker of the driver's pool (task 7.2). It loads the addon and the config
// once, then lints whatever file the driver hands it and posts the problems
// back. The TypeScript rules run here, on this worker's own thread, while the
// driver keeps every worker fed; that is the whole of the parallelism.

import { parentPort, workerData } from "node:worker_threads";

import { loadCompiledConfig } from "./config.ts";
import { loadAddon, lintOne, type LintRequest, type LintResult } from "./driver.ts";

const data = workerData as { addonPath?: string; configPath: string };

// Loaded once; every file this worker lints waits on the same promises.
const ready = Promise.all([loadAddon(data.addonPath), loadCompiledConfig(data.configPath)]);

const port = parentPort;
if (!port) throw new Error("lint_worker must run as a worker thread");

port.on("message", (request: LintRequest) => {
  void (async () => {
    const [addon, compiled] = await ready;
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
