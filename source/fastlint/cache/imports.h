#pragma once

#include "fastlint/ast/file.h"
#include "util/string.h"
#include "util/vector.h"

namespace fastlint::cache {

using litestl::util::Vector;
using string = litestl::util::string;

/** Appends every module specifier `file` names, unquoted, deduplicated and in source
 * order. Covers import and export declarations, `import x = require("m")`, dynamic
 * `import("m")` and `require("m")` with one string literal argument, and `import("m")`
 * types. */
void collectImports(const ast::AstFile &file, Vector<string> &specifiers);

} // namespace fastlint::cache
