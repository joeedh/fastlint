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

/** Fires with a listener's `owner` token before (`begin`) and after each
 * listener runs; used to attribute the work a listener does to its owner. */
using ScopeHook = void (*)(void *ctx, void *owner, bool begin);

/**
 * Listeners keyed by node kind. `on` fires when the scan reaches a node and
 * `onExit` after its last descendant. Listeners are references: the callable
 * must outlive the dispatcher. Registration is closed by the first `run`. An
 * optional `owner` token travels with each listener for a scope hook to read.
 */
class Dispatcher {
public:
  void on(NodeKind kind, Listener listener, void *owner = nullptr)
  {
    add(kind, listener, false, owner);
  }
  void onExit(NodeKind kind, Listener listener, void *owner = nullptr)
  {
    add(kind, listener, true, owner);
  }
  /** Registers `on` for every kind the view `T` matches. */
  template <typename T> void on(Listener listener, void *owner = nullptr)
  {
    forKinds<T>(listener, false, owner);
  }
  template <typename T> void onExit(Listener listener, void *owner = nullptr)
  {
    forKinds<T>(listener, true, owner);
  }

  int listenerCount() const
  {
    return int(m_entries.size());
  }
  /** Installs a hook fired around every listener with the listener's owner. */
  void setScope(ScopeHook hook, void *ctx)
  {
    m_scopeHook = hook;
    m_scopeCtx = ctx;
  }
  /** Fires every listener over `file`'s preorder vector; enters and exits nest. */
  void run(const AstFile &file);

private:
  struct Entry {
    NodeKind kind;
    bool exit;
    Listener listener;
    void *owner;
  };
  Vector<Entry> m_entries;
  /** First entry of each (kind, exit) bucket once sorted, plus an end slot. */
  Vector<uint32_t> m_starts;
  bool m_sorted = false;
  ScopeHook m_scopeHook = nullptr;
  void *m_scopeCtx = nullptr;

  void add(NodeKind kind, Listener listener, bool exit, void *owner)
  {
    m_entries.append({kind, exit, listener, owner});
    m_sorted = false;
  }
  template <typename T> void forKinds(Listener listener, bool exit, void *owner)
  {
    for (int k = 0; k < kindCount; k++) {
      if (T::matches(NodeKind(k))) {
        add(NodeKind(k), listener, exit, owner);
      }
    }
  }
  void sortEntries();
  bool hasListeners(NodeKind kind, bool exit) const;
  void fire(NodeKind kind, bool exit, Node *node);
};

} // namespace fastlint::ast
