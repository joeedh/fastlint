#include "fastlint/syntax/parser.h"

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

  void text(std::string_view t)
  {
    out.append(t);
  }

  void walk(NodeId id, int depth)
  {
    if (id == kNoNode) {
      fprintf(stderr, "Invalid node reference!\n");
      return;
    }
    const Node &node = tree.nodes()[id];
    for (int i = 0; i < depth; ++i) {
      text("  ");
    }
    text("(");
    text(nodeKindName(node.kind));
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
    uint32_t tokenEnd = node.firstToken + node.tokenCount;
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
    for (NodeId child : tree.children(id)) {
      walk(child, depth + 1);
    }
    for (int i = 0; i < depth; ++i) {
      text("  ");
    }
    text(")\n");
  }
};

} // namespace
void dumpTree(GrammarTree &tree, litestl::util::string &out)
{
  std::string buffer;
  // S-expression dump: (Kind flags? "token" children…). Tokens print their
  // source text; nodes recurse. Used by parser tests and debugging.

  TreeWriter writer{tree, buffer};
  if (tree.root() != kNoNode) {
    writer.walk(tree.root(), 0);
  }
  out += buffer;
}

} // namespace fastlint::syntax
