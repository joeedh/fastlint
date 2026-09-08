#include "fastlint/lint/config.h"
#include "fastlint/lint/linter.h"
#include "fastlint/lint/registry.h"
#include "fastlint/plugin/host.h"
#include "testing/test.h"
#include "util/alloc.h"

#include <string>

using namespace fastlint;
using namespace fastlint::lint;

namespace {

std::string sv(const litestl::util::string &s)
{
  return std::string(s.c_str(), s.size());
}

} // namespace

TEST(plugin_host, loads_a_plugin_and_runs_its_rule)
{
  string error;
  plugin::Plugin *loaded = plugin::Plugin::load(SAMPLE_PLUGIN_PATH, error);
  INFO("load: {}", error.c_str());
  REQUIRE(loaded != nullptr);

  Registry registry;
  for (const RuleDef *rule : loaded->rules()) {
    registry.add(*rule);
  }
  Config config;
  REQUIRE(
      config.parse("{\"rules\": {\"sample/no-foo\": \"error\"}}", "", registry, error));
  Linter linter(registry, config);

  // A no-fix pass reports the plugin's message on the `foo` identifier only.
  FileResult reported;
  linter.lintSource("const x = foo;\n", "a.ts", LintOptions{}, reported);
  REQUIRE_EQ(int(reported.diagnostics.size()), 1);
  CHECK_EQ(sv(reported.diagnostics[0].message), "Rename 'foo' to 'bar'.");
  CHECK_EQ(std::string(reported.diagnostics[0].ruleId()), "sample/no-foo");

  // A fixing pass applies the template fix, rewriting `foo` to `bar`.
  LintOptions fixing;
  fixing.fix = true;
  FileResult fixed;
  linter.lintSource("const x = foo;\n", "a.ts", fixing, fixed);
  CHECK_EQ(fixed.fixesApplied, 1);
  CHECK_EQ(sv(fixed.output), "const x = bar;\n");

  litestl::alloc::Delete(loaded);
}

TEST(plugin_host, a_missing_plugin_is_an_error)
{
  string error;
  plugin::Plugin *loaded = plugin::Plugin::load("does-not-exist.dll", error);
  CHECK(loaded == nullptr);
  CHECK(error.size() > 0);
}
