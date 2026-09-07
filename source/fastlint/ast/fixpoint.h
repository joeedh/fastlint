#pragma once

// Runs fix passes over one file until nothing changes (docs/ast-design.md
// "Fixpoint driver"). Each pass parses, lowers and binds the current text
// afresh, so rules see positions and scopes that match the text.

#include "fastlint/ast/binder.h"
#include "fastlint/ast/file.h"
#include "fastlint/ast/fixer.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "fastlint/syntax/tree.h"
#include "util/function.h"
#include "util/string.h"
#include "util/vector.h"

#include <string_view>

namespace fastlint::ast {

using litestl::util::function_ref;
using litestl::util::string;

struct FixpointOptions {
  /** Passes after which the driver stops even though fixes keep coming. */
  int maxPasses = 10;
  syntax::Parser::Options parser;
};

/** What one pass sees. The trees live until the pass's fixes have been applied. */
struct Pass {
  const syntax::GrammarTree &tree;
  /** The parser's diagnostics for the text this pass sees. */
  const syntax::Diagnostics &diagnostics;
  AstFile &file;
  Bindings &bindings;
  /** Zero-based; pass 0 sees the original text. */
  int index;
  /** Fixes the pass proposes; applied in source order once the callback returns. */
  Vector<Fix> &fixes;
};

using PassFn = function_ref<void(Pass &)>;

struct FixpointReport {
  /** The text every applied fix produced. */
  string text;
  /** Passes whose callback ran. */
  int passes = 0;
  /** Fixes applied whose output survived. */
  int applied = 0;
  /** Fixes skipped because an earlier fix in their pass had touched their target. */
  int deferred = 0;
  /** A pass proposed nothing, or its fixes left the text unchanged. */
  bool converged = false;
  /** The last pass's output did not parse and was discarded. */
  bool reverted = false;
  /** The starting text has syntax errors; the first pass ran and no fix was applied. */
  bool syntaxErrors = false;
};

/**
 * Parses, lowers and binds `source`, runs `pass`, applies its fixes and
 * prints, then repeats on the printed text until a pass proposes nothing or
 * `maxPasses` is reached. Output that fails to parse is thrown away and the
 * previous text stands.
 */
FixpointReport
runToFixpoint(std::string_view source, PassFn pass, const FixpointOptions &options = {});

} // namespace fastlint::ast
