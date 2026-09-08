#pragma once

// Helpers the built-in rules share: static property names, same-reference
// tests, literal truthiness, comment scans. Control-flow completion lives in
// rules/flow.h.

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
/** One-based byte column of `offset` in the context's file. */
uint32_t columnOf(const RuleContext &ctx, uint32_t offset);

/** The offset of `token` between `from` and `to` in the source, or `from` when absent. */
uint32_t findToken(string_view source, uint32_t from, uint32_t to, string_view token);

/** The last non-whitespace offset before `end` (exclusive), or `end`. */
uint32_t lastNonSpaceBefore(string_view source, uint32_t end);
/** The first non-whitespace offset at or after `start`, or the source size. */
uint32_t firstNonSpaceAt(string_view source, uint32_t start);

/** Whether `id` is an unresolved (global) reference in the file's bindings. */
bool isGlobalReference(const RuleContext &ctx, const ast::Node *id);

/** A `.js`-family file with no import or export, whose top level is the global scope. */
bool isScript(const RuleContext &ctx);
/** A `.d.ts`, `.d.mts` or `.d.cts` file. */
bool isDefinitionFile(const RuleContext &ctx);

} // namespace fastlint::rules
