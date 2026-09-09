// Collecting a run's files (task 8.3). Both engines lint the list this builds,
// so what it walks past decides what a bare `fastlint .` covers.

import assert from "node:assert";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { after, test } from "node:test";

import { collectFiles, sourceExtensions } from "./files.ts";

const root = fs.mkdtempSync(path.join(os.tmpdir(), "fastlint-files-"));
const write = (relative: string, text = ""): string => {
  const full = path.join(root, relative);
  fs.mkdirSync(path.dirname(full), { recursive: true });
  fs.writeFileSync(full, text);
  return full.replace(/\\/g, "/");
};

const a = write("src/a.ts");
const b = write("src/nested/b.tsx");
const mts = write("src/c.mts");
const js = write("src/d.js");
write("src/readme.md");
write("node_modules/pkg/index.ts");
write(".cache/state.ts");

after(() => fs.rmSync(root, { recursive: true, force: true }));

test("a directory walk takes the source extensions and nothing else", () => {
  const files = collectFiles([root]);
  assert.deepStrictEqual(files.toSorted(), [a, mts, b].toSorted());
  assert.ok(sourceExtensions.includes(".tsx"));
});

test("a dependency tree and a dot directory are walked past", () => {
  const files = collectFiles([root]);
  assert.ok(!files.some((file) => file.includes("node_modules")));
  assert.ok(!files.some((file) => file.includes(".cache")));
});

test("a file named outright is linted whatever it is called", () => {
  assert.deepStrictEqual(collectFiles([js]), [js]);
});

test("a path that does not exist is kept, for the read to report", () => {
  const missing = path.join(root, "gone.ts").replace(/\\/g, "/");
  assert.deepStrictEqual(collectFiles([missing]), [missing]);
});

test("naming a file twice lints it once", () => {
  assert.deepStrictEqual(collectFiles([a, root, a]).filter((f) => f === a), [a]);
});

test("the list is absolute paths with forward slashes", () => {
  const files = collectFiles([path.relative(process.cwd(), root)]);
  for (const file of files) {
    assert.ok(path.isAbsolute(file), file);
    assert.ok(!file.includes("\\"), file);
  }
});
