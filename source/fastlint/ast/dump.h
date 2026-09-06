#pragma once

// The AST's s-expression dump, driven by the kind tables (docs/debugging.md
// `dump-ast`). One node per line: kind, text, `:flag` names, `field=value`
// pairs and `@start-end`; fixed slots are named, list elements are not.
// Attached comments follow the head line as `;leading`, `;trailing` or
// `;dangling` entries.

#include "fastlint/ast/file.h"
#include "util/string.h"

namespace fastlint::ast {

using litestl::util::string;

void dumpAst(const AstFile &file, string &out);

} // namespace fastlint::ast
