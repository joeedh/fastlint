// Loads the built addon and runs two TypeScript rules through the runtime, to
// prove the rule-loading path end to end (task 7.2). `smokeNapi` invokes it with
// the addon path as the one argument.

import assert from "node:assert";
import { createRequire } from "node:module";

import { lint, type Addon } from "./runtime.ts";
import { noConsole, noDebugger } from "./rules/no-debugger.ts";

const addonPath = process.argv[2];
if (!addonPath) throw new Error("usage: runtime.smoke.ts <addon path>");

const require = createRequire(import.meta.url);
const addon = require(addonPath) as Addon;

const source = ["function f() {", "  debugger;", "  console.log(1);", "}", ""].join("\n");
const messages = lint(addon, source, "smoke.ts", [noDebugger, noConsole]);

for (const m of messages) {
  console.log(`${m.line}:${m.column} ${m.ruleId} ${m.message} (${m.nodeType})`);
}

const debuggerHit = messages.find((m) => m.ruleId === "no-debugger");
assert(debuggerHit, "expected no-debugger to report");
assert.strictEqual(debuggerHit.line, 2, "debugger is on line 2");
assert.strictEqual(debuggerHit.message, "Unexpected 'debugger' statement.");
assert.strictEqual(debuggerHit.nodeType, "DebuggerStatement");

const consoleHit = messages.find((m) => m.ruleId === "no-console");
assert(consoleHit, "expected no-console to report");
assert.strictEqual(consoleHit.line, 3, "console is on line 3");

console.log(`ok: ${messages.length} problems from ${2} TypeScript rules`);
