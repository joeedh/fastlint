#pragma once

// The rule-facing tree (docs/ast-design.md). Nodes are pooled per file, own an
// SBO child vector, and link back to the grammar node they were lowered from.
// Typed views over Node are generated into generated/views.h.

#include "fastlint/ast/generated/kinds.h"
#include "fastlint/ast/kind_info.h"
#include "fastlint/syntax/tree.h"
#include "util/span.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

namespace fastlint::ast {

using litestl::util::span;
using litestl::util::Vector;
using std::string_view;

constexpr uint32_t kNoToken = 0xffffffffu;

/**
 * A node in a grammar tree, which is the file's or a template's. The token
 * range is the grammar node's unless `firstToken` is set, which lets an AST
 * node stand for part of a grammar node (a template quasi, a method's value).
 */
struct GrammarRef {
  const syntax::GrammarTree *tree = nullptr;
  syntax::NodeId id = syntax::kNoNode;
  uint32_t firstToken = kNoToken;
  uint32_t tokenCount = 0;

  explicit operator bool() const
  {
    return tree != nullptr;
  }
};

struct Node {
  NodeKind kind;
  /** Set by every mutation on the node and on each ancestor; drives the printer. */
  bool dirty = false;
  uint32_t flags = 0;
  /** Enum fields packed one per byte, in nodes.def order. */
  uint32_t data = 0;
  /** Name or raw text for kinds that carry one; points into the source or the file's
   * string arena. */
  string_view text;
  /** Byte span: the node's own tokens and every descendant's. Zero-length until lowered
   * or reprinted. */
  uint32_t start = 0;
  uint32_t end = 0;
  Node *parent = nullptr;
  /** Null for synthesized nodes. */
  GrammarRef grammar;
  /** Fixed slots first (nullptr when an optional child is absent), then the list. */
  Vector<Node *, 3> children;

  explicit Node(NodeKind k) : kind(k)
  {
  }

  bool hasFlag(Flag flag) const
  {
    return (flags & uint32_t(flag)) != 0;
  }
  void setFlag(Flag flag, bool on = true)
  {
    flags = on ? flags | uint32_t(flag) : flags & ~uint32_t(flag);
  }
  uint8_t dataByte(int index) const
  {
    return uint8_t(data >> (8 * index));
  }
  void setDataByte(int index, uint8_t value)
  {
    uint32_t shift = uint32_t(8 * index);
    data = (data & ~(0xffu << shift)) | (uint32_t(value) << shift);
  }

  /** Appends a child slot; a null child records an absent optional slot. */
  void appendChild(Node *child)
  {
    children.append(child);
    if (child) {
      child->parent = this;
    }
  }

  template <typename T> bool is() const
  {
    return T::matches(kind);
  }
  /** The typed view, or a null view when the kind does not match. */
  template <typename T> T as()
  {
    return T::matches(kind) ? T(this) : T();
  }

  // ----------------------------------------------------------------- predicates

  bool hasCategory(Category category) const
  {
    return ast::hasCategory(kind, category);
  }
  bool isStatement() const
  {
    return hasCategory(Category::Statement);
  }
  bool isExpression() const
  {
    return hasCategory(Category::Expression);
  }
  bool isType() const
  {
    return hasCategory(Category::Type);
  }
  bool isPattern() const
  {
    return hasCategory(Category::Pattern);
  }
  /** An Identifier, and one named `name` when `name` is given. */
  bool isIdentifier(string_view name = {}) const
  {
    return kind == NodeKind::Identifier && (name.empty() || text == name);
  }
  bool isLiteral() const
  {
    return kind == NodeKind::Literal;
  }
  /**
   * A string Literal, and one spelling `value` when `value` is given. The
   * comparison is against the raw text between the quotes, so escapes in the
   * source are not decoded.
   */
  bool isStringLiteral(string_view value = {}) const;

  // ------------------------------------------------------------------ traversal

  struct AncestorRange;
  /** The parent chain from the parent up to the root, nearest first. */
  AncestorRange ancestors() const;
  /** The nearest strict ancestor that is a statement, or null. */
  Node *enclosingStatement() const;
  /** The nearest strict ancestor that is one of the five function kinds, or null. */
  Node *enclosingFunction() const;
  /** The nearest strict ancestor of kind `k`, or null. */
  Node *enclosing(NodeKind k) const;
  /** The nearest strict ancestor matching `T`, as a view (null on none). */
  template <typename T> T enclosing() const
  {
    for (Node *a = parent; a; a = a->parent) {
      if (T::matches(a->kind)) {
        return T(a);
      }
    }
    return T();
  }
  /** The first direct child matching `T`, as a view (null on none). */
  template <typename T> T firstChild() const
  {
    for (Node *c : children) {
      if (c && T::matches(c->kind)) {
        return T(c);
      }
    }
    return T();
  }
  /** Calls `fn(Node *)` for every descendant in preorder, excluding this node. */
  template <typename F> void descendants(F &&fn) const
  {
    for (Node *c : children) {
      if (c) {
        fn(c);
        c->descendants(fn);
      }
    }
  }
  /** Calls `fn(T)` for every descendant whose kind matches `T`, in preorder. */
  template <typename T, typename F> void descendants(F &&fn) const
  {
    descendants([&](Node *d) {
      if (T::matches(d->kind)) {
        fn(T(d));
      }
    });
  }
};

struct Node::AncestorRange {
  struct Iterator {
    Node *at;
    Node *operator*() const
    {
      return at;
    }
    Iterator &operator++()
    {
      at = at->parent;
      return *this;
    }
    bool operator!=(const Iterator &b) const
    {
      return at != b.at;
    }
  };
  Node *first;
  Iterator begin() const
  {
    return {first};
  }
  Iterator end() const
  {
    return {nullptr};
  }
};

inline Node::AncestorRange Node::ancestors() const
{
  return {parent};
}

inline Node *Node::enclosing(NodeKind k) const
{
  for (Node *a = parent; a; a = a->parent) {
    if (a->kind == k) {
      return a;
    }
  }
  return nullptr;
}

inline Node *Node::enclosingStatement() const
{
  for (Node *a = parent; a; a = a->parent) {
    if (a->isStatement()) {
      return a;
    }
  }
  return nullptr;
}

inline Node *Node::enclosingFunction() const
{
  for (Node *a = parent; a; a = a->parent) {
    switch (a->kind) {
    case NodeKind::FunctionDeclaration:
    case NodeKind::FunctionExpression:
    case NodeKind::ArrowFunctionExpression:
    case NodeKind::TSDeclareFunction:
    case NodeKind::TSEmptyBodyFunctionExpression:
      return a;
    default:
      break;
    }
  }
  return nullptr;
}

inline bool Node::isStringLiteral(string_view value) const
{
  if (kind != NodeKind::Literal || text.size() < 2) {
    return false;
  }
  char quote = text.front();
  if ((quote != '"' && quote != '\'') || text.back() != quote) {
    return false;
  }
  return value.empty() || text.substr(1, text.size() - 2) == value;
}

/** Base of every generated view. A view is a Node pointer with typed accessors. */
struct View {
  Node *n = nullptr;

  View() = default;
  explicit View(Node *node) : n(node)
  {
  }

  explicit operator bool() const
  {
    return n != nullptr;
  }
  Node *node() const
  {
    return n;
  }
};

} // namespace fastlint::ast
