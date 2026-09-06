#pragma once

// Operator precedence for expressions and types, used to decide when a node
// placed into a slot needs parentheses to keep its meaning.

#include "fastlint/ast/node.h"

namespace fastlint::ast {

/**
 * Binding strength of an expression, higher binding tighter: sequence is 1,
 * assignment and arrows 2, conditional 3, then the binary levels up to 14,
 * unary 15, postfix 16, call and member 18, primaries 19. Non-expressions
 * get 19 so they never attract parentheses.
 */
int expressionPrecedence(const Node *n);

/**
 * Binding strength of a type: conditional 1, function and constructor 2,
 * union 3, intersection 4, prefix operators 5, array and indexed access 6,
 * primaries 7.
 */
int typePrecedence(const Node *n);

/**
 * Whether `child` would parse differently, or not at all, in slot `index` of
 * `parent` without parentheses. Takes the `parenthesized` flag into account
 * neither on the child nor on the parent.
 */
bool needsParens(const Node *parent, int index, const Node *child);

} // namespace fastlint::ast
