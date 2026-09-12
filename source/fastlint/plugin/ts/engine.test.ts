// Finding the native binary (task 8.3). The package's `bin` and the executable
// share the name `lintrix`, so the PATH lookup has to step past npm's shims or
// the CLI spawns itself: an EINVAL on Windows, a fork loop elsewhere.

import assert from "node:assert";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { after, test } from "node:test";

import { loadCompiledConfig } from "./config.ts";
import { findWasmModule, isPackageShim, lintWithWasm, onPath } from "./engine.ts";

const root = fs.mkdtempSync(path.join(os.tmpdir(), "lintrix-engine-"));
const exe = process.platform === "win32" ? ".exe" : "";
const write = (relative: string, text = ""): string => {
  const full = path.join(root, relative);
  fs.mkdirSync(path.dirname(full), { recursive: true });
  fs.writeFileSync(full, text);
  return full;
};

const shimDir = path.join(root, "node_modules", ".bin");
const realDir = path.join(root, "bin");
write(`node_modules/.bin/lintrix${exe}`, "#!/usr/bin/env node\n");
write("node_modules/.bin/lintrix.cmd", "@ECHO OFF\r\n");
write("node_modules/.bin/lintrix", "#!/bin/sh\n");
const real = write(`bin/lintrix${exe}`, "MZ");

after(() => fs.rmSync(root, { recursive: true, force: true }));

/** Runs `body` with PATH set to `dirs` for its duration. */
function withPath<T>(dirs: string[], body: () => T): T {
  const saved = process.env["PATH"];
  process.env["PATH"] = dirs.join(path.delimiter);
  try {
    return body();
  } finally {
    process.env["PATH"] = saved;
  }
}

test("a file under node_modules is a shim, one elsewhere is not", () => {
  assert.ok(isPackageShim(path.join(shimDir, `lintrix${exe}`)));
  assert.ok(isPackageShim(path.join(shimDir, "lintrix")));
  assert.ok(!isPackageShim(real));
});

test("the lookup steps past node_modules/.bin to the real binary", () => {
  assert.strictEqual(withPath([shimDir, realDir], () => onPath("lintrix")), real);
});

test("a PATH holding only shims finds nothing", () => {
  assert.strictEqual(withPath([shimDir], () => onPath("lintrix")), undefined);
});

test("a directory with no such file is skipped", () => {
  assert.strictEqual(withPath([root, realDir], () => onPath("lintrix")), real);
  assert.strictEqual(withPath([root], () => onPath("lintrix")), undefined);
});

const wasm = findWasmModule();

test("--fix through WASM rewrites the file and reports what is left", {
  skip: wasm === undefined && "no WASM build",
}, async () => {
  const config = write(
    "fix/lintrix.config.json",
    '{"rules": {"no-debugger": "error", "curly": "error", "no-var": "error"}}\n'
  );
  const file = write("fix/a.ts", "var a = 1;\ndebugger;\nif (a) b();\n");
  const compiled = await loadCompiledConfig(config);

  const [report] = await lintWithWasm(wasm!, compiled, [file], true);
  const text = fs.readFileSync(file, "utf8");
  assert.ok(!text.includes("debugger"), text);
  assert.ok(text.includes("{"), text);
  const rules = report!.messages.map((message) => message.ruleId);
  assert.ok(!rules.includes("no-debugger"), rules.join(","));
  assert.ok(!rules.includes("curly"), rules.join(","));

  const [again] = await lintWithWasm(wasm!, compiled, [file], true);
  assert.strictEqual(fs.readFileSync(file, "utf8"), text);
  assert.deepStrictEqual(again!.messages, report!.messages);
});
