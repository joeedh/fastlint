#pragma once

// Runs the configured rules over one file (docs/rules.md "Linter"): one
// dispatch walk for every rule, reports turned into diagnostics, disable
// directives applied, and with `fix` the fixpoint driver around it all.

#include "fastlint/ast/binder.h"
#include "fastlint/ast/file.h"
#include "fastlint/ast/fixer.h"
#include "fastlint/lint/config.h"
#include "fastlint/lint/registry.h"
#include "fastlint/lint/rule.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "fastlint/syntax/tree.h"
#include "fastlint/types/type_facts.h"
#include "util/map.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

namespace fastlint::lint {

struct SuggestionResult {
  const char *messageId;
  string message;
};

struct Diagnostic {
  /** Null for a syntax error or a directive problem. */
  const RuleDef *rule = nullptr;
  Severity severity = Severity::Error;
  /** A syntax error; rules did not run on the file. */
  bool fatal = false;
  /** A fix was offered; `--fix` applies it. */
  bool fixable = false;
  uint32_t start = 0;
  uint32_t end = 0;
  /** One-based, columns in UTF-16 code units (as editor protocols and ESLint's
   * JSON count them). `endColumn` is the column after the last character. */
  uint32_t line = 0;
  uint32_t column = 0;
  uint32_t endLine = 0;
  uint32_t endColumn = 0;
  const char *messageId = nullptr;
  string message;
  Vector<SuggestionResult, 1> suggestions;
  /** The single fix the rule offers for this problem, computed on demand
   * (`LintOptions::fixEdits`). `fixStart`/`fixEnd` are UTF-16 code-unit
   * offsets into the source and `fixText` replaces that span. */
  bool hasFix = false;
  uint32_t fixStart = 0;
  uint32_t fixEnd = 0;
  string fixText;

  /** The rule's name, or empty. */
  string_view ruleId() const
  {
    return rule ? string_view(rule->meta.name) : string_view();
  }
};

struct FileResult {
  string filename;
  /** In source order, suppressed ones removed. */
  Vector<Diagnostic> diagnostics;
  int errorCount = 0;
  int warningCount = 0;
  int fixableErrorCount = 0;
  int fixableWarningCount = 0;
  /** Fixes `--fix` applied; `output` holds the text when it changed. */
  int fixesApplied = 0;
  bool changed = false;
  string output;
  /** Matched an `ignores` glob; nothing ran. */
  bool ignored = false;
  /** Why the type source could not type the file; type-aware rules did not run. */
  string typeError;

  void clear();
};

struct LintOptions {
  bool fix = false;
  /** Without `fix`, compute each fixable diagnostic's `fixStart`/`fixEnd`/
   * `fixText` by applying that fix alone to the original and diffing. */
  bool fixEdits = false;
  /** Passes the fixpoint driver may take. */
  int maxPasses = 10;
  /** Null runs the syntactic rules only. */
  types::TypeSource *types = nullptr;
  /** When set, each rule's type-query counts accumulate here, keyed by rule. */
  Map<const RuleDef *, types::FactsStats> *ruleStats = nullptr;
};

/** Parser options implied by a file name's extension. */
syntax::Parser::Options parserOptionsFor(string_view filename);

class Linter {
public:
  Linter(const Registry &registry, const Config &config)
      : m_registry(registry), m_config(config)
  {
  }

  /** Parses, lowers, binds and lints `source`; with `fix` runs to a fixpoint. */
  void lintSource(string_view source,
                  string_view filename,
                  const LintOptions &options,
                  FileResult &out);

  /**
   * Lints an already lowered file. `diagnostics` are the parser's; when any
   * exist they are reported and rules do not run. Rule fixes for problems
   * that survive the directives are appended to `fixes` when given.
   */
  void lintFile(const syntax::GrammarTree &tree,
                const syntax::Diagnostics &diagnostics,
                ast::AstFile &file,
                ast::Bindings &bindings,
                string_view filename,
                const ResolvedConfig &config,
                types::TypeFacts *types,
                Vector<ast::Fix> *fixes,
                FileResult &out,
                Map<const RuleDef *, types::FactsStats> *ruleStats = nullptr);

private:
  const Registry &m_registry;
  const Config &m_config;
};

} // namespace fastlint::lint
