#pragma once

// The accessor interface the generated views are written against. The host
// reads Node inline; a plugin build (FASTLINT_PLUGIN) routes every read through
// the host's function table instead (docs/ast-design.md "Plugins"). The view
// code that includes this file is byte-identical on both sides.

#ifdef FASTLINT_PLUGIN

#include "fastlint/plugin/abi.h"
#include "fastlint/plugin/plugin_node.h"

namespace fastlint::ast {

/** Set by `fastlint_plugin_init`; every `access::` call routes through it. */
extern const fl_host_api *g_flHost;

namespace access {

inline const fl_node *raw(const Node *n)
{
  return reinterpret_cast<const fl_node *>(n);
}

inline NodeKind kind(const Node *n)
{
  return NodeKind(g_flHost->kind(raw(n)));
}
inline uint32_t flags(const Node *n)
{
  return g_flHost->flags(raw(n));
}
inline bool hasFlag(const Node *n, Flag flag)
{
  return g_flHost->has_flag(raw(n), uint32_t(flag)) != 0;
}
inline uint8_t dataByte(const Node *n, int index)
{
  return g_flHost->data_byte(raw(n), index);
}
inline string_view text(const Node *n)
{
  fl_str s = g_flHost->text(raw(n));
  return string_view(s.ptr, s.len);
}
inline Node *parent(const Node *n)
{
  return reinterpret_cast<Node *>(g_flHost->parent(raw(n)));
}
inline int childCount(const Node *n)
{
  return g_flHost->child_count(raw(n));
}
inline Node *child(const Node *n, int index)
{
  return reinterpret_cast<Node *>(g_flHost->child(raw(n), index));
}
inline span<Node *> tail(const Node *n, int from)
{
  fl_nodes t = g_flHost->tail(raw(n), from);
  return span<Node *>(const_cast<Node **>(reinterpret_cast<Node *const *>(t.ptr)), t.len);
}

} // namespace access
} // namespace fastlint::ast

#else // host build

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

#endif
