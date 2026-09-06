#pragma once

// Comment attachment (docs/ast-design.md "Comments"). Every comment belongs
// to exactly one AST node; the file keeps the lists in a side table.

#include "fastlint/syntax/tree.h"
#include "util/vector.h"

#include <cstdint>

namespace fastlint::ast {

class AstFile;
struct Node;

enum class CommentPlace : uint8_t {
  /** Before the node, on its own line or the line of the node's first token. */
  Leading,
  /** After the node's last token on the same line, or after the last child of a
   * node with nothing following it. */
  Trailing,
  /** Inside a node that has no child around the comment (an empty block). */
  Dangling,
};

struct Comment {
  uint32_t offset;
  uint32_t length;
  bool multiLine;
  CommentPlace place;
  /** Set by a fixer when the comment left the source text it came from; the
   * printer emits moved comments from this table instead of from the text. */
  bool moved = false;
};

using CommentList = litestl::util::Vector<Comment, 1>;

/** Attaches every comment in the tree to a node of the lowered AST. */
void attachComments(const syntax::GrammarTree &tree, AstFile &file);

} // namespace fastlint::ast
