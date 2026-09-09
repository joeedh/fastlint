#include "fastlint/lint/linter.h"

#include "fastlint/ast/dispatch.h"
#include "fastlint/ast/fixpoint.h"
#include "fastlint/ast/lower.h"
#include "fastlint/ast/printer.h"
#include "fastlint/lint/directives.h"
#include "fastlint/syntax/parser.h"
#include "fastlint/types/type_source.h"

#include <algorithm>
#include <cstdio>

namespace fastlint::lint {

namespace {

void append(string &out, string_view text)
{
  for (char c : text) {
    out += c;
  }
}

string copy(string_view text)
{
  string out;
  append(out, text);
  return out;
}

string_view view(const string &s)
{
  return string_view(s.c_str(), s.size());
}

/** Snapshots the shared type stats around one rule's listener so the gain is
 * charged to that rule. Listeners do not nest, so a single slot suffices. */
struct RuleAttribution {
  types::TypeFacts *facts;
  Map<const RuleDef *, types::FactsStats> *out;
  types::FactsStats snapshot;
};

void attributeToRule(void *ctx, void *owner, bool begin)
{
  auto *attr = static_cast<RuleAttribution *>(ctx);
  if (begin) {
    attr->snapshot = attr->facts->stats();
    return;
  }
  const auto *rule = static_cast<const RuleDef *>(owner);
  types::FactsStats *acc = attr->out->lookup_ptr(rule);
  if (!acc) {
    attr->out->add_overwrite(rule, types::FactsStats{});
    acc = attr->out->lookup_ptr(rule);
  }
  acc->addDelta(attr->snapshot, attr->facts->stats());
}

bool endsWith(string_view text, string_view suffix)
{
  return text.size() >= suffix.size() &&
         text.substr(text.size() - suffix.size()) == suffix;
}

/** The 1-based column of `offset`, counting UTF-16 code units from `lineStart`
 * as editor protocols and ESLint's JSON do: one per byte for ASCII, one per
 * non-ASCII code point in the basic plane, two for an astral one. */
uint32_t utf16Column(string_view source, uint32_t lineStart, uint32_t offset)
{
  uint32_t units = 0;
  uint32_t i = lineStart;
  uint32_t end = offset < source.size() ? offset : uint32_t(source.size());
  while (i < end) {
    unsigned char b = static_cast<unsigned char>(source[i]);
    if (b < 0x80) {
      units += 1;
      i += 1;
    } else if (b < 0xE0) {
      units += 1;
      i += 2;
    } else if (b < 0xF0) {
      units += 1;
      i += 3;
    } else {
      // An astral code point is a surrogate pair, so two UTF-16 units.
      units += 2;
      i += 4;
    }
  }
  return units + 1;
}

/** UTF-16 code-unit offset of a byte position, as a fix range indexes the
 * source (a JS string) rather than its bytes. */
uint32_t utf16Offset(string_view source, uint32_t byteOffset)
{
  return utf16Column(source, 0, byteOffset) - 1;
}

void locate(const syntax::GrammarTree &tree, Diagnostic &d)
{
  string_view source = tree.source();
  auto position = [&](uint32_t offset, uint32_t &line, uint32_t &column) {
    line = tree.lineOf(offset);
    uint32_t lineStart = line > 0 ? tree.lineStarts()[int(line) - 1] : 0;
    column = utf16Column(source, lineStart, offset);
  };
  position(d.start, d.line, d.column);
  position(d.end, d.endLine, d.endColumn);
}

void count(FileResult &out)
{
  out.errorCount = out.warningCount = 0;
  out.fixableErrorCount = out.fixableWarningCount = 0;
  for (const Diagnostic &d : out.diagnostics) {
    if (d.severity == Severity::Error) {
      out.errorCount++;
      out.fixableErrorCount += d.fixable ? 1 : 0;
    } else if (d.severity == Severity::Warn) {
      out.warningCount++;
      out.fixableWarningCount += d.fixable ? 1 : 0;
    }
  }
}

void sortByPosition(Vector<Diagnostic> &diagnostics)
{
  std::stable_sort(diagnostics.data(),
                   diagnostics.data() + diagnostics.size(),
                   [](const Diagnostic &a, const Diagnostic &b) {
                     return a.start != b.start ? a.start < b.start : a.end < b.end;
                   });
}

} // namespace

void FileResult::clear()
{
  diagnostics.clear();
  errorCount = warningCount = 0;
  fixableErrorCount = fixableWarningCount = 0;
  fixesApplied = 0;
  changed = false;
  output = string();
  ignored = false;
  typeError = string();
}

syntax::Parser::Options parserOptionsFor(string_view filename)
{
  syntax::Parser::Options options;
  if (endsWith(filename, ".js") || endsWith(filename, ".mjs") ||
      endsWith(filename, ".cjs"))
  {
    options.javaScript = true;
  } else if (endsWith(filename, ".jsx")) {
    options.javaScript = true;
    options.jsx = true;
  } else if (endsWith(filename, ".tsx")) {
    options.jsx = true;
  }
  return options;
}

// ------------------------------------------------------------------ RuleContext

RuleContext::~RuleContext()
{
  // Listeners may hold references into the states, so they go first.
  m_listeners.clear();
  for (const State &state : m_states) {
    state.destroy(state.value);
  }
}

void RuleContext::report(Report report)
{
  if (report.node && !report.ownSpan) {
    report.start = report.node->start;
    report.end = report.node->end;
  }
  m_reports->append(std::move(report));
}

const Message *findMessage(const RuleDef &rule, const char *id)
{
  string_view wanted(id);
  for (const Message &message : rule.meta.messages) {
    if (wanted == message.id) {
      return &message;
    }
  }
  return nullptr;
}

void interpolate(string_view text, span<const Placeholder> data, string &out)
{
  size_t at = 0;
  while (at < text.size()) {
    size_t open = text.find("{{", at);
    if (open == string_view::npos) {
      append(out, text.substr(at));
      return;
    }
    size_t close = text.find("}}", open + 2);
    if (close == string_view::npos) {
      append(out, text.substr(at));
      return;
    }
    append(out, text.substr(at, open - at));
    string_view name = text.substr(open + 2, close - open - 2);
    while (!name.empty() && name.front() == ' ') {
      name = name.substr(1);
    }
    while (!name.empty() && name.back() == ' ') {
      name = name.substr(0, name.size() - 1);
    }
    bool found = false;
    for (const Placeholder &p : data) {
      if (p.name == name) {
        append(out, p.value);
        found = true;
        break;
      }
    }
    if (!found) {
      append(out, text.substr(open, close + 2 - open));
    }
    at = close + 2;
  }
}

// ----------------------------------------------------------------------- Linter

void Linter::lintFile(const syntax::GrammarTree &tree,
                      const syntax::Diagnostics &diagnostics,
                      ast::AstFile &file,
                      ast::Bindings &bindings,
                      string_view filename,
                      const ResolvedConfig &config,
                      types::TypeFacts *types,
                      Vector<ast::Fix> *fixes,
                      FileResult &out,
                      Map<const RuleDef *, types::FactsStats> *ruleStats,
                      Vector<ast::Fix> *suggestionFixes)
{
  out.clear();
  out.filename = copy(filename);

  for (const string &name : config.unknownRules) {
    Diagnostic d;
    d.message = copy("Definition for rule '");
    append(d.message, view(name));
    append(d.message, "' was not found.");
    d.start = d.end = 0;
    locate(tree, d);
    out.diagnostics.append(std::move(d));
  }

  if (!diagnostics.empty()) {
    // ESLint's parser throws on the first syntax error, so a file with one
    // gets a single fatal message and no rule results. We recover past the
    // error to keep parsing, but report only the earliest one to match.
    const syntax::Diagnostic *first = nullptr;
    for (const syntax::Diagnostic &sd : diagnostics.items()) {
      if (!first || sd.offset < first->offset) {
        first = &sd;
      }
    }
    Diagnostic d;
    d.fatal = true;
    d.start = first->offset;
    d.end = first->offset + first->length;
    d.message = copy("Parsing error: ");
    append(d.message, view(first->message));
    locate(tree, d);
    out.diagnostics.append(std::move(d));
    count(out);
    return;
  }

  // One context per enabled rule; its reports are keyed to it.
  struct Active {
    RuleContext *context;
    Vector<Report> reports;
    Severity severity;
  };
  Vector<Active> active;
  for (const RuleSetting &setting : config.rules) {
    if (setting.severity == Severity::Off) {
      continue;
    }
    if (setting.rule->meta.typeAware && !types) {
      continue;
    }
    active.append(
        {litestl::alloc::New<RuleContext>("rule context", setting.rule, setting.setting),
         {},
         setting.severity});
  }
  string_view source = tree.source();
  for (Active &a : active) {
    a.context->m_file = &file;
    a.context->m_bindings = &bindings;
    a.context->m_source = source;
    a.context->m_filename = filename;
    a.context->m_types = types;
    a.context->m_reports = &a.reports;
    a.context->m_rule->create(*a.context);
  }

  ast::Dispatcher dispatcher;
  for (Active &a : active) {
    void *owner = const_cast<RuleDef *>(a.context->m_rule);
    for (RuleContext::Entry &entry : a.context->m_listeners) {
      if (entry.exit) {
        dispatcher.onExit(entry.kind, ast::Listener(entry.listener), owner);
      } else {
        dispatcher.on(entry.kind, ast::Listener(entry.listener), owner);
      }
    }
  }
  // A rule's type-server work is the delta of the shared stats across its
  // listener; the hook snapshots on entry and attributes the gain on exit.
  RuleAttribution attribution{types, ruleStats, {}};
  if (types && ruleStats) {
    dispatcher.setScope(&attributeToRule, &attribution);
  }
  if (dispatcher.listenerCount() > 0) {
    dispatcher.run(file);
  }

  // Reports become diagnostics; the fix of each is kept aside until the
  // directives have decided which survive.
  struct Pending {
    Diagnostic diagnostic;
    ast::Node *target;
    FixFn fix;
    Vector<FixFn, 1> suggestionFixes;
  };
  Vector<Pending> pending;
  for (Active &a : active) {
    const RuleDef &rule = *a.context->m_rule;
    for (Report &r : a.reports) {
      Pending p;
      p.diagnostic.rule = &rule;
      p.diagnostic.severity = a.severity;
      p.diagnostic.start = r.start;
      p.diagnostic.end = r.end;
      p.diagnostic.messageId = r.messageId;
      p.diagnostic.fixable = bool(r.fix);
      const Message *message = findMessage(rule, r.messageId);
      if (message) {
        interpolate(message->text,
                    span<const Placeholder>(r.data.data(), r.data.size()),
                    p.diagnostic.message);
      } else {
        p.diagnostic.message = copy("Unknown message id '");
        append(p.diagnostic.message, r.messageId ? r.messageId : "");
        append(p.diagnostic.message, "'.");
      }
      for (Suggestion &s : r.suggestions) {
        SuggestionResult result;
        result.messageId = s.messageId;
        const Message *text = findMessage(rule, s.messageId);
        if (text) {
          interpolate(text->text,
                      span<const Placeholder>(s.data.data(), s.data.size()),
                      result.message);
        }
        p.diagnostic.suggestions.append(std::move(result));
        p.suggestionFixes.append(std::move(s.fix));
      }
      locate(tree, p.diagnostic);
      p.target = r.node;
      p.fix = std::move(r.fix);
      pending.append(std::move(p));
    }
  }
  std::stable_sort(pending.data(),
                   pending.data() + pending.size(),
                   [](const Pending &a, const Pending &b) {
                     return a.diagnostic.start != b.diagnostic.start
                                ? a.diagnostic.start < b.diagnostic.start
                                : a.diagnostic.end < b.diagnostic.end;
                   });

  DirectiveSet directives;
  directives.collect(tree, m_registry, config.eslintDirectives);
  for (Pending &p : pending) {
    if (directives.suppressor(
            p.diagnostic.ruleId(), p.diagnostic.start, p.diagnostic.line) >= 0)
    {
      continue;
    }
    if (fixes && p.fix && p.target) {
      fixes->append(ast::Fix{p.target, std::move(p.fix)});
    }
    // Every suggestion contributes an entry, empty fix included, so the k-th
    // collected fix matches the k-th suggestion the diagnostics carry.
    if (suggestionFixes && p.target) {
      for (FixFn &sfix : p.suggestionFixes) {
        suggestionFixes->append(ast::Fix{p.target, std::move(sfix)});
      }
    }
    out.diagnostics.append(std::move(p.diagnostic));
  }
  for (const DirectiveProblem &problem : directives.problems()) {
    Diagnostic d;
    d.start = problem.offset;
    d.end = problem.offset + problem.length;
    d.message = problem.message;
    locate(tree, d);
    out.diagnostics.append(std::move(d));
  }
  if (config.unusedDirectives != Severity::Off) {
    for (const Directive &directive : directives.directives()) {
      if (directive.used || directive.kind == DirectiveKind::Enable) {
        continue;
      }
      Diagnostic d;
      d.severity = config.unusedDirectives;
      d.start = directive.offset;
      d.end = directive.offset + directive.length;
      d.message = copy("Unused ");
      append(d.message, directive.eslint ? string_view("eslint") : kDirectivePrefix);
      append(d.message, "-disable directive (no problems were reported");
      if (!directive.rule.empty()) {
        append(d.message, " from '");
        append(d.message, directive.rule);
        append(d.message, "'");
      }
      append(d.message, ").");
      locate(tree, d);
      out.diagnostics.append(std::move(d));
    }
  }

  for (Active &a : active) {
    litestl::alloc::Delete(a.context);
  }
  sortByPosition(out.diagnostics);
  count(out);
}

void Linter::lintSource(string_view source,
                        string_view filename,
                        const LintOptions &options,
                        FileResult &out)
{
  out.clear();
  out.filename = copy(filename);
  ResolvedConfig config;
  m_config.resolve(filename, config);
  if (config.ignored) {
    out.ignored = true;
    return;
  }
  syntax::Parser::Options parserOptions = parserOptionsFor(filename);

  // The type source sees the text of each pass, so its answers match the tree.
  auto lintPass = [&](const syntax::GrammarTree &tree,
                      const syntax::Diagnostics &diagnostics,
                      ast::AstFile &file,
                      ast::Bindings &bindings,
                      Vector<ast::Fix> *fixes) {
    types::TypeFacts *facts = nullptr;
    if (options.types) {
      string error;
      facts = options.types->beginFile(file, filename, tree.source(), error);
      if (!facts) {
        out.typeError = std::move(error);
      }
    }
    lintFile(tree,
             diagnostics,
             file,
             bindings,
             filename,
             config,
             facts,
             fixes,
             out,
             options.ruleStats);
    if (facts && facts->lastError().size() > 0) {
      out.typeError = facts->lastError();
    }
    if (options.types) {
      options.types->endFile();
    }
  };

  auto lintText = [&](string_view text, Vector<ast::Fix> *fixes) {
    syntax::Diagnostics diagnostics;
    syntax::GrammarTree tree;
    syntax::Parser parser(text, parserOptions, diagnostics);
    parser.parseFile(tree);
    ast::AstFile file(&tree);
    ast::lower(tree, file);
    ast::Bindings bindings;
    ast::bind(file, bindings);
    lintPass(tree, diagnostics, file, bindings, fixes);
  };

  // Applies one collected fix to a fresh parse of the original and diffs, so
  // the JSON output can carry an ESLint-shaped `{range, text}`. `fromSuggestions`
  // picks the suggestion-fix list over the diagnostic-fix list; the k-th entry
  // of either matches the k-th surviving fix of that kind, in diagnostic order.
  auto singleEdit = [&](bool fromSuggestions,
                        int index,
                        bool &hasFix,
                        uint32_t &fixStart,
                        uint32_t &fixEnd,
                        string &fixText) {
    syntax::Diagnostics diagnostics;
    syntax::GrammarTree tree;
    syntax::Parser parser(source, parserOptions, diagnostics);
    parser.parseFile(tree);
    ast::AstFile file(&tree);
    ast::lower(tree, file);
    ast::Bindings bindings;
    ast::bind(file, bindings);
    Vector<ast::Fix> fixes;
    Vector<ast::Fix> suggestionFixes;
    FileResult scratch;
    types::TypeFacts *facts = nullptr;
    if (options.types) {
      string error;
      facts = options.types->beginFile(file, filename, tree.source(), error);
    }
    lintFile(tree,
             diagnostics,
             file,
             bindings,
             filename,
             config,
             facts,
             &fixes,
             scratch,
             nullptr,
             &suggestionFixes);
    if (options.types) {
      options.types->endFile();
    }
    Vector<ast::Fix> &chosen = fromSuggestions ? suggestionFixes : fixes;
    if (index >= int(chosen.size()) || !chosen[index].apply) {
      return;
    }
    ast::applyFixes(file, span<ast::Fix>(&chosen[index], 1));
    string printed;
    ast::printAst(file, printed);
    string_view a = source;
    string_view b = view(printed);
    size_t prefix = 0;
    size_t limit = a.size() < b.size() ? a.size() : b.size();
    while (prefix < limit && a[prefix] == b[prefix]) {
      prefix++;
    }
    size_t suffix = 0;
    while (suffix < a.size() - prefix && suffix < b.size() - prefix &&
           a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix])
    {
      suffix++;
    }
    hasFix = true;
    fixStart = utf16Offset(source, uint32_t(prefix));
    fixEnd = utf16Offset(source, uint32_t(a.size() - suffix));
    fixText = copy(b.substr(prefix, b.size() - suffix - prefix));
  };

  if (!options.fix) {
    lintText(source, nullptr);
    if (options.fixEdits) {
      int fixIndex = 0;
      int suggestionIndex = 0;
      for (Diagnostic &d : out.diagnostics) {
        if (d.fixable) {
          singleEdit(false, fixIndex++, d.hasFix, d.fixStart, d.fixEnd, d.fixText);
        }
        for (SuggestionResult &s : d.suggestions) {
          singleEdit(true, suggestionIndex++, s.hasFix, s.fixStart, s.fixEnd, s.fixText);
        }
      }
    }
    return;
  }

  ast::FixpointOptions fixpoint;
  fixpoint.maxPasses = options.maxPasses;
  fixpoint.parser = parserOptions;
  ast::FixpointReport report = ast::runToFixpoint(
      source,
      [&](ast::Pass &pass) {
        lintPass(pass.tree, pass.diagnostics, pass.file, pass.bindings, &pass.fixes);
      },
      fixpoint);
  // A reverted or cut-off run leaves diagnostics from a text that is not the
  // output; lint the output once more without fixing.
  if (report.reverted || !report.converged) {
    lintText(view(report.text), nullptr);
  }
  out.fixesApplied = report.applied;
  out.changed = view(report.text) != source;
  if (out.changed) {
    out.output = std::move(report.text);
  }
}

} // namespace fastlint::lint
