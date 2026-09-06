#pragma once

// The accessor interface the generated views are written against. The host
// reads Node inline; a plugin build supplies the C functions instead
// (docs/ast-design.md "Plugins"). Views touch nothing outside this file.

#include "fastlint/ast/node.h"

namespace fastlint::ast::access {

inline NodeKind kind(const Node *n)
{
  return n->kind;
}
inline uint32_t flags(const Node *n)
{
  return n->flags;
}
inline bool hasFlag(const Node *n, Flag flag)
{
  return (n->flags & uint32_t(flag)) != 0;
}
inline uint8_t dataByte(const Node *n, int index)
{
  return uint8_t(n->data >> (8 * index));
}
inline string_view text(const Node *n)
{
  return n->text;
}
inline Node *parent(const Node *n)
{
  return n->parent;
}
inline int childCount(const Node *n)
{
  return int(n->children.size());
}
inline Node *child(const Node *n, int index)
{
  return n->children[index];
}
/** The list slot: every child from `from` to the end. */
inline span<Node *> tail(const Node *n, int from)
{
  Vector<Node *, 3> &children = const_cast<Node *>(n)->children;
  size_t count = children.size() > size_t(from) ? children.size() - size_t(from) : 0;
  return {children.data() + from, count};
}

} // namespace fastlint::ast::access
