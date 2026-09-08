#pragma once

// The litestl-free stand-ins the generated views need in a plugin build
// (docs/ast-design.md "Plugins"). `Node` stays opaque: a plugin holds node
// pointers and reads them through the host, never the host's layout. Compiled
// only with FASTLINT_PLUGIN defined.

#include "fastlint/ast/generated/kinds.h"

#include <span>
#include <string_view>

namespace fastlint::ast {

using std::string_view;
template <class T> using span = std::span<T>;

/** Opaque host node; the plugin passes pointers and reads through `access::`. */
struct Node;

/** Base of every generated view: a node pointer with typed accessors. */
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
