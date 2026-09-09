// A rule ported to the runtime: `var` is a problem, use `let` or `const`. It
// reads the `VariableDeclaration.kind` enum off the view surface. The runtime
// has no fixer yet, so this reports where the built-in `no-var` also fixes.

import { NodeKind, VariableKind } from "../../generated/ts/views.ts";
import type { Rule } from "../runtime.ts";

export const noVar: Rule = {
  name: "no-var",
  messages: {
    unexpected: "Unexpected var, use let or const instead.",
  },
  create(context) {
    return {
      VariableDeclaration(node) {
        if (!node.is(NodeKind.VariableDeclaration)) return;
        if (node.kind === VariableKind.Var) {
          context.report({ node, messageId: "unexpected" });
        }
      },
    };
  },
};
