// Measures the TypeScript rule overhead (task 7.4) against the shared C++ front
// end, over the same synthetic source, best of a few repeats:
//   - parse: addon.parse alone, the parse/lower/bind a native rule also pays.
//   - ts:    lint() with the example rules; `ts - parse` is the JS walk and the
//            per-node accessor crossings the runtime adds on top.
// A native rule runs inside the parse pass, so its marginal cost is a dispatch
// map lookup per node, nanoseconds; the TS figure is what a rule author trades
// for authoring in TypeScript. Run as `node bench.ts <addon path> [repeats]`.

import { createRequire } from "node:module";

import { lint, resolveRule, type Addon } from "./runtime.ts";
import { loadWasmAddon } from "./wasm_addon.ts";
import { eqeqeq } from "./rules/eqeqeq.ts";
import { noConsole, noDebugger } from "./rules/no-debugger.ts";
import { noEmpty } from "./rules/no-empty.ts";
import { noVar } from "./rules/no-var.ts";

const addonPath = process.argv[2];
if (!addonPath) throw new Error("usage: bench.ts <addon path (.node or .js)> [repeats]");
const repeats = Number(process.argv[3] ?? 5);

// A `.js` path is the WASM module, loaded async; a `.node` path is the addon.
const addon: Addon = addonPath.endsWith(".js")
  ? await loadWasmAddon(addonPath)
  : (createRequire(import.meta.url)(addonPath) as Addon);

// A source with a mix of the kinds the example rules visit, sized so per-call
// overhead is not what dominates.
const block = `
function work(a, b) {
  var total = 0;
  for (var i = 0; i < a.length; i++) {
    if (a[i] == b) {
      debugger;
      console.log(a[i]);
    } else {
    }
    total = total + a[i];
  }
  return total != 0 ? total : b;
}
`;
const source = block.repeat(400);
const bytes = Buffer.byteLength(source, "utf8");

const rules = [noDebugger, noConsole, noVar, eqeqeq, noEmpty].map((rule) =>
  resolveRule(rule)
);
const justDebugger = [resolveRule(noDebugger)];

function countNodes(): number {
  const session = addon.parse(source, "bench.ts");
  let n = 0;
  const stack: unknown[] = [addon.root(session)];
  while (stack.length > 0) {
    const handle = stack.pop();
    n++;
    const count = addon.childCount(handle);
    for (let i = 0; i < count; i++) {
      const child = addon.child(handle, i);
      if (child !== null) stack.push(child);
    }
  }
  addon.freeSession?.(session);
  return n;
}

/** Best (lowest) wall time in ms over `repeats` runs of `fn`. */
function best(fn: () => void): number {
  let ms = Infinity;
  for (let i = 0; i < repeats; i++) {
    const start = process.hrtime.bigint();
    fn();
    const took = Number(process.hrtime.bigint() - start) / 1e6;
    if (took < ms) ms = took;
  }
  return ms;
}

const nodes = countNodes();

const parseOnly = best(() => {
  const session = addon.parse(source, "bench.ts");
  addon.freeSession?.(session);
});
const ts = best(() => {
  lint(addon, source, "bench.ts", rules);
});
const one = best(() => {
  lint(addon, source, "bench.ts", justDebugger);
});

// The one-rule run visits almost nothing, so its cost over parse is the bare
// traversal: wrap a node, read its kind and children across the boundary. The
// five-rule run adds each visitor's own accessor reads on the nodes it matches.
const walk = one - parseOnly;

console.log(`source ${(bytes / 1024) | 0} KB, ${nodes} nodes, best of ${repeats}`);
console.log(`parse only           ${parseOnly.toFixed(1)} ms`);
console.log(
  `bare walk            ${walk.toFixed(1)} ms  (${((walk * 1e6) / nodes).toFixed(0)} ns/node)`
);
console.log(`ts, 1 rule           ${one.toFixed(1)} ms`);
console.log(`ts, ${rules.length} rules         ${ts.toFixed(1)} ms`);
console.log(`ts, 5 rules vs parse ${(ts / parseOnly).toFixed(0)}x the front end`);
