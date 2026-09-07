#pragma once

// Helpers the built-in rules share: control-flow exits, static property
// names, same-reference tests, literal truthiness, comment scans.

#include "fastlint/ast/generated/views.h"
#include "fastlint/ast/node.h"
#include "fastlint/lint/rule.h"
#include "fastlint/syntax/tree.h"
#include "util/function.h"

#include <string_view>

namespace fastlint::rules {

using lint::RuleContext;
using std::string_view;

/**
 * Whether control never reaches the statement after `stmt` in the same
 * list: `return`, `throw`, `break`, `continue`, a block or `if` whose every
 * path exits, a `try` that exits, a `do` whose body exits without a jump,
 * or a `while (true)` / `for (;;)` with no `break`. Other loops, switches
 * and labels answer false, since a `break` inside them stays inside.
 */
bool alwaysExits(const ast::Node *stmt);

/** Whether some statement of `list` always exits. */
bool listExits(litestl::util::span<ast::Node *> list);

/**
 * Whether a `break` (or, with `orContinue`, a `continue`) sits anywhere
 * under `node` outside nested functions. Labels and nesting are not
 * tracked, so the answer errs toward "yes".
 */
bool containsJump(const ast::Node *node, bool orContinue);

/**
 * The name a non-computed key, a string or number literal key, or a
 * template with no substitutions spells; empty for anything computed from
 * a runtime value.
 */
string_view staticPropertyName(const ast::Node *key, bool computed);
/** The static name of a member expression's property, or empty. */
string_view staticMemberName(const ast::Node *member);

/**
 * Whether two expressions name the same place: identifiers, `this`,
 * `super` and private names by spelling, member expressions by object and
 * static or identical property.
 */
bool isSameReference(const ast::Node *a, const ast::Node *b);

/** Truthiness of a literal's value: `0`, `""`, `false`, `null`, `0n` are false. */
bool literalTruthy(const ast::Node *literal);

/** Whether `node` sits in a statement list: a Program, block, case, static or module
 * block. */
bool isStatementListParent(const ast::Node *node);

/** The comments (trivia) whose offset lies in `[from, to)`, in source order. */
void commentsBetween(const syntax::GrammarTree &tree,
                     uint32_t from,
                     uint32_t to,
                     litestl::util::function_ref<void(const syntax::Trivia &)> fn);
bool hasCommentBetween(const syntax::GrammarTree &tree, uint32_t from, uint32_t to);

/** One-based line of `offset` in the context's file. */
uint32_t lineOf(const RuleContext &ctx, uint32_t offset);

/** The offset of `token` between `from` and `to` in the source, or `from` when absent. */
uint32_t findToken(string_view source, uint32_t from, uint32_t to, string_view token);

/** The last non-whitespace offset before `end` (exclusive), or `end`. */
uint32_t lastNonSpaceBefore(string_view source, uint32_t end);
/** The first non-whitespace offset at or after `start`, or the source size. */
uint32_t firstNonSpaceAt(string_view source, uint32_t start);

/** Whether `id` is an unresolved (global) reference in the file's bindings. */
bool isGlobalReference(const RuleContext &ctx, const ast::Node *id);

} // namespace fastlint::rules
