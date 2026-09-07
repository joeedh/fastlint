#include "fastlint/lint/config.h"
#include "fastlint/lint/directives.h"
#include "fastlint/lint/linter.h"
#include "fastlint/lint/registry.h"
#include "fastlint/rules/rules.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "testing/test.h"

#include <string>

using namespace fastlint;
using namespace fastlint::lint;

namespace {

std::string sv(const litestl::util::string &s)
{
  return std::string(s.c_str(), s.size());
}

constexpr Message kOther[] = {{"x", "other"}};
const RuleDef kOtherRule{
    {"other-rule", "", "", false, false, false, false, span<const Message>(kOther, 1)},
    [](RuleContext &) {},
};

struct Parsed {
  syntax::Diagnostics diagnostics;
  syntax::GrammarTree tree;

  explicit Parsed(const char *code)
  {
    syntax::Parser parser(code, {}, diagnostics);
    parser.parseFile(tree);
  }
};

struct Directives {
  Registry registry;
  DirectiveSet set;

  Directives(const char *code, bool eslintCompat = true)
  {
    registry.add(rules::kNoDebugger);
    registry.add(kOtherRule);
    Parsed parsed(code);
    set.collect(parsed.tree, registry, eslintCompat);
  }
};

/** Lints `code` with no-debugger and the unused-directive setting `unused`. */
std::string lintDescribe(const char *code, const char *unused = "warn")
{
  Registry registry;
  registry.add(rules::kNoDebugger);
  registry.add(kOtherRule);
  Config config;
  string error;
  std::string text =
      "{\"rules\": {\"no-debugger\": \"error\", \"other-rule\": \"error\"}, "
      "\"reportUnusedDisableDirectives\": \"";
  text += unused;
  text += "\"}";
  if (!config.parse(text, "", registry, error)) {
    return "config error: " + sv(error);
  }
  Linter linter(registry, config);
  FileResult r;
  linter.lintSource(code, "a.ts", LintOptions{}, r);
  std::string out;
  for (const Diagnostic &d : r.diagnostics) {
    out += std::to_string(d.line) + ":" + std::to_string(d.column) + " ";
    out += d.severity == Severity::Error ? "error " : "warning ";
    out += sv(d.message) + "\n";
  }
  return out;
}

} // namespace

TEST(lint_directives, collects_every_form)
{
  Directives d("/* fastlint-disable */\n"
               "// fastlint-enable no-debugger, other-rule -- reason\n"
               "// eslint-disable-line no-debugger\n"
               "/* eslint-disable-next-line\n   @typescript-eslint/no-debugger */\n"
               "// fastlint-disable-next-line other-rule\n"
               "// not a directive: fastlint-disable\n"
               "// eslint-disable-line react/jsx-key\n");
  span<const Directive> items = d.set.directives();
  REQUIRE_EQ(int(items.size()), 5);
  CHECK(items[0].kind == DirectiveKind::Disable);
  CHECK(items[0].rule.empty());
  CHECK(!items[0].eslint);
  CHECK(items[1].kind == DirectiveKind::Enable);
  CHECK_EQ(std::string(items[1].rule), "no-debugger");
  CHECK(items[2].kind == DirectiveKind::Enable);
  CHECK_EQ(std::string(items[2].rule), "other-rule");
  CHECK(items[3].kind == DirectiveKind::DisableLine);
  CHECK_EQ(items[3].line, 3u);
  CHECK(items[3].eslint);
  // The two-line block comment is rejected, as ESLint rejects it; the
  // directive on line 6 covers line 7, and line 8 names another linter's rule.
  CHECK(items[4].kind == DirectiveKind::DisableNextLine);
  CHECK_EQ(std::string(items[4].rule), "other-rule");
  CHECK_EQ(items[4].line, 7u);
  REQUIRE_EQ(int(d.set.problems().size()), 1);
  CHECK_EQ(sv(d.set.problems()[0].message),
           "eslint-disable-next-line comment should not span multiple lines.");
}

TEST(lint_directives, unknown_fastlint_rules_and_multiline_line_directives_are_problems)
{
  Directives d("// fastlint-disable-next-line no-such-rule\n"
               "/* fastlint-disable-line\n no-debugger */\n");
  CHECK_EQ(int(d.set.directives().size()), 0);
  span<const DirectiveProblem> problems = d.set.problems();
  REQUIRE_EQ(int(problems.size()), 2);
  CHECK_EQ(sv(problems[0].message), "Definition for rule 'no-such-rule' was not found.");
  CHECK_EQ(sv(problems[1].message),
           "fastlint-disable-line comment should not span multiple lines.");
}

TEST(lint_directives, eslint_spellings_can_be_turned_off)
{
  Directives d("// eslint-disable-next-line no-debugger\n", false);
  CHECK_EQ(int(d.set.directives().size()), 0);
}

TEST(lint_directives, block_directives_replay_as_a_state_machine)
{
  Directives d("/* fastlint-disable */\n"
               "a;\n"
               "/* fastlint-enable other-rule */\n"
               "b;\n"
               "/* fastlint-disable other-rule */\n"
               "c;\n"
               "/* fastlint-enable */\n"
               "d;\n");
  // Offsets of the statements a, b, c, d.
  CHECK(d.set.suppressor("no-debugger", 23, 2) == 0);
  CHECK(d.set.suppressor("other-rule", 23, 2) == 0);
  CHECK(d.set.suppressor("no-debugger", 59, 4) == 0);
  CHECK(d.set.suppressor("other-rule", 59, 4) == -1);
  CHECK(d.set.suppressor("other-rule", 97, 6) == 2);
  CHECK(d.set.suppressor("no-debugger", 97, 6) == 0);
  CHECK(d.set.suppressor("no-debugger", 122, 8) == -1);
  CHECK(d.set.suppressor("other-rule", 122, 8) == -1);
  span<const Directive> items = d.set.directives();
  CHECK(items[0].used);
  CHECK(!items[1].used);
  CHECK(items[2].used);
  CHECK(!items[3].used);
}

TEST(lint_directives, suppress_and_report_unused)
{
  CHECK_EQ(lintDescribe("debugger; // fastlint-disable-line\n"
                        "// eslint-disable-next-line no-debugger\n"
                        "debugger;\n"
                        "debugger;\n"),
           "4:1 error Unexpected 'debugger' statement.\n");
  CHECK_EQ(lintDescribe("/* fastlint-disable no-debugger */\n"
                        "debugger;\n"
                        "/* fastlint-enable no-debugger */\n"
                        "debugger;\n"),
           "4:1 error Unexpected 'debugger' statement.\n");
  CHECK_EQ(
      lintDescribe("// fastlint-disable-next-line no-debugger\n"
                   "let a = 1;\n"
                   "// eslint-disable-next-line\n"
                   "let b = 1;\n"),
      "1:1 warning Unused fastlint-disable directive (no problems were reported from "
      "'no-debugger').\n"
      "3:1 warning Unused eslint-disable directive (no problems were reported).\n");
  CHECK_EQ(lintDescribe("// fastlint-disable-next-line no-debugger\nlet a = 1;\n", "off"),
           "");
  CHECK_EQ(
      lintDescribe("// fastlint-disable-next-line no-debugger\nlet a = 1;\n", "error"),
      "1:1 error Unused fastlint-disable directive (no problems were reported from "
      "'no-debugger').\n");
  // A directive for another linter's rule is neither applied nor reported.
  CHECK_EQ(lintDescribe("// eslint-disable-next-line react/jsx-key\nlet a = 1;\n"), "");
}
