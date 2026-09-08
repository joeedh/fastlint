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

/**
 * One piece of a dirty node's original layout: a slice of its tree's source
 * (`child` null) or a child that sat there, with the slot it occupied.
 */
struct LayoutItem {
  uint32_t from;
  uint32_t to;
  Node *child;
  int slot;
};

/** A node's layout as captured the moment it first became dirty. */
struct Layout {
  Vector<LayoutItem, 4> items;
  /** False when children overlapped (a shorthand property) and the printer
   * must use the kind template instead. */
  bool usable = true;
};

/** A slice of the file's source the printer must never copy. */
struct DeadRange {
  uint32_t offset;
  uint32_t length;
};

/** One node of the file's preorder; `subtreeEnd` is the index after its last descendant.
 */
struct PreorderEntry {
  Node *node;
  uint32_t subtreeEnd;
};

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

  /**
   * Rebuilds the preorder vector from the root. Lowering calls it; an edit
   * leaves the vector stale until the fixpoint driver calls it again.
   */
  void buildPreorder();
  span<const PreorderEntry> preorder() const
  {
    return {const_cast<Vector<PreorderEntry> &>(m_preorder).data(), m_preorder.size()};
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
  /** Stores synthesized comment text and returns its index, which a synthetic
   * `Comment` holds in `offset`. The text keeps its comment delimiters, block
   * or line, as a source comment's slice does. */
  uint32_t addSyntheticComment(string_view text)
  {
    m_syntheticComments.append(intern(text));
    return uint32_t(m_syntheticComments.size() - 1);
  }
  /** The text of the synthetic comment at `index`, or empty when out of range. */
  string_view syntheticComment(uint32_t index) const
  {
    return index < m_syntheticComments.size() ? m_syntheticComments[int(index)]
                                              : string_view();
  }
  /** Appends `from`'s comments to `to` and drops `from`'s entry. */
  void moveComments(const Node *from, const Node *to);
  void dropComments(const Node *node)
  {
    m_comments.remove(node);
  }

  /**
   * Records `node`'s current layout unless one exists. Fixers call it on
   * the clean-to-dirty transition, before the first edit; the printer reads
   * it back to interleave own text with the current children.
   */
  void captureLayout(Node *node);
  const Layout *layout(const Node *node) const
  {
    return const_cast<Map<const Node *, Layout> &>(m_layouts).lookup_ptr(node);
  }
  /** Forgets a captured layout, so the printer falls back to the kind template. */
  void dropLayout(const Node *node)
  {
    if (m_layouts.contains(node)) {
      m_layouts.remove(node);
    }
  }

  /** Marks a source slice the printer must skip when copying text. */
  void markDead(uint32_t offset, uint32_t length)
  {
    m_dead.append({offset, length});
  }
  span<const DeadRange> deadRanges() const
  {
    return {const_cast<Vector<DeadRange> &>(m_dead).data(), m_dead.size()};
  }

private:
  const syntax::GrammarTree *m_tree;
  Pool<Node, 256> m_pool;
  Node *m_root = nullptr;
  Vector<PreorderEntry> m_preorder;
  Map<const Node *, CommentList> m_comments;
  Vector<string_view> m_syntheticComments;
  Map<const Node *, Layout> m_layouts;
  Vector<DeadRange> m_dead;
  Vector<char *> m_chunks;
  size_t m_chunkUsed = 0;
  size_t m_chunkSize = 0;
};

} // namespace fastlint::ast
