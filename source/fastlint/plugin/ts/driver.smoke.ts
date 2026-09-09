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
};
const files = Object.keys(sources).map((name) => {
  const file = path.join(dir, name);
  fs.writeFileSync(file, sources[name]!);
  return file;
});

function check(label: string, results: FileMessages[]): void {
  assert.strictEqual(results.length, files.length, `${label}: one result per file`);
  const byName = new Map(results.map((r) => [path.basename(r.filename), r]));
  for (const r of results) assert(!r.error, `${label}: ${r.filename} ${r.error ?? ""}`);

  const a = byName.get("a.ts")!;
  assert(
    a.messages.some((m) => m.ruleId === "no-debugger"),
    `${label}: a.ts should trip no-debugger`
  );
  const b = byName.get("b.ts")!;
  assert(
    b.messages.some((m) => m.ruleId === "no-console"),
    `${label}: b.ts should trip no-console`
  );
  const clean = byName.get("clean.ts")!;
  assert.strictEqual(clean.messages.length, 0, `${label}: clean.ts should be clean`);
}

try {
  check("workers", await lintFiles(files, { configPath, addonPath, concurrency: 2 }));
  check("in-process", await lintFiles(files, { configPath, addonPath, concurrency: 1 }));
  console.log(`ok: ${files.length} files linted from a config, in a pool and in-process`);
} finally {
  fs.rmSync(dir, { recursive: true, force: true });
}
