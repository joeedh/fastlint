// Runs the TypeScript rules over the WASM heap (task 7.3), proving the same
// runtime works against the Emscripten module as against the N-API addon.
// `smokeWasm` invokes it with the built `fastlint.js` path.

import assert from "node:assert";

import { lint, resolveRule } from "./runtime.ts";
import { noConsole, noDebugger } from "./rules/no-debugger.ts";
import { loadWasmAddon } from "./wasm_addon.ts";

const modulePath = process.argv[2];
if (!modulePath) throw new Error("usage: wasm.smoke.ts <fastlint.js path>");

const addon = await loadWasmAddon(modulePath);

const source = ["function f() {", "  debugger;", "  console.log(1);", "}", ""].join("\n");
const messages = lint(addon, source, "smoke.ts", [noDebugger, noConsole].map((rule) =>
  resolveRule(rule)
));

for (const m of messages) {
  console.log(`${m.line}:${m.column} ${m.ruleId} ${m.message} (${m.nodeType})`);
}

const debuggerHit = messages.find((m) => m.ruleId === "no-debugger");
assert(debuggerHit, "expected no-debugger to report");
assert.strictEqual(debuggerHit.line, 2, "debugger is on line 2");
assert.strictEqual(debuggerHit.nodeType, "DebuggerStatement");

const consoleHit = messages.find((m) => m.ruleId === "no-console");
assert(consoleHit, "expected no-console to report");
assert.strictEqual(consoleHit.line, 3, "console is on line 3");

console.log(`ok: ${messages.length} problems from 2 TypeScript rules over WASM`);
