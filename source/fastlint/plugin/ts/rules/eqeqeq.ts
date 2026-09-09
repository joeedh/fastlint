// A rule ported to the runtime: `==` and `!=` should be `===` and `!==`. It
// reads the `BinaryExpression.op` enum and fills the message from `data`, so it
// exercises `{{placeholder}}` substitution.

import { BinaryOperator, NodeKind } from "../../generated/ts/views.ts";
import type { Rule } from "../runtime.ts";

export const eqeqeq: Rule = {
  name: "eqeqeq",
  messages: {
    unexpected: "Expected '{{good}}' but found '{{bad}}'.",
  },
  create(context) {
    return {
      BinaryExpression(node) {
        if (!node.is(NodeKind.BinaryExpression)) return;
        if (node.op === BinaryOperator.Equal) {
          context.report({ node, messageId: "unexpected", data: { bad: "==", good: "===" } });
        } else if (node.op === BinaryOperator.NotEqual) {
          context.report({ node, messageId: "unexpected", data: { bad: "!=", good: "!==" } });
        }
      },
    };
  },
};
