// Structural comparison of two normalized trees. Reports the first
// disagreement only; one file, one finding, so the aggregate counts files.

import type { TreeNode } from "./sexp.ts";

export interface Mismatch {
  /** Ancestor kinds from the root down to the disagreeing parent. */
  path: string[];
  /** Short, file-independent description used to bucket mismatches. */
  signature: string;
  detail: string;
  offset: number;
}

export interface CompareOptions {
  spans: boolean;
}

export function compare(
  expected: TreeNode,
  actual: TreeNode,
  options: CompareOptions
): Mismatch | undefined {
  return walk(expected, actual, [], options);
}

function walk(
  expected: TreeNode,
  actual: TreeNode,
  path: string[],
  options: CompareOptions
): Mismatch | undefined {
  if (expected.kind !== actual.kind) {
    return {
      path,
      signature: `${parent(path)} > ${expected.kind} != ${actual.kind}`,
      detail   : `expected ${expected.kind}, got ${actual.kind}`,
      offset   : expected.start,
    };
  }
  if (options.spans && (expected.start !== actual.start || expected.end !== actual.end)) {
    return {
      path,
      signature: `${expected.kind} span`,
      detail: `expected [${expected.start},${expected.end}), got [${actual.start},${actual.end})`,
      offset   : expected.start,
    };
  }
  const here = [...path, expected.kind];
  const n = Math.min(expected.children.length, actual.children.length);
  for (let i = 0; i < n; i++) {
    const e = expected.children[i];
    const a = actual.children[i];
    if (e.kind !== a.kind) {
      return {
        path     : here,
        signature: `${expected.kind} > ${e.kind} != ${a.kind}`,
        detail: `child ${i}: expected ${e.kind} [${kinds(expected)}], got ${a.kind} [${kinds(actual)}]`,
        offset   : e.start,
      };
    }
  }
  if (expected.children.length !== actual.children.length) {
    const longer = expected.children.length > actual.children.length ? expected : actual;
    const extra = longer.children[n];
    const which = longer === expected ? "missing" : "extra";
    return {
      path     : here,
      signature: `${expected.kind} ${which} ${extra.kind}`,
      detail: `${which} ${extra.kind}: expected [${kinds(expected)}], got [${kinds(actual)}]`,
      offset   : extra.start,
    };
  }
  for (let i = 0; i < n; i++) {
    const found = walk(expected.children[i], actual.children[i], here, options);
    if (found) return found;
  }
  return undefined;
}

function parent(path: string[]): string {
  return path.length > 0 ? path[path.length - 1] : "(root)";
}

function kinds(node: TreeNode): string {
  const list = node.children.map((c) => c.kind);
  return list.length > 8 ? list.slice(0, 8).join(" ") + " …" : list.join(" ");
}
