#include "fastlint/ast/generated/views.h"
#include "fastlint/lint/config.h"
#include "fastlint/lint/format.h"
#include "fastlint/lint/linter.h"
#include "fastlint/lint/registry.h"
#include "fastlint/rules/rules.h"
#include "fastlint/version.h"
#include "testing/snapshot.h"
#include "testing/test.h"

#include <string>

using namespace fastlint;
using namespace fastlint::lint;

namespace {

std::string sv(const litestl::util::string &s)
{
  return std::string(s.c_str(), s.size());
}

// A rule with options, placeholders, per-file state and an exit listener,
// so the framework is exercised beyond what no-debugger needs.
constexpr Message kCallMessages[] = {
    {"named", "Call to '{{name}}' is banned."},
    {"count", "{{count}} calls in this file."},
};

struct CallState {
  int calls = 0;
};

void createNoCalls(RuleContext &ctx)
{
  CallState *state = ctx.state<CallState>();
  const JsonValue *banned = ctx.option(0);
  string_view name = banned ? banned->asString() : string_view("eval");
  ctx.on<ast::CallExpression>([&ctx, state, name](ast::Node *node) {
    state->calls++;
    ast::CallExpression call(node);
    if (call.callee()->isIdentifier(name)) {
      ctx.report(node, "named", {{"name", name}});
    }
  });
  ctx.onExit(ast::NodeKind::Program, [&ctx, state](ast::Node *node) {
    if (state->calls > 1) {
      char text[16];
      std::snprintf(text, sizeof text, "%d", state->calls);
      Report r;
      r.node = node;
      r.at(0, 0);
      r.messageId = "count";
      r.data.append({"count", text});
      ctx.report(std::move(r));
    }
  });
}

const RuleDef kNoCalls{
    {"no-calls",
     "test rule",
     "",
     false,
     false,
     false,
     false,
     span<const Message>(kCallMessages, 2)},
    createNoCalls,
};

struct Harness {
  Registry registry;
  Config config;
  string error;

  Harness(const char *json = nullptr)
  {
    registry.add(rules::kNoDebugger);
    registry.add(kNoCalls);
    const char *text =
        json ? json : "{\"rules\": {\"no-debugger\": \"error\", \"no-calls\": \"warn\"}}";
    if (!config.parse(text, "C:/project", registry, error)) {
      INFO("config: {}", error.c_str());
    }
  }

  FileResult
  lint(const char *code, const char *filename = "C:/project/src/a.ts", bool fix = false)
  {
    Linter linter(registry, config);
    LintOptions options;
    options.fix = fix;
    FileResult result;
    linter.lintSource(code, filename, options, result);
    return result;
  }
};

std::string describe(const FileResult &r)
{
  std::string out;
  for (const Diagnostic &d : r.diagnostics) {
    out += std::to_string(d.line) + ":" + std::to_string(d.column) + "-" +
           std::to_string(d.endLine) + ":" + std::to_string(d.endColumn) + " ";
    out += d.severity == Severity::Error ? "error " : "warning ";
    out += sv(d.message);
    if (!d.ruleId().empty()) {
      out += " [";
      out += std::string(d.ruleId());
      out += "]";
    }
    out += "\n";
  }
  return out;
}

} // namespace

TEST(lint_linter, reports_with_positions_and_severities)
{
  Harness h;
  FileResult r = h.lint("let a = 1;\ndebugger;\neval('x');\n");
  CHECK_EQ(describe(r),
           "2:1-2:10 error Unexpected 'debugger' statement. [no-debugger]\n"
           "3:1-3:10 warning Call to 'eval' is banned. [no-calls]\n");
  CHECK_EQ(r.errorCount, 1);
  CHECK_EQ(r.warningCount, 1);
  CHECK_EQ(r.fixableErrorCount, 1);
  CHECK_EQ(r.fixableWarningCount, 0);
  CHECK(!r.changed);
}

TEST(lint_linter, columns_count_utf16_units_not_bytes)
{
  Harness h;
  // The `\xC3\xA9` is é: two UTF-8 bytes but one UTF-16 unit, so `debugger`
  // sits one column earlier than a byte count would place it.
  FileResult r = h.lint("var x = \"\xC3\xA9\"; debugger;");
  CHECK_EQ(r.diagnostics.size(), 1u);
  CHECK_EQ(r.diagnostics[0].column, 14u);
  CHECK_EQ(r.diagnostics[0].endColumn, 23u);
}

TEST(lint_linter, options_state_and_exit_listeners)
{
  Harness h("{\"rules\": {\"no-calls\": [\"error\", \"alert\"]}}");
  FileResult r = h.lint("alert(1);\nfoo();\neval(2);\n");
  CHECK_EQ(describe(r),
           "1:1-1:1 error 3 calls in this file. [no-calls]\n"
           "1:1-1:9 error Call to 'alert' is banned. [no-calls]\n");
}

TEST(lint_linter, fix_removes_debugger_statements)
{
  Harness h;
  FileResult r =
      h.lint("debugger;\nlet a = 1;\ndebugger;\n", "C:/project/src/a.ts", true);
  CHECK(r.changed);
  CHECK_EQ(sv(r.output), "let a = 1;\n");
  CHECK_EQ(r.fixesApplied, 2);
  CHECK_EQ(int(r.diagnostics.size()), 0);
}

TEST(lint_linter, syntax_errors_are_fatal_and_skip_rules)
{
  Harness h;
  FileResult r = h.lint("debugger;\nlet = ;\n");
  REQUIRE(r.diagnostics.size() >= 1u);
  CHECK(r.diagnostics[0].fatal);
  CHECK(r.diagnostics[0].rule == nullptr);
  for (const Diagnostic &d : r.diagnostics) {
    CHECK(d.rule == nullptr);
  }
  CHECK_EQ(r.warningCount, 0);
}

TEST(lint_linter, a_syntax_error_reports_only_the_first)
{
  Harness h;
  // Several parse errors, but ESLint reports one fatal message; so do we.
  FileResult r = h.lint("let x = ;\nfunction (\nif )\n");
  REQUIRE_EQ(r.diagnostics.size(), 1u);
  CHECK(r.diagnostics[0].fatal);
  CHECK(r.diagnostics[0].rule == nullptr);
  // The earliest error, at the first line's empty initializer.
  CHECK_EQ(int(r.diagnostics[0].line), 1);
}

TEST(lint_linter, unknown_rules_are_reported_once_per_file)
{
  Harness h("{\"rules\": {\"no-debugger\": \"error\", \"no-such-rule\": \"error\"}}");
  FileResult r = h.lint("let a = 1;\n");
  CHECK_EQ(describe(r),
           "1:1-1:1 error Definition for rule 'no-such-rule' was not found.\n");
}

TEST(lint_linter, ignored_files_produce_nothing)
{
  Harness h(
      "{\"ignores\": [\"src/generated/**\"], \"rules\": {\"no-debugger\": \"error\"}}");
  FileResult r = h.lint("debugger;", "C:/project/src/generated/a.ts");
  CHECK(r.ignored);
  CHECK_EQ(int(r.diagnostics.size()), 0);
  FileResult kept = h.lint("debugger;", "C:/project/src/a.ts");
  CHECK_EQ(int(kept.diagnostics.size()), 1);
}

TEST(lint_linter, type_aware_rules_wait_for_a_type_server)
{
  constexpr Message none[] = {{"x", "x"}};
  const RuleDef typed{
      {"typed-rule",
       "",
       "",
       false,
       false,
       false,
       /*typeAware=*/true,
       span<const Message>(none, 1)},
      [](RuleContext &ctx) {
        ctx.on(ast::NodeKind::Program,
               [&ctx](ast::Node *node) { ctx.report(node, "x"); });
      },
  };
  Registry registry;
  registry.add(typed);
  Config config;
  string error;
  REQUIRE(config.parse("{\"rules\": {\"typed-rule\": \"error\"}}", "", registry, error));
  Linter linter(registry, config);
  FileResult r;
  linter.lintSource("let a = 1;", "a.ts", LintOptions{}, r);
  CHECK_EQ(int(r.diagnostics.size()), 0);
}

TEST(lint_linter, pretty_and_json_output)
{
  Harness h;
  Vector<FileResult> results;
  results.append(h.lint("debugger;\neval('x');\n", "src/a.ts"));
  results.append(h.lint("let ok = 1;\n", "src/b.ts"));
  string pretty;
  formatPretty(
      span<const FileResult>(results.data(), results.size()), FormatOptions{}, pretty);
  SNAPSHOT(pretty);
  string json;
  formatJson(span<const FileResult>(results.data(), results.size()), json);
  SNAPSHOT(json);
}

TEST(lint_linter, json_fix_ranges)
{
  Harness h("{\"rules\": {\"no-debugger\": \"error\"}}");
  Linter linter(h.registry, h.config);
  LintOptions options;
  options.fixEdits = true;
  FileResult result;
  const char *src = "x;\ndebugger;\ny;\n";
  linter.lintSource(src, "a.ts", options, result);

  const Diagnostic *fixed = nullptr;
  for (const Diagnostic &d : result.diagnostics) {
    if (d.hasFix) {
      fixed = &d;
    }
  }
  REQUIRE(fixed != nullptr);
  std::string source(src);
  std::string applied = source.substr(0, fixed->fixStart) +
                        std::string(fixed->fixText.c_str(), fixed->fixText.size()) +
                        source.substr(fixed->fixEnd);
  // Applying the reported range and text removes the debugger statement.
  CHECK(applied.find("debugger") == std::string::npos);
  CHECK(applied.find("x;") != std::string::npos);
  CHECK(applied.find("y;") != std::string::npos);
}

TEST(lint_linter, json_suggestion_fix_ranges)
{
  Registry registry;
  registry.add(rules::kEqeqeq);
  Config config;
  string error;
  REQUIRE(config.parse("{\"rules\": {\"eqeqeq\": \"error\"}}", "", registry, error));
  Linter linter(registry, config);
  LintOptions options;
  options.fixEdits = true;
  FileResult result;
  // Unrelated operands make eqeqeq offer the swap as a suggestion, not a fix.
  const char *src = "if (a == b) {}\n";
  linter.lintSource(src, "a.ts", options, result);

  const SuggestionResult *suggestion = nullptr;
  for (const Diagnostic &d : result.diagnostics) {
    CHECK(!d.hasFix);
    for (const SuggestionResult &s : d.suggestions) {
      if (s.hasFix) {
        suggestion = &s;
      }
    }
  }
  REQUIRE(suggestion != nullptr);
  std::string source(src);
  std::string applied =
      source.substr(0, suggestion->fixStart) +
      std::string(suggestion->fixText.c_str(), suggestion->fixText.size()) +
      source.substr(suggestion->fixEnd);
  // Applying the reported range and text upgrades `==` to `===`.
  CHECK(applied.find("===") != std::string::npos);
  CHECK(applied.find("==") == applied.find("==="));
}

TEST(lint_linter, no_empty_suggests_a_comment_fix)
{
  Registry registry;
  registry.add(rules::kNoEmpty);
  Config config;
  string error;
  REQUIRE(config.parse("{\"rules\": {\"no-empty\": \"error\"}}", "", registry, error));
  Linter linter(registry, config);
  LintOptions options;
  options.fixEdits = true;
  FileResult result;
  const char *src = "if (x) {}\n";
  linter.lintSource(src, "a.ts", options, result);

  REQUIRE_EQ(int(result.diagnostics.size()), 1);
  const Diagnostic &d = result.diagnostics[0];
  CHECK(!d.hasFix);
  REQUIRE_EQ(int(d.suggestions.size()), 1);
  const SuggestionResult &s = d.suggestions[0];
  REQUIRE(s.hasFix);
  std::string source(src);
  std::string applied = source.substr(0, s.fixStart) +
                        std::string(s.fixText.c_str(), s.fixText.size()) +
                        source.substr(s.fixEnd);
  // The suggested edit puts a comment between the braces.
  CHECK_EQ(applied, "if (x) { /* empty */ }\n");
}

TEST(lint_linter, sarif_output)
{
  Harness h;
  Vector<FileResult> results;
  results.append(h.lint("debugger;\neval('x');\n", "src/a.ts"));
  results.append(h.lint("let ok = 1;\n", "src/b.ts"));
  string sarif;
  formatSarif(span<const FileResult>(results.data(), results.size()), sarif);

  // The driver version tracks the release, so it is masked before snapshotting;
  // otherwise this snapshot would go stale on every version bump.
  std::string masked(sarif.c_str(), sarif.size());
  std::string needle = std::string("\"version\":\"") + version() + "\"";
  size_t pos = masked.find(needle);
  REQUIRE(pos != std::string::npos);
  masked.replace(pos, needle.size(), "\"version\":\"X.Y.Z\"");
  SNAPSHOT(string(masked));
}

TEST(lint_linter, builtin_registry_resolves_plugin_prefixes)
{
  const Registry &registry = builtinRegistry();
  CHECK(registry.find("no-debugger") == &rules::kNoDebugger);
  CHECK(registry.find("@typescript-eslint/no-debugger") == &rules::kNoDebugger);
  CHECK(registry.find("nope") == nullptr);
  for (const RuleDef *rule : registry.rules()) {
    INFO("rule {}", rule->meta.name);
    CHECK(rule->meta.messages.size() > 0);
    CHECK(rule->meta.docsUrl[0] != 0);
  }
}
