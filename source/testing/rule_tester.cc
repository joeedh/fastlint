#include "testing/rule_tester.h"

#include "fastlint/lint/config.h"
#include "fastlint/lint/linter.h"
#include "fastlint/lint/registry.h"
#include "testing/test.h"

#include <cstdio>
#include <string>

namespace fastlint::test {

namespace {

using lint::Config;
using lint::FileResult;
using lint::Linter;
using lint::LintOptions;
using lint::Registry;
using lint::RuleDef;

std::string sv(const litestl::util::string &s)
{
  return std::string(s.c_str(), s.size());
}

/** A config enabling `rule` alone, with `options` appended after the severity. */
bool configFor(const RuleDef &rule,
               const char *options,
               const Registry &registry,
               Config &config)
{
  std::string text = "{\"rules\": {\"";
  text += rule.meta.name;
  text += "\": [\"error\"";
  if (options) {
    std::string_view list(options);
    // The case gives a JSON array; splice its elements in.
    size_t open = list.find('[');
    size_t close = list.rfind(']');
    if (open != std::string_view::npos && close != std::string_view::npos &&
        close > open + 1)
    {
      text += ", ";
      text += list.substr(open + 1, close - open - 1);
    }
  }
  text += "]}, \"reportUnusedDisableDirectives\": \"off\"}";
  litestl::util::string error;
  bool ok = config.parse(text, "", registry, error);
  if (!ok) {
    INFO("config: {}", error.c_str());
  }
  return ok;
}

const char *filenameFor(const char *given)
{
  return given ? given : "test.ts";
}

} // namespace

void runRuleTests(const RuleDef &rule,
                  std::initializer_list<ValidCase> valid,
                  std::initializer_list<InvalidCase> invalid)
{
  Registry registry;
  registry.add(rule);

  int index = 0;
  for (const ValidCase &c : valid) {
    char label[32];
    std::snprintf(label, sizeof label, "valid[%d]", index++);
    SUBCASE(label)
    {
      INFO("code: {}", c.code);
      Config config;
      if (!configFor(rule, c.options, registry, config)) {
        CHECK(false);
        continue;
      }
      Linter linter(registry, config);
      FileResult result;
      linter.lintSource(c.code, filenameFor(c.filename), LintOptions{}, result);
      for (const lint::Diagnostic &d : result.diagnostics) {
        INFO("unexpected {}:{} {}", d.line, d.column, d.message.c_str());
        CHECK(false);
      }
    }
  }

  index = 0;
  for (const InvalidCase &c : invalid) {
    char label[32];
    std::snprintf(label, sizeof label, "invalid[%d]", index++);
    SUBCASE(label)
    {
      INFO("code: {}", c.code);
      Config config;
      if (!configFor(rule, c.options, registry, config)) {
        CHECK(false);
        continue;
      }
      Linter linter(registry, config);
      FileResult result;
      linter.lintSource(c.code, filenameFor(c.filename), LintOptions{}, result);
      CHECK_EQ(int(result.diagnostics.size()), int(c.errors.size()));
      int i = 0;
      for (const ExpectedError &expected : c.errors) {
        if (i >= int(result.diagnostics.size())) {
          break;
        }
        const lint::Diagnostic &d = result.diagnostics[i++];
        INFO("error[{}]: {}:{} {}", i - 1, d.line, d.column, d.message.c_str());
        CHECK(d.rule == &rule);
        CHECK_EQ(std::string(d.messageId ? d.messageId : ""),
                 std::string(expected.messageId));
        if (expected.line) {
          CHECK_EQ(int(d.line), expected.line);
        }
        if (expected.column) {
          CHECK_EQ(int(d.column), expected.column);
        }
        if (expected.endLine) {
          CHECK_EQ(int(d.endLine), expected.endLine);
        }
        if (expected.endColumn) {
          CHECK_EQ(int(d.endColumn), expected.endColumn);
        }
        if (expected.message) {
          CHECK_EQ(sv(d.message), std::string(expected.message));
        }
      }
      for (; i < int(result.diagnostics.size()); i++) {
        const lint::Diagnostic &d = result.diagnostics[i];
        INFO("unexpected {}:{} {}", d.line, d.column, d.message.c_str());
        CHECK(false);
      }

      LintOptions fixing;
      fixing.fix = true;
      FileResult fixed;
      linter.lintSource(c.code, filenameFor(c.filename), fixing, fixed);
      if (c.output) {
        CHECK(fixed.changed);
        CHECK_EQ(sv(fixed.output), std::string(c.output));
      } else {
        CHECK(!fixed.changed);
      }
    }
  }
}

} // namespace fastlint::test
