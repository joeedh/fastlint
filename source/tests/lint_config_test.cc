#include "fastlint/lint/config.h"
#include "fastlint/lint/glob.h"
#include "fastlint/lint/registry.h"
#include "fastlint/rules/rules.h"
#include "testing/test.h"

#include <string>

using namespace fastlint;
using namespace fastlint::lint;

namespace {

std::string sv(const litestl::util::string &s)
{
  return std::string(s.c_str(), s.size());
}

constexpr Message kMessage[] = {{"x", "x"}};
const RuleDef kOptional{
    {"optional-rule",
     "",
     "",
     /*recommended=*/false,
     false,
     false,
     false,
     span<const Message>(kMessage, 1)},
    [](RuleContext &) {},
};
const RuleDef kRecommended{
    {"recommended-rule",
     "",
     "",
     /*recommended=*/true,
     false,
     false,
     false,
     span<const Message>(kMessage, 1)},
    [](RuleContext &) {},
};

struct Fixture {
  Registry registry;
  Config config;
  string error;
  bool ok;

  explicit Fixture(const char *json, const char *baseDir = "C:/project")
  {
    registry.add(rules::kNoDebugger);
    registry.add(kOptional);
    registry.add(kRecommended);
    ok = config.parse(json, baseDir, registry, error);
  }

  const char *severityOf(const char *file, const RuleDef &rule)
  {
    ResolvedConfig resolved;
    config.resolve(file, resolved);
    const RuleSetting *setting = resolved.find(&rule);
    return setting ? Config::severityName(setting->severity) : "unset";
  }
};

} // namespace

TEST(lint_glob, matches_segments_stars_and_braces)
{
  CHECK(globMatch("**/*.ts", "src/a.ts"));
  CHECK(globMatch("**/*.ts", "a.ts"));
  CHECK(!globMatch("**/*.ts", "src/a.tsx"));
  CHECK(globMatch("src/**", "src/deep/er/a.ts"));
  CHECK(globMatch("src/**/*.test.ts", "src/a.test.ts"));
  CHECK(globMatch("src/**/*.test.ts", "src/x/y/a.test.ts"));
  CHECK(!globMatch("src/*.ts", "src/x/a.ts"));
  CHECK(globMatch("src/*.{ts,tsx}", "src/a.tsx"));
  CHECK(!globMatch("src/*.{ts,tsx}", "src/a.js"));
  CHECK(globMatch("a?c", "abc"));
  CHECK(!globMatch("a?c", "a/c"));
  CHECK(globMatch("*.ts", ".hidden.ts"));
  CHECK(globMatch("./src/*.ts", "src/a.ts"));
  CHECK(globMatch("dist/**", "dist"));
  CHECK(!globMatch("dist/**", "distx/a"));
}

TEST(lint_config, severities_in_every_spelling)
{
  Fixture f("{\"rules\": {\"no-debugger\": 2, \"optional-rule\": \"warn\", "
            "\"recommended-rule\": [\"off\", {\"x\": 1}]}}");
  REQUIRE(f.ok);
  CHECK_EQ(std::string(f.severityOf("C:/project/a.ts", rules::kNoDebugger)), "error");
  CHECK_EQ(std::string(f.severityOf("C:/project/a.ts", kOptional)), "warn");
  CHECK_EQ(std::string(f.severityOf("C:/project/a.ts", kRecommended)), "off");

  Fixture bad("{\"rules\": {\"no-debugger\": \"loud\"}}");
  CHECK(!bad.ok);
  CHECK(sv(bad.error).find("needs a severity") != std::string::npos);
}

TEST(lint_config, presets_and_overrides_layer_in_order)
{
  Fixture f(
      "{\"extends\": [\"fastlint:recommended\"],"
      " \"rules\": {\"optional-rule\": [\"warn\", \"opt\"]},"
      " \"overrides\": ["
      "  {\"files\": [\"**/*.test.ts\"], \"rules\": {\"no-debugger\": \"off\", "
      "\"optional-rule\": \"error\"}},"
      "  {\"files\": \"src/legacy/**\", \"rules\": {\"recommended-rule\": \"warn\"}}"
      " ]}");
  REQUIRE(f.ok);
  CHECK_EQ(std::string(f.severityOf("C:/project/src/a.ts", rules::kNoDebugger)), "error");
  CHECK_EQ(std::string(f.severityOf("C:/project/src/a.ts", kRecommended)), "error");
  CHECK_EQ(std::string(f.severityOf("C:/project/src/a.ts", kOptional)), "warn");
  CHECK_EQ(std::string(f.severityOf("C:/project/src/a.test.ts", rules::kNoDebugger)),
           "off");
  CHECK_EQ(std::string(f.severityOf("C:/project/src/a.test.ts", kOptional)), "error");
  CHECK_EQ(std::string(f.severityOf("C:\\project\\src\\legacy\\b.ts", kRecommended)),
           "warn");

  // A bare severity in an override keeps the base layer's options.
  ResolvedConfig resolved;
  f.config.resolve("C:/project/src/a.test.ts", resolved);
  const RuleSetting *optional = resolved.find(&kOptional);
  REQUIRE(optional != nullptr);
  REQUIRE(optional->setting != nullptr);
  CHECK_EQ(std::string(optional->setting->at(1)->asString()), "opt");

  Fixture all("{\"extends\": \"fastlint:all\"}");
  REQUIRE(all.ok);
  CHECK_EQ(std::string(all.severityOf("x.ts", kOptional)), "error");
  Fixture unknown("{\"extends\": \"eslint:recommended\"}");
  CHECK(!unknown.ok);
}

TEST(lint_config, ignores_unknown_rules_and_directive_settings)
{
  Fixture f(
      "{\"ignores\": [\"**/*.d.ts\", \"build/**\"], \"rules\": {\"nope\": \"error\", "
      "\"@typescript-eslint/no-debugger\": \"warn\"}, "
      "\"reportUnusedDisableDirectives\": \"error\", \"eslintDirectives\": false}");
  REQUIRE(f.ok);
  ResolvedConfig resolved;
  f.config.resolve("C:/project/build/a.ts", resolved);
  CHECK(resolved.ignored);
  f.config.resolve("C:/project/src/a.d.ts", resolved);
  CHECK(resolved.ignored);
  f.config.resolve("C:/project/src/a.ts", resolved);
  CHECK(!resolved.ignored);
  REQUIRE_EQ(int(resolved.unknownRules.size()), 1);
  CHECK_EQ(sv(resolved.unknownRules[0]), "nope");
  CHECK_EQ(std::string(f.severityOf("C:/project/src/a.ts", rules::kNoDebugger)), "warn");
  CHECK(resolved.unusedDirectives == Severity::Error);
  CHECK(!resolved.eslintDirectives);

  Fixture flag("{\"reportUnusedDisableDirectives\": false}");
  REQUIRE(flag.ok);
  flag.config.resolve("a.ts", resolved);
  CHECK(resolved.unusedDirectives == Severity::Off);
}

TEST(lint_config, command_line_rules_win)
{
  Fixture f("{\"rules\": {\"no-debugger\": \"off\"}}");
  REQUIRE(f.ok);
  f.config.setRule(&rules::kNoDebugger, Severity::Warn);
  f.config.setRule(&kOptional, Severity::Error);
  CHECK_EQ(std::string(f.severityOf("a.ts", rules::kNoDebugger)), "warn");
  CHECK_EQ(std::string(f.severityOf("a.ts", kOptional)), "error");
}

TEST(lint_config, rejects_malformed_documents)
{
  CHECK(!Fixture("[1, 2]").ok);
  CHECK(!Fixture("{\"rules\": 5}").ok);
  CHECK(!Fixture("{\"overrides\": [{\"rules\": {}}]}").ok);
  CHECK(!Fixture("{not json").ok);
}
