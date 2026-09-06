#pragma once

// The rule-facing tree (docs/ast-design.md). Nodes are pooled per file, own an
// SBO child vector, and link back to the grammar node they were lowered from.
// Typed views over Node are generated into generated/views.h.

#include "fastlint/ast/generated/kinds.h"
#include "fastlint/syntax/tree.h"
#include "util/span.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

namespace fastlint::ast {

using litestl::util::span;
using litestl::util::Vector;
using std::string_view;

/** A node in a grammar tree, which is the file's or a template's. */
struct GrammarRef {
  const syntax::GrammarTree *tree = nullptr;
  syntax::NodeId id = syntax::kNoNode;

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
};

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
