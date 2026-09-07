#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST(rules_eqeqeq, cases)
{
  test::runRuleTests(
      rules::kEqeqeq,
      {
          {"a === b"},
          {"a !== b"},
          {"a === null"},
          {"typeof a === 'string'"},
          {"typeof a == 'number'", R"(["smart"])"},
          {"'foo' == 'bar'", R"(["smart"])"},
          {"1 != 2", R"(["smart"])"},
          {"a == null", R"(["smart"])"},
          {"null == a", R"(["allow-null"])"},
          {"a == null", R"(["always", {"null": "ignore"}])"},
          {"a != null", R"(["always", {"null": "never"}])"},
          {"a === null", R"(["always", {"null": "always"}])"},
      },
      {
          {"a == b",
           {{"unexpected", 1, 3, 1, 5, "Expected '===' and instead saw '=='."}}},
          {"a != b",
           {{"unexpected", 1, 3, 1, 5, "Expected '!==' and instead saw '!='."}}},
          {"if (a == b) {}", {{"unexpected", 1, 7, 1, 9}}},
          {"(a) == (b)", {{"unexpected", 1, 5, 1, 7}}},
          {"typeof a == 'number'",
           {{"unexpected", 1, 10, 1, 12}},
           "typeof a === 'number'"},
          {"'b' != 'a'", {{"unexpected"}}, "'b' !== 'a'"},
          {"1 == 1", {{"unexpected"}}, "1 === 1"},
          {"`a` == `b`", {{"unexpected"}}, "`a` === `b`"},
          {"a == null", {{"unexpected"}}},
          {"a == b", {{"unexpected"}}, nullptr, R"(["smart"])"},
          {"'foo' == 1", {{"unexpected"}}, nullptr, R"(["smart"])"},
          {"a === null",
           {{"unexpected", 1, 3, 1, 6, "Expected '==' and instead saw '==='."}},
           nullptr,
           R"(["always", {"null": "never"}])"},
          {"a !== null", {{"unexpected"}}, nullptr, R"(["always", {"null": "never"}])"},
          {"a == b\nc != d", {{"unexpected", 1, 3}, {"unexpected", 2, 3}}},
      });
}
