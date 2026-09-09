// The glob cases lint_config_test.cc runs, over the TypeScript port. Both sides
// match the same `files` and `ignores` patterns, so a case that changes here has
// to change there.

import assert from "node:assert";
import { test } from "node:test";

import { globMatch, relativePath } from "./glob.ts";

test("matches segments, stars and braces", () => {
  assert.ok(globMatch("**/*.ts", "src/a.ts"));
  assert.ok(globMatch("**/*.ts", "a.ts"));
  assert.ok(!globMatch("**/*.ts", "src/a.tsx"));
  assert.ok(globMatch("src/**", "src/deep/er/a.ts"));
  assert.ok(globMatch("src/**/*.test.ts", "src/a.test.ts"));
  assert.ok(globMatch("src/**/*.test.ts", "src/x/y/a.test.ts"));
  assert.ok(!globMatch("src/*.ts", "src/x/a.ts"));
  assert.ok(globMatch("src/*.{ts,tsx}", "src/a.tsx"));
  assert.ok(!globMatch("src/*.{ts,tsx}", "src/a.js"));
  assert.ok(globMatch("a?c", "abc"));
  assert.ok(!globMatch("a?c", "a/c"));
  assert.ok(globMatch("*.ts", ".hidden.ts"));
  assert.ok(globMatch("./src/*.ts", "src/a.ts"));
  assert.ok(globMatch("dist/**", "dist"));
  assert.ok(!globMatch("dist/**", "distx/a"));
});

test("case insensitivity matches regardless of case", () => {
  assert.ok(!globMatch("src/**/*.ts", "Src/App/Main.TS"));
  assert.ok(!globMatch("src/*.{ts,tsx}", "src/A.TSX"));
  assert.ok(globMatch("src/**/*.ts", "Src/App/Main.TS", true));
  assert.ok(globMatch("src/*.{ts,tsx}", "src/A.TSX", true));
  assert.ok(!globMatch("src/*.ts", "src/a.js", true));
});

test("a path is relative to the config directory, with forward slashes", () => {
  assert.strictEqual(relativePath("C:/project", "C:/project/src/a.ts"), "src/a.ts");
  assert.strictEqual(relativePath("C:/project", "C:\\project\\src\\a.ts"), "src/a.ts");
  assert.strictEqual(relativePath("C:/project/", "C:/project/a.ts"), "a.ts");
  // A file outside the config's directory keeps the path it came with.
  assert.strictEqual(relativePath("C:/project", "C:/other/a.ts"), "C:/other/a.ts");
  assert.strictEqual(relativePath("", "src/a.ts"), "src/a.ts");
});
