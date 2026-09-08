#pragma once

// Statement completion analysis (docs/rules/no-unreachable.md,
// docs/rules/no-fallthrough.md). Whether control can fall off the end of a
// statement to the one after it, tracking `break`/`continue` targets, switch
// exhaustiveness and labels. Replaces the earlier statement-list exit guess.

#include "fastlint/ast/node.h"
#include "util/span.h"

namespace fastlint::rules {

/**
 * Whether control can fall off the end of `stmt` to the following statement.
 * False for `return`/`throw`/`break`/`continue` and for constructs whose every
 * path leaves: an `if`/`else` that both leave, a `switch` with a `default`
 * whose last clause leaves and that has no `break` to it, an infinite loop
 * with no `break`, a `try` whose reachable paths all leave.
 */
bool completesNormally(const ast::Node *stmt);

/**
 * Whether control can fall off the end of a statement sequence. A statement
 * that cannot complete makes the rest unreachable, so the sequence then cannot
 * complete either.
 */
bool sequenceCompletesNormally(litestl::util::span<ast::Node *> list);

} // namespace fastlint::rules
