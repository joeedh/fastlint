// The `loadAddon` export on the package surface, including the pattern
// docs/plugins.md gives for a rule that parses a second file mid-visit.

import assert from "node:assert";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import { NodeKind, lint, loadAddon, resolveRule, type Rule } from "./index.ts";
import { findWasmModule } from "./engine.ts";
import { noDebugger } from "./rules/no-debugger.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const napiAddon = path.join(
  here,
  "..",
  "..",
  "..",
  "..",
  "build",
  "napi",
  "fastlint.node"
);
const noWasm = findWasmModule() === undefined && "no WASM build";
const noNapi = !fs.existsSync(napiAddon) && "no N-API build";

const source = "function f() {\n  debugger;\n}\n";

test("with no path it loads the bundled WASM build", { skip: noWasm }, async () => {
  const addon = await loadAddon();
  const messages = lint(addon, source, "a.ts", [resolveRule(noDebugger)]);
  assert.deepStrictEqual(
    messages.map((m) => [m.ruleId, m.line]),
    [["no-debugger", 2]]
  );
});

test("a .node path loads the N-API addon", { skip: noNapi }, async () => {
  const addon = await loadAddon(napiAddon);
  const messages = lint(addon, source, "a.ts", [resolveRule(noDebugger)]);
  assert.deepStrictEqual(
    messages.map((m) => [m.ruleId, m.line]),
    [["no-debugger", 2]]
  );
});

test("each call loads a separate instance", { skip: noWasm }, async () => {
  assert.notStrictEqual(await loadAddon(), await loadAddon());
});

test("a rule can lint a second buffer while it visits", { skip: noWasm }, async () => {
  const addon = await loadAddon();
  const other = "export function g() {}\nexport function h() {}\n";

  // Collects the names `other` declares, the way a helper rule would
  const declared: string[] = [];
  const collect: Rule = {
    name  : "collect",
    create: () => ({
      FunctionDeclaration(node) {
        if (!node.is(NodeKind.FunctionDeclaration) || !node.id) return;
        declared.push(node.id.text);
      },
    }),
  };

  // Reports each call whose callee `other` does not declare
  const unknownCall: Rule = {
    name: "unknown-call",
    create(context) {
      return {
        CallExpression(node) {
          if (declared.length === 0)
            lint(addon, other, "other.ts", [resolveRule(collect)]);
          if (!node.is(NodeKind.CallExpression)) return;
          const callee = node.callee;
          if (callee.is(NodeKind.Identifier) && !declared.includes(callee.text)) {
            context.report({ node, message: `unknown ${callee.text}` });
          }
        },
      };
    },
  };

  const messages = lint(addon, "g();\nk();\nh();\n", "main.ts", [
    resolveRule(unknownCall),
  ]);
  assert.deepStrictEqual(declared, ["g", "h"]);
  assert.deepStrictEqual(
    messages.map((m) => [m.message, m.line, m.nodeType]),
    [["unknown k", 2, "CallExpression"]]
  );
});
