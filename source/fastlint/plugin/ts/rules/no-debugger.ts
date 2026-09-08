// A rule authored in TypeScript, run through the N-API runtime (task 7.2). It
// mirrors the built-in `no-debugger`: a `debugger;` statement is a problem. The
// point is that a rule reads the generated view surface and reports through
// `context.report`, with no C++ of its own.

import { NodeKind } from "../../generated/ts/views.ts";
import type { Rule } from "../runtime.ts";

export const noDebugger: Rule = {
  name: "no-debugger",
  messages: {
    unexpected: "Unexpected 'debugger' statement.",
  },
  create(context) {
    return {
      DebuggerStatement(node) {
        context.report({ node, messageId: "unexpected" });
      },
    };
  },
};

// A second rule over a typed descendant query, so the smoke exercises `is`
// narrowing and a required-child read as well as a plain visitor.
export const noConsole: Rule = {
  name: "no-console",
  messages: {
    unexpected: "Unexpected console statement.",
  },
  create(context) {
    return {
      MemberExpression(node) {
        if (!node.is(NodeKind.MemberExpression)) return;
        const object = node.object;
        if (object.is(NodeKind.Identifier) && object.text === "console") {
          context.report({ node, messageId: "unexpected" });
        }
      },
    };
  },
};
