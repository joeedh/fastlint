#pragma once

// Lowers a grammar tree into the AST (docs/ast-design.md "Lowering"). This
// is the only code that knows both trees.

#include "fastlint/ast/file.h"
#include "fastlint/syntax/tree.h"

namespace fastlint::ast {

/**
 * Builds the AST for `tree` into `file` and returns the Program node, which is
 * also set as the file's root. Missing children become null slots with
 * `Flag::Incomplete` on the parent; error nodes become `Error` leaves.
 */
Node *lower(const syntax::GrammarTree &tree, AstFile &file);

} // namespace fastlint::ast
