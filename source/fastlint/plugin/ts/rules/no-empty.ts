// A rule ported to the runtime: an empty block is a problem. It reads a list
// child (`BlockStatement.body`) and skips a block that carries a comment, the
// way the built-in `no-empty` does, so an intentionally empty block can say so.

import { NodeKind } from "../../generated/ts/views.ts";
import type { Rule } from "../runtime.ts";

export const noEmpty: Rule = {
  name: "no-empty",
  messages: {
    unexpected: "Empty block statement.",
  },
  create(context) {
    return {
      BlockStatement(node) {
        if (!node.is(NodeKind.BlockStatement)) return;
        if (node.body.length > 0) return;
        // A comment between the braces makes the emptiness deliberate.
        const [start, end] = node.range;
        if (/\/\/|\/\*/.test(context.sourceText.slice(start, end))) return;
        context.report({ node, messageId: "unexpected" });
      },
    };
  },
};
