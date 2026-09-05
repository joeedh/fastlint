#include "fastlint/syntax/parser.h"
#include <algorithm>

#include "fastlint/syntax/parser/internal.h"

namespace fastlint::syntax {

using litestl::util::string;

using detail::binaryPrecedence;
using detail::isAlwaysIdentifier;

// ----------------------------------------------------------------- debug dump

namespace {

struct TreeWriter {
  GrammarTree &tree;
  std::string &out;
  bool spans = false;

  void text(std::string_view t)
  {
    out.append(t);
  }

  // Indentation stops growing past this depth so a long left-associative
  // chain does not make the dump quadratic in size.
  static constexpr int maxIndent = 64;

  void indent(int depth)
  {
    for (int i = 0; i < std::min(depth, maxIndent); ++i) {
      text("  ");
    }
  }

  struct Frame {
    NodeId id;
    uint32_t next; // index of the next child to open
    int depth;
  };

  // Iterative so a deep tree cannot overflow the stack.
  void walk(NodeId root)
  {
    litestl::util::Vector<Frame, 64> stack;
    open(root, 0);
    stack.append({root, 0, 0});
    while (stack.size() > 0) {
      Frame &top = stack[int(stack.size()) - 1];
      auto children = tree.children(top.id);
      if (top.next < uint32_t(children.size())) {
        NodeId child = children[top.next++];
        int depth = top.depth + 1;
        open(child, depth);
        stack.append({child, 0, depth});
        continue;
      }
      indent(top.depth);
      text(")\n");
      stack.pop_back();
    }
  }

  /** Prints a node's opening line: kind, span, flags and unowned tokens. */
  void open(NodeId id, int depth)
  {
    if (id == kNoNode) {
      fprintf(stderr, "Invalid node reference!\n");
      return;
    }
    const Node &node = tree.nodes()[id];
    indent(depth);
    text("(");
    text(nodeKindName(node.kind));
    if (spans) {
      uint32_t start = 0;
      uint32_t end = 0;
      if (node.tokenCount > 0) {
        const Token &first = tree.tokenAt(node.firstToken);
        const Token &last = tree.tokenAt(node.firstToken + node.tokenCount - 1);
        start = first.offset;
        end = last.offset + last.length;
      }
      char buf[48];
      snprintf(buf, sizeof(buf), "@%u-%u", start, end);
      text(buf);
    }
    if (node.flags) {
      text(" :");
      if (node.flags & FLAG_MISSING) {
        text(" missing");
      }
      if (node.flags & FLAG_ERROR) {
        text(" error");
      }
      if (node.flags & FLAG_AMBIENT) {
        text(" ambient");
      }
      if (node.flags & FLAG_EXPORTED) {
        text(" exported");
      }
      if (node.flags & FLAG_DEFAULT) {
        text(" default");
      }
      if (node.flags & FLAG_ASYNC) {
        text(" async");
      }
      if (node.flags & FLAG_CONST) {
        text(" const");
      }
      if (node.flags & FLAG_READONLY) {
        text(" readonly");
      }
      if (node.flags & FLAG_STATIC) {
        text(" static");
      }
      if (node.flags & FLAG_ABSTRACT) {
        text(" abstract");
      }
      if (node.flags & FLAG_OVERRIDE) {
        text(" override");
      }
      if (node.flags & FLAG_ACCESSOR) {
        text(" accessor");
      }
      if (node.flags & FLAG_OPTIONAL) {
        text(" optional");
      }
      if (node.flags & FLAG_REST) {
        text(" rest");
      }
      if (node.flags & FLAG_GENERATOR) {
        text(" generator");
      }
      if (node.flags & FLAG_OPTIONAL_CHAIN) {
        text(" optional-chain");
      }
      if (node.flags & FLAG_ASI) {
        text(" asi");
      }
      if (node.flags & FLAG_PUBLIC) {
        text(" public");
      }
      if (node.flags & FLAG_PRIVATE) {
        text(" private");
      }
      if (node.flags & FLAG_PROTECTED) {
        text(" protected");
      }
      if (node.flags & FLAG_USING) {
        text(" using");
      }
      if (node.flags & FLAG_AWAIT) {
        text(" await");
      }
      if (node.flags & FLAG_TYPE_ONLY) {
        text(" type-only");
      }
    }
    // Tokens not owned by any child belong to this node directly.
    // Spans mode is for machine diffing, so token text (which may span
    // lines) stays out of it.
    uint32_t tokenEnd = spans ? node.firstToken : node.firstToken + node.tokenCount;
    auto children = tree.children(id);
    for (uint32_t i = node.firstToken; i < tokenEnd; ++i) {
      if (tree.tokenAt(i).kind == TokenKind::EndOfFile) {
        break;
      }
      bool owned = false;
      for (NodeId child : children) {
        const Node &c = tree.nodes()[child];
        if (i >= c.firstToken && i < c.firstToken + c.tokenCount) {
          owned = true;
          break;
        }
      }
      if (owned) {
        continue;
      }
      text(" \"");
      text(tree.tokenText(tree.tokenAt(i)));
      text("\"");
    }
    text("\n");
  }
};

} // namespace
void dumpTree(GrammarTree &tree, litestl::util::string &out, bool spans)
{
  std::string buffer;
  // S-expression dump: (Kind flags? "token" children…). Tokens print their
  // source text; nodes recurse. Used by parser tests and debugging.

  TreeWriter writer{tree, buffer, spans};
  if (tree.root() != kNoNode) {
    writer.walk(tree.root());
  }
  out += buffer;
}

} // namespace fastlint::syntax
