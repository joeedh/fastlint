#pragma once

// Disable directives in comments (docs/rules.md "Disable directives"):
// `fastlint-disable`, `fastlint-enable`, `fastlint-disable-line` and
// `fastlint-disable-next-line`, each with an optional rule list and a `--`
// justification. The `eslint-` spellings are accepted as aliases.

#include "fastlint/lint/registry.h"
#include "fastlint/syntax/tree.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

namespace fastlint::lint {

enum class DirectiveKind : uint8_t { Disable, Enable, DisableLine, DisableNextLine };

struct Directive {
  DirectiveKind kind;
  /** The comment's span. */
  uint32_t offset;
  uint32_t length;
  /** The line a line directive covers, or the line a block directive starts on. */
  uint32_t line;
  /** The rule's canonical name; empty when the directive names every rule. */
  string_view rule;
  /** Spelled with the `eslint-` prefix. */
  bool eslint;
  /** Suppressed at least one problem. */
  bool used = false;
};

/** A malformed directive, reported as an error at the comment. */
struct DirectiveProblem {
  uint32_t offset;
  uint32_t length;
  string message;
};

class DirectiveSet {
public:
  /**
   * Reads every comment of `tree`. Directives naming a rule `registry` does
   * not know are dropped when spelled `eslint-` (another linter's rule) and
   * reported when spelled `fastlint-`. With `eslintCompat` false the
   * `eslint-` spellings are ordinary comments.
   */
  void
  collect(const syntax::GrammarTree &tree, const Registry &registry, bool eslintCompat);

  /**
   * The directive that suppresses a problem of `rule` starting at `offset`
   * on `line`, or -1. Marks it used. Problems must arrive in source order,
   * since block directives are replayed as a state machine.
   */
  int suppressor(string_view rule, uint32_t offset, uint32_t line);

  span<const Directive> directives() const
  {
    return {const_cast<Vector<Directive> &>(m_directives).data(), m_directives.size()};
  }
  span<const DirectiveProblem> problems() const
  {
    return {const_cast<Vector<DirectiveProblem> &>(m_problems).data(), m_problems.size()};
  }

private:
  Vector<Directive> m_directives;
  Vector<DirectiveProblem> m_problems;
  /** Replay state over block directives, in `m_directives` order. */
  int m_next = 0;
  int m_disableAll = -1;
  /** Per-rule `disable` directives in force, and rules re-enabled under a `disable` of
   * all. */
  Vector<int> m_disabled;
  Vector<string_view> m_enabled;

  void advance(uint32_t offset);
  void addDirective(const syntax::GrammarTree &tree,
                    const Registry &registry,
                    const syntax::Trivia &comment,
                    string_view body,
                    bool eslint);
};

} // namespace fastlint::lint
