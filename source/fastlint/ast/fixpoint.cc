#include "fastlint/ast/fixpoint.h"

#include "fastlint/ast/lower.h"
#include "fastlint/ast/printer.h"
#include "fastlint/syntax/diagnostics.h"

namespace fastlint::ast {

namespace {

std::string_view view(const string &s)
{
  return std::string_view(s.c_str(), s.size());
}

string copy(std::string_view text)
{
  string out;
  for (char c : text) {
    out += c;
  }
  return out;
}

} // namespace

FixpointReport
runToFixpoint(std::string_view source, PassFn pass, const FixpointOptions &options)
{
  FixpointReport report;
  string text = copy(source);
  string previous;
  int lastApplied = 0;
  int lastDeferred = 0;
  int maxPasses = options.maxPasses < 1 ? 1 : options.maxPasses;

  for (int i = 0; i < maxPasses; i++) {
    syntax::Diagnostics diagnostics;
    syntax::GrammarTree tree;
    syntax::Parser parser(view(text), options.parser, diagnostics);
    parser.parseFile(tree);
    // A fix that breaks the syntax is undone rather than passed on.
    if (!diagnostics.empty() && i > 0) {
      report.reverted = true;
      report.applied -= lastApplied;
      report.deferred -= lastDeferred;
      text = std::move(previous);
      break;
    }

    AstFile file(&tree);
    lower(tree, file);
    Bindings bindings;
    bind(file, bindings);
    Vector<Fix> fixes;
    Pass ctx{tree, diagnostics, file, bindings, i, fixes};
    pass(ctx);
    report.passes++;

    if (!diagnostics.empty()) {
      report.syntaxErrors = true;
      break;
    }
    if (fixes.isEmpty()) {
      report.converged = true;
      break;
    }
    FixReport applied = applyFixes(file, span<Fix>(fixes.data(), fixes.size()));
    lastApplied = applied.applied;
    lastDeferred = applied.deferred;
    report.applied += applied.applied;
    report.deferred += applied.deferred;
    if (applied.applied == 0) {
      report.converged = true;
      break;
    }

    string printed;
    printAst(file, printed);
    if (view(printed) == view(text)) {
      report.converged = true;
      break;
    }
    previous = std::move(text);
    text = std::move(printed);
  }

  report.text = std::move(text);
  return report;
}

} // namespace fastlint::ast
