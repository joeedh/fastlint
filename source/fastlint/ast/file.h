#pragma once

// Owns one file's AST: the node pool, the string arena for synthesized text,
// the comment side table, and the link to the grammar tree it was lowered
// from. Released as a unit.

#include "fastlint/ast/comments.h"
#include "fastlint/ast/node.h"
#include "fastlint/syntax/tree.h"
#include "util/map.h"
#include "util/pool.h"
#include "util/vector.h"

#include <string_view>

namespace fastlint::ast {

using litestl::util::Map;
using litestl::util::Pool;

class AstFile {
public:
  explicit AstFile(const syntax::GrammarTree *tree = nullptr) : m_tree(tree)
  {
  }
  ~AstFile();

  AstFile(const AstFile &) = delete;
  AstFile &operator=(const AstFile &) = delete;

  /** Allocates a node with no children; the caller fills every slot. */
  Node *make(NodeKind kind, GrammarRef grammar = {});

  /** Copies `text` into file-owned storage that lives as long as the file. */
  string_view intern(string_view text);

  Node *root() const
  {
    return m_root;
  }
  void setRoot(Node *root)
  {
    m_root = root;
  }
  const syntax::GrammarTree *grammar() const
  {
    return m_tree;
  }
  int nodeCount() const
  {
    return m_pool.live_count();
  }

  /** The node's comments, or null when it has none. */
  const CommentList *comments(const Node *node) const
  {
    return const_cast<Map<const Node *, CommentList> &>(m_comments).lookup_ptr(node);
  }
  /** The node's comment list, created on first use. */
  CommentList &commentsFor(const Node *node)
  {
    return m_comments[node];
  }
  /** Appends `from`'s comments to `to` and drops `from`'s entry. */
  void moveComments(const Node *from, const Node *to);
  void dropComments(const Node *node)
  {
    m_comments.remove(node);
  }

private:
  const syntax::GrammarTree *m_tree;
  Pool<Node, 256> m_pool;
  Node *m_root = nullptr;
  Map<const Node *, CommentList> m_comments;
  Vector<char *> m_chunks;
  size_t m_chunkUsed = 0;
  size_t m_chunkSize = 0;
};

} // namespace fastlint::ast
