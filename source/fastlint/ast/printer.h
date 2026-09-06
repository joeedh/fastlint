#pragma once

// Produces a file's text from its AST (docs/ast-design.md "Printer"). Clean
// nodes copy their source slice; dirty nodes print their captured layout
// around the current children; synthesized nodes print from kind templates.

#include "fastlint/ast/file.h"
#include "fastlint/syntax/tree.h"
#include "util/string.h"

namespace fastlint::ast {

using litestl::util::string;

/** Per-file conventions the printer follows for text it has to invent. */
struct Style {
  bool semicolons = true;
  char quote = '"';
  /** One level of indentation. */
  string_view indent = "  ";
  string_view newline = "\n";
};

/** Reads the conventions off the tokens once per file. */
Style sniffStyle(const syntax::GrammarTree &tree);

struct PrintOptions {
  /**
   * Rewrites every node's span to its position in the output. The file must
   * be reparsed before it is printed again, because clean nodes copy their
   * slice by span.
   */
  bool updateSpans = false;
};

void printAst(AstFile &file, string &out, PrintOptions options = {});

} // namespace fastlint::ast
