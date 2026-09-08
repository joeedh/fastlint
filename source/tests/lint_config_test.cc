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

const RuleDef kSchemaRule{
    {"schema-rule",
     "",
     "",
     /*recommended=*/false,
     false,
     false,
     false,
     span<const Message>(kMessage, 1),
     R"([{"enum":["always","never"]},{"type":"object","properties":{"depth":{"type":"integer"},"names":{"type":"array","items":{"type":"string"}}},"additionalProperties":false}])"},
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
    registry.add(kSchemaRule);
    ok = config.parse(json, baseDir, registry, error);
  }

  const char *severityOf(const char *file, const RuleDef &rule)
  {
    ResolvedConfig resolved;
    config.resolve(file, resolved);
    const RuleSetting *setting = resolved.find(&rule);
    return setting ? Config::severityName(setting->severity) : "unset";
  }

  std::string projectOf(const char *file)
  {
    return sv(config.projectFor(file));
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

TEST(lint_glob, case_insensitive_matches_regardless_of_case)
{
  // Case matters by default, as on a case-sensitive filesystem.
  CHECK(!globMatch("src/**/*.ts", "Src/App/Main.TS"));
  CHECK(!globMatch("src/*.{ts,tsx}", "src/A.TSX"));
  // The flag matches letters regardless of case, as Windows compares paths.
  CHECK(globMatch("src/**/*.ts", "Src/App/Main.TS", /*caseInsensitive=*/true));
  CHECK(globMatch("src/*.{ts,tsx}", "src/A.TSX", /*caseInsensitive=*/true));
  CHECK(!globMatch("src/*.ts", "src/a.js", /*caseInsensitive=*/true));
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

TEST(lint_config, projects_map_globs_to_tsconfigs)
{
  Fixture f(
      "{\"project\": \"tsconfig.json\","
      " \"projects\": ["
      "  {\"files\": \"packages/web/**\", \"project\": \"packages/web/tsconfig.json\"},"
      "  {\"files\": [\"packages/node/**\"], \"project\": "
      "\"packages/node/tsconfig.json\"}"
      " ]}");
  REQUIRE(f.ok);
  // A `projects` glob wins and its tsconfig is anchored at the base directory.
  CHECK_EQ(f.projectOf("C:/project/packages/web/src/a.ts"),
           "C:/project/packages/web/tsconfig.json");
  CHECK_EQ(f.projectOf("C:/project/packages/node/b.ts"),
           "C:/project/packages/node/tsconfig.json");
  // A file no glob claims falls back to the bare `project`.
  CHECK_EQ(f.projectOf("C:/project/tools/c.ts"), "C:/project/tsconfig.json");
}

TEST(lint_config, project_absolute_path_is_left_alone)
{
  Fixture f("{\"project\": \"C:/elsewhere/tsconfig.json\"}");
  REQUIRE(f.ok);
  CHECK_EQ(f.projectOf("C:/project/a.ts"), "C:/elsewhere/tsconfig.json");
}

TEST(lint_config, no_project_configured_returns_empty)
{
  Fixture f("{\"rules\": {}}");
  REQUIRE(f.ok);
  CHECK(f.projectOf("C:/project/a.ts").empty());
}

TEST(lint_config, rejects_malformed_project_settings)
{
  CHECK(!Fixture("{\"project\": 5}").ok);
  CHECK(!Fixture("{\"projects\": {}}").ok);
  CHECK(!Fixture("{\"projects\": [{\"project\": \"tsconfig.json\"}]}").ok);
  CHECK(!Fixture("{\"projects\": [{\"files\": \"src/**\"}]}").ok);
}

TEST(lint_config, rejects_malformed_documents)
{
  CHECK(!Fixture("[1, 2]").ok);
  CHECK(!Fixture("{\"rules\": 5}").ok);
  CHECK(!Fixture("{\"overrides\": [{\"rules\": {}}]}").ok);
  CHECK(!Fixture("{not json").ok);
}

TEST(lint_config, accepts_options_matching_the_schema)
{
  CHECK(Fixture("{\"rules\": {\"schema-rule\": [\"error\", \"always\"]}}").ok);
  CHECK(Fixture("{\"rules\": {\"schema-rule\": [\"error\", \"never\", {\"depth\": 3, "
                "\"names\": [\"a\", \"b\"]}]}}")
            .ok);
  // A bare severity leaves the options unchecked.
  CHECK(Fixture("{\"rules\": {\"schema-rule\": \"error\"}}").ok);
}

TEST(lint_config, rejects_options_the_schema_forbids)
{
  // Wrong enum value.
  CHECK(!Fixture("{\"rules\": {\"schema-rule\": [\"error\", \"sometimes\"]}}").ok);
  // Unknown object key.
  CHECK(!Fixture("{\"rules\": {\"schema-rule\": [\"error\", \"always\", {\"deth\": 3}]}}")
             .ok);
  // Wrong type for a known key.
  CHECK(
      !Fixture(
           "{\"rules\": {\"schema-rule\": [\"error\", \"always\", {\"depth\": \"x\"}]}}")
           .ok);
  // A wrong array element type.
  CHECK(!Fixture(
             "{\"rules\": {\"schema-rule\": [\"error\", \"always\", {\"names\": [1]}]}}")
             .ok);
  // More options than the schema allows.
  CHECK(!Fixture("{\"rules\": {\"schema-rule\": [\"error\", \"always\", {}, \"extra\"]}}")
             .ok);
}
