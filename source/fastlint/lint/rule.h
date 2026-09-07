#pragma once

// The rule interface (docs/rules.md "Rule interface"). A rule is a static
// definition: metadata plus a `create` function that registers kind-indexed
// listeners on a per-file context and reports through it.

#include "fastlint/ast/binder.h"
#include "fastlint/ast/dispatch.h"
#include "fastlint/ast/file.h"
#include "fastlint/ast/fixer.h"
#include "fastlint/ast/node.h"
#include "fastlint/tsgo/json.h"
#include "util/alloc.h"
#include "util/function.h"
#include "util/span.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace fastlint::types {
class TypeFacts;
}

namespace fastlint::lint {

using litestl::util::span;
using litestl::util::string;
using litestl::util::Vector;
using std::string_view;
using tsgo::JsonValue;

enum class Severity : uint8_t { Off, Warn, Error };

/** One message template; `{{name}}` placeholders are filled from a report's data. */
struct Message {
  const char *id;
  const char *text;
};

struct RuleMeta {
  /** The configured name, without any plugin prefix (`no-debugger`). */
  const char *name;
  const char *description;
  const char *docsUrl;
  /** In the `fastlint:recommended` preset. */
  bool recommended = false;
  /** Reports may carry a fix that `--fix` applies. */
  bool fixable = false;
  /** Reports may carry suggestions, which are fixes only an editor applies. */
  bool hasSuggestions = false;
  /** Needs `TypeFacts`; skipped when the linter runs without a type server. */
  bool typeAware = false;
  span<const Message> messages;
};

class RuleContext;

struct RuleDef {
  RuleMeta meta;
  /** Registers listeners for one file; called once per rule per file. */
  void (*create)(RuleContext &ctx);
};

struct Placeholder {
  string_view name;
  string_view value;
};

using FixFn = litestl::util::function<void(ast::Fixer &)>;

struct Suggestion {
  const char *messageId;
  Vector<Placeholder, 2> data;
  FixFn fix;
};

/** One problem a rule found. The span is `node`'s unless `at` gave one. */
struct Report {
  ast::Node *node = nullptr;
  uint32_t start = 0;
  uint32_t end = 0;
  bool ownSpan = false;
  const char *messageId = nullptr;
  Vector<Placeholder, 2> data;
  /** Empty when the report has no fix; `node` is the fix target. */
  FixFn fix;
  Vector<Suggestion, 1> suggestions;

  void at(uint32_t from, uint32_t to)
  {
    start = from;
    end = to;
    ownSpan = true;
  }
};

using Listener = litestl::util::function<void(ast::Node *)>;

/** What the linter hands a rule for one file; the linter implements it. */
class RuleContext {
public:
  RuleContext(const RuleDef *rule, const JsonValue *setting)
      : m_rule(rule), m_setting(setting)
  {
  }
  RuleContext(const RuleContext &) = delete;
  RuleContext &operator=(const RuleContext &) = delete;
  ~RuleContext();

  const RuleDef &rule() const
  {
    return *m_rule;
  }
  ast::AstFile &file() const
  {
    return *m_file;
  }
  ast::Bindings &bindings() const
  {
    return *m_bindings;
  }
  string_view source() const
  {
    return m_source;
  }
  string_view filename() const
  {
    return m_filename;
  }
  /** Source text of `node`. */
  string_view textOf(const ast::Node *node) const
  {
    return m_source.substr(node->start, node->end - node->start);
  }
  /** Null when the linter runs without a type server. */
  types::TypeFacts *types() const
  {
    return m_types;
  }
  /** The rule's configured option at `index` (0 is the first after the severity), or
   * null. */
  const JsonValue *option(int index = 0) const
  {
    return m_setting ? m_setting->at(index + 1) : nullptr;
  }

  // ------------------------------------------------------------- listeners

  void on(ast::NodeKind kind, Listener listener)
  {
    m_listeners.append({kind, false, std::move(listener)});
  }
  void onExit(ast::NodeKind kind, Listener listener)
  {
    m_listeners.append({kind, true, std::move(listener)});
  }
  /** Listens on every kind the view `T` matches. */
  template <typename T> void on(Listener listener)
  {
    forKinds<T>(std::move(listener), false);
  }
  template <typename T> void onExit(Listener listener)
  {
    forKinds<T>(std::move(listener), true);
  }

  /**
   * Allocates per-file state the rule's listeners may capture; destroyed
   * with the context, after the last listener has fired.
   */
  template <typename T, typename... Args> T *state(Args &&...args)
  {
    T *value = litestl::alloc::New<T>("rule state", std::forward<Args>(args)...);
    m_states.append(
        {value, [](void *p) { litestl::alloc::Delete(static_cast<T *>(p)); }});
    return value;
  }

  // --------------------------------------------------------------- reports

  void report(Report report);
  void report(ast::Node *node, const char *messageId)
  {
    Report r;
    r.node = node;
    r.messageId = messageId;
    report(std::move(r));
  }
  void
  report(ast::Node *node, const char *messageId, std::initializer_list<Placeholder> data)
  {
    Report r;
    r.node = node;
    r.messageId = messageId;
    for (const Placeholder &p : data) {
      r.data.append(p);
    }
    report(std::move(r));
  }
  void report(ast::Node *node, const char *messageId, FixFn fix)
  {
    Report r;
    r.node = node;
    r.messageId = messageId;
    r.fix = std::move(fix);
    report(std::move(r));
  }

private:
  friend class Linter;

  struct Entry {
    ast::NodeKind kind;
    bool exit;
    Listener listener;
  };
  struct State {
    void *value;
    void (*destroy)(void *);
  };

  const RuleDef *m_rule;
  const JsonValue *m_setting;
  ast::AstFile *m_file = nullptr;
  ast::Bindings *m_bindings = nullptr;
  string_view m_source;
  string_view m_filename;
  types::TypeFacts *m_types = nullptr;
  /** Where reports go; set by the linter before `create` runs. */
  Vector<Report> *m_reports = nullptr;
  Vector<Entry> m_listeners;
  Vector<State, 1> m_states;

  template <typename T> void forKinds(Listener listener, bool exit)
  {
    for (int k = 0; k < ast::kindCount; k++) {
      if (T::matches(ast::NodeKind(k))) {
        // Each kind needs its own copy; the last one takes the original.
        Listener copy = listener;
        m_listeners.append({ast::NodeKind(k), exit, std::move(copy)});
      }
    }
  }
};

/** Fills `{{name}}` placeholders of `text` from `data`; unknown names stay as written. */
void interpolate(string_view text, span<const Placeholder> data, string &out);

/** The message template of `rule` with `id`, or null. */
const Message *findMessage(const RuleDef &rule, const char *id);

} // namespace fastlint::lint
