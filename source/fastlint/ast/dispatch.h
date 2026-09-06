#pragma once

// Kind-indexed listeners run by one linear scan of a file's preorder vector
// (docs/ast-design.md "Traversal and dispatch"). Rules register callbacks
// per kind; nothing traverses the tree per rule.

#include "fastlint/ast/file.h"
#include "fastlint/ast/generated/kinds.h"
#include "util/function.h"
#include "util/vector.h"

namespace fastlint::ast {

using litestl::util::function_ref;

using Listener = function_ref<void(Node *)>;

/**
 * Listeners keyed by node kind. `on` fires when the scan reaches a node and
 * `onExit` after its last descendant. Listeners are references: the callable
 * must outlive the dispatcher. Registration is closed by the first `run`.
 */
class Dispatcher {
public:
  void on(NodeKind kind, Listener listener)
  {
    add(kind, listener, false);
  }
  void onExit(NodeKind kind, Listener listener)
  {
    add(kind, listener, true);
  }
  /** Registers `on` for every kind the view `T` matches. */
  template <typename T> void on(Listener listener)
  {
    forKinds<T>(listener, false);
  }
  template <typename T> void onExit(Listener listener)
  {
    forKinds<T>(listener, true);
  }

  int listenerCount() const
  {
    return int(m_entries.size());
  }
  /** Fires every listener over `file`'s preorder vector; enters and exits nest. */
  void run(const AstFile &file);

private:
  struct Entry {
    NodeKind kind;
    bool exit;
    Listener listener;
  };
  Vector<Entry> m_entries;
  /** Index of the first entry of each (kind, exit) bucket once sorted; one past-the-end slot. */
  Vector<uint32_t> m_starts;
  bool m_sorted = false;

  void add(NodeKind kind, Listener listener, bool exit)
  {
    m_entries.append({kind, exit, listener});
    m_sorted = false;
  }
  template <typename T> void forKinds(Listener listener, bool exit)
  {
    for (int k = 0; k < kindCount; k++) {
      if (T::matches(NodeKind(k))) {
        add(NodeKind(k), listener, exit);
      }
    }
  }
  void sortEntries();
  bool hasListeners(NodeKind kind, bool exit) const;
  void fire(NodeKind kind, bool exit, Node *node);
};

} // namespace fastlint::ast
