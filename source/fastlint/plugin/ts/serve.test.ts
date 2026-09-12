// The serve client (task 9.5) against a built `lintrix`, which the C++ tests
// exercise more thoroughly; this covers the framing and the request queue
// from the Node side, and skips when no preset has been built.

import assert from "node:assert";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { test } from "node:test";

import { ServeClient } from "./serve.ts";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../../..");
const exe = process.platform === "win32" ? "lintrix.exe" : "lintrix";
const binary = ["release", "relwithdebinfo", "debug"]
  .map((preset) => path.join(repoRoot, "build", preset, "bin", exe))
  .find((candidate) => fs.existsSync(candidate));

test("lints buffers over a resident binary", { skip: binary === undefined }, async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "lintrix-serve-"));
  const file = path.join(dir, "a.ts");
  fs.writeFileSync(file, "debugger;\n");
  const stderr: string[] = [];
  const client = new ServeClient(binary!, {
    cwd: repoRoot,
    noCache: true,
    onStderr: (line) => stderr.push(line),
  });
  try {
    // Requests are answered in order, so two in flight come back to the right
    // callers.
    const [saved, overlay] = await Promise.all([
      client.lint(file),
      client.lint(file, "export const a = 1;\n"),
    ]);
    assert.deepStrictEqual(
      saved.results[0]!.messages.map((m) => m.ruleId),
      ["no-debugger"]
    );
    assert.strictEqual(typeof saved.typed, "boolean");
    assert.deepStrictEqual(overlay.results[0]!.messages, []);

    const untitled = await client.lint(path.join(dir, "untitled.ts"), "debugger;\n");
    assert.strictEqual(untitled.results[0]!.messages[0]!.ruleId, "no-debugger");

    await client.close(file);
    await client.changed([file]);
    await client.configChanged();
    await assert.rejects(client.request("nope", {}), /unknown method/);

    await client.shutdown();
    assert.strictEqual(await client.exited, 0);
    assert.ok(!client.alive);
    await assert.rejects(client.lint(file), /exited/);
  } finally {
    client.dispose();
    fs.rmSync(dir, { recursive: true, force: true, maxRetries: 5 });
  }
});
