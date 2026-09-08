// A hand-written rule over the generated view surface, so `tsc` keeps the
// generated API usable as nodes.def changes. It has no runtime yet (that lands
// with the WASM/N-API build); it only exercises the types.

import { NodeKind, kindNames } from "../generated/ts/views.ts";
import type { CallExpression, MemberExpression, Node } from "../generated/ts/views.ts";

/** Every `foo.bar()` call under `root`, found with one typed descendant query. */
export function memberCalls(root: Node): CallExpression[] {
  const found: CallExpression[] = [];
  for (const call of root.descendants(NodeKind.CallExpression)) {
    // `callee` is a required child, so it is a `Node`, never null.
    const callee = call.callee;
    if (callee.is(NodeKind.MemberExpression)) {
      // `is` narrowed `callee` to the MemberExpression view.
      const member: MemberExpression = callee;
      if (!member.isComputed) {
        found.push(call);
      }
    }
  }
  return found;
}

/** A node's kind name, from the generated table. */
export function kindLabel(node: Node): string {
  return kindNames[node.type];
}
