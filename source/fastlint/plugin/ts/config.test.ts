// The config loader and compiler (task 8.2): what a config has to look like,
// what a plugin specifier resolves to, what one file is linted with, and what
// the native binary is handed. The layering cases mirror lint_config_test.cc, so
// a rule's severity does not depend on which side read the config.

import assert from "node:assert";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import {
  compileConfig,
  nativeConfig,
  pluginPrefixOf,
  resolveFile,
} from "./compile.ts";
import { loadCompiledConfig, loadConfigFile } from "./config.ts";
import type { FastlintConfigFile } from "./schema.ts";
import { validateConfigFile } from "./validate.ts";

const pluginDir = path.dirname(fileURLToPath(import.meta.url));

/** A directory with the files `contents` names, removed when `body` returns. */
async function inTempDir(
  contents: Record<string, string>,
  body: (dir: string) => Promise<void>
): Promise<void> {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "lintrix-config-"));
  try {
    for (const [name, text] of Object.entries(contents)) {
      const file = path.join(dir, name);
      fs.mkdirSync(path.dirname(file), { recursive: true });
      fs.writeFileSync(file, text);
    }
    await body(dir);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

test("a valid config reports no problems", () => {
  assert.deepStrictEqual(
    validateConfigFile({
      extends: "lintrix:recommended",
      plugins: { acme: "@acme/rules" },
      rules: { "no-debugger": "error", "acme/no-foo": ["warn", { depth: 2 }] },
      overrides: [{ files: "**/*.test.ts", rules: { "no-debugger": "off" } }],
      ignores: ["dist/**"],
      project: "tsconfig.json",
      projects: [{ files: "web/**", project: "web/tsconfig.json" }],
      reportUnusedDisableDirectives: 1,
      eslintDirectives: false,
      binary: "./node_modules/.bin/lintrix",
    }),
    []
  );
  // A key neither side knows is left alone, as the native parser leaves it.
  assert.deepStrictEqual(validateConfigFile({ future: 1 }), []);
});

test("validation names each problem the way the native parser does", () => {
  assert.deepStrictEqual(validateConfigFile([]), [
    "config: the top level must be an object",
  ]);
  assert.deepStrictEqual(validateConfigFile({ extends: "eslint:recommended" }), [
    'config: unknown preset "eslint:recommended"',
  ]);
  assert.deepStrictEqual(validateConfigFile({ rules: { "no-debugger": "loud" } }), [
    'config: rule "no-debugger" needs a severity ("off", "warn", "error" or 0-2)',
  ]);
  assert.deepStrictEqual(validateConfigFile({ plugins: { acme: 1 } }), [
    'config: plugin "acme" must name a module specifier string',
  ]);
  assert.deepStrictEqual(
    validateConfigFile({ plugins: { "@typescript-eslint": "./x.ts" } }),
    ['config: plugin prefix "@typescript-eslint" is reserved for the built-in rules']
  );
  assert.deepStrictEqual(validateConfigFile({ overrides: [{ rules: {} }] }), [
    'config: overrides[0] "files" must be a glob or a list of globs',
  ]);
  assert.deepStrictEqual(validateConfigFile({ binary: true }), [
    'config: "binary" must be a path to the lintrix executable',
  ]);
});

test("a prefix is everything before the last slash", () => {
  const prefixes = ["acme", "@scope/pack"];
  assert.strictEqual(pluginPrefixOf("acme/no-foo", prefixes), "acme");
  assert.strictEqual(pluginPrefixOf("@scope/pack/no-bar", prefixes), "@scope/pack");
  assert.strictEqual(pluginPrefixOf("acmee/no-foo", prefixes), undefined);
  assert.strictEqual(pluginPrefixOf("no-debugger", prefixes), undefined);
});

test("a .json config is read and validated", async () => {
  await inTempDir(
    {
      "lintrix.config.json": '{"rules": {"no-debugger": "error"}}',
      "bad.config.json": '{"rules": {"no-debugger": "loud"}}',
    },
    async (dir) => {
      const file = await loadConfigFile(path.join(dir, "lintrix.config.json"));
      assert.deepStrictEqual(file.rules, { "no-debugger": "error" });
      await assert.rejects(
        () => loadConfigFile(path.join(dir, "bad.config.json")),
        /needs a severity/
      );
    }
  );
});

test("a .ts config is imported and its plugins are bound", async () => {
  const compiled = await loadCompiledConfig(path.join(pluginDir, "example.config.ts"));
  assert.deepStrictEqual(compiled.unknownRules, []);
  assert.ok(compiled.rules.has("example/no-debugger"));
  assert.strictEqual(compiled.rules.get("example/no-debugger")?.name, "no-debugger");
});

test("a rule its plugin does not define is collected, not thrown", async () => {
  const file: FastlintConfigFile = {
    plugins: { example: "./rules/index.ts" },
    rules: { "example/no-such-rule": "error", "example/no-var": "error" },
  };
  const compiled = await compileConfig(file, pluginDir);
  assert.deepStrictEqual(compiled.unknownRules, ["example/no-such-rule"]);
  assert.deepStrictEqual(
    resolveFile(compiled, path.join(pluginDir, "a.ts")).rules.map((r) => r.id),
    ["example/no-var"]
  );
});

test("overrides and ignores layer as they do natively", async () => {
  const file: FastlintConfigFile = {
    plugins: { example: "./rules/index.ts" },
    rules: { "example/eqeqeq": ["warn", "always"], "example/no-var": "error" },
    overrides: [
      { files: ["**/*.test.ts"], rules: { "example/eqeqeq": "error" } },
      { files: "legacy/**", rules: { "example/no-var": "off" } },
    ],
    ignores: ["dist/**"],
  };
  const compiled = await compileConfig(file, pluginDir);
  const severities = (name: string): Record<string, string> =>
    Object.fromEntries(
      resolveFile(compiled, path.join(pluginDir, name)).rules.map((r) => [
        r.id,
        r.severity,
      ])
    );

  assert.deepStrictEqual(severities("src/a.ts"), {
    "example/eqeqeq": "warn",
    "example/no-var": "error",
  });
  assert.deepStrictEqual(severities("src/a.test.ts"), {
    "example/eqeqeq": "error",
    "example/no-var": "error",
  });
  assert.strictEqual(severities("legacy/b.ts")["example/no-var"], "off");

  // A bare severity in an override keeps the options the base layer gave.
  const raised = resolveFile(compiled, path.join(pluginDir, "src/a.test.ts")).rules.find(
    (r) => r.id === "example/eqeqeq"
  );
  assert.deepStrictEqual(raised?.options, ["always"]);

  assert.ok(resolveFile(compiled, path.join(pluginDir, "dist/c.ts")).ignored);
  assert.ok(!resolveFile(compiled, path.join(pluginDir, "src/c.ts")).ignored);
});

test("the native document drops the plugin rules and the keys only this side reads", async () => {
  const file: FastlintConfigFile = {
    $schema: "./schema.json",
    extends: "lintrix:recommended",
    plugins: { example: "./rules/index.ts" },
    rules: { "no-debugger": "error", "example/no-var": "error", "typo/no-foo": "warn" },
    overrides: [
      {
        files: "src/**",
        rules: { eqeqeq: "error", "example/no-empty": "off" },
      },
    ],
    ignores: ["dist/**"],
    binary: "./lintrix",
  };
  const compiled = await compileConfig(file, pluginDir);
  assert.deepStrictEqual(nativeConfig(compiled), {
    extends: "lintrix:recommended",
    // A name under an undeclared prefix stays, so the native binary still warns.
    rules: { "no-debugger": "error", "typo/no-foo": "warn" },
    overrides: [{ files: "src/**", rules: { eqeqeq: "error" } }],
    ignores: ["dist/**"],
  });
});
