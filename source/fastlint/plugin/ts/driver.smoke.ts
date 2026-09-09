// Exercises the config loader and the file driver end to end (task 7.2): it
// writes a few files, lints them through the worker pool and again in-process,
// and checks each file's problems. `smokeNapi` invokes it with the addon path.

import assert from "node:assert";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { lintFiles, type FileMessages } from "./driver.ts";

const addonPath = process.argv[2];
if (!addonPath) throw new Error("usage: driver.smoke.ts <addon path>");

const configPath = fileURLToPath(new URL("./example.config.ts", import.meta.url));

const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fastlint-driver-"));
const sources: Record<string, string> = {
  "a.ts": "function a() {\n  debugger;\n}\n",
  "b.ts": "const b = () => console.log(1);\n",
  "clean.ts": "export const c = 1;\n",
  // An override turns `no-debugger` off here, and `ignores` skips the third.
  "a.test.ts": "function t() {\n  debugger;\n}\n",
  "generated/g.ts": "function g() {\n  debugger;\n}\n",
};
const files = Object.keys(sources).map((name) => {
  const file = path.join(dir, name);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, sources[name]!);
  return file;
});

function check(label: string, results: FileMessages[]): void {
  assert.strictEqual(results.length, files.length, `${label}: one result per file`);
  const byName = new Map(results.map((r) => [path.basename(r.filename), r]));
  for (const r of results) assert(!r.error, `${label}: ${r.filename} ${r.error ?? ""}`);

  const a = byName.get("a.ts")!;
  const debuggerHit = a.messages.find((m) => m.ruleId === "example/no-debugger");
  assert(debuggerHit, `${label}: a.ts should trip example/no-debugger`);
  assert.strictEqual(debuggerHit.severity, 2, `${label}: it is configured at error`);
  const b = byName.get("b.ts")!;
  const consoleHit = b.messages.find((m) => m.ruleId === "example/no-console");
  assert(consoleHit, `${label}: b.ts should trip example/no-console`);
  assert.strictEqual(consoleHit.severity, 1, `${label}: it is configured at warn`);
  const clean = byName.get("clean.ts")!;
  assert.strictEqual(clean.messages.length, 0, `${label}: clean.ts should be clean`);

  const test = byName.get("a.test.ts")!;
  assert.strictEqual(
    test.messages.length,
    0,
    `${label}: the override turns no-debugger off for a test file`
  );
  const generated = byName.get("g.ts")!;
  assert(generated.ignored, `${label}: an ignored file is not linted`);
}

try {
  check("workers", await lintFiles(files, { configPath, addonPath, concurrency: 2 }));
  check("in-process", await lintFiles(files, { configPath, addonPath, concurrency: 1 }));
  console.log(`ok: ${files.length} files linted from a config, in a pool and in-process`);
} finally {
  fs.rmSync(dir, { recursive: true, force: true });
}
