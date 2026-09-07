#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST(rules_no_non_null_assertion, cases)
{
  test::runRuleTests(
      rules::kNoNonNullAssertion,
      {
          {"x;"},
          {"x.y;"},
          {"x.y.z;"},
          {"x?.y.z;"},
          {"x?.y?.z;"},
          {"!x;"},
          {"x != null;"},
      },
      {
          {"x!;", {{"noNonNull", 1, 1, 1, 3, "Forbidden non-null assertion."}}},
          {"x!.y;", {{"noNonNull", 1, 1, 1, 3}}},
          {"x.y!;", {{"noNonNull", 1, 1, 1, 5}}},
          {"!x!.y;", {{"noNonNull", 1, 2, 1, 4}}},
          {"x!.y?.z;", {{"noNonNull"}}},
          {"x![y];", {{"noNonNull"}}},
          {"x![y]?.z;", {{"noNonNull"}}},
          {"x.y.z!();", {{"noNonNull"}}},
          {"x.y?.z!();", {{"noNonNull"}}},
          {"x!!!;",
           {{"noNonNull", 1, 1, 1, 3},
            {"noNonNull", 1, 1, 1, 4},
            {"noNonNull", 1, 1, 1, 5}}},
          {"x!!.y;", {{"noNonNull"}, {"noNonNull"}}},
          {"x.y!!;", {{"noNonNull"}, {"noNonNull"}}},
          {"x.y.z!!();", {{"noNonNull"}, {"noNonNull"}}},
          {"x!?.[y].z;", {{"noNonNull"}}},
          {"x!?.y.z;", {{"noNonNull"}}},
          {"x.y.z!?.();", {{"noNonNull"}}},
          {"x!.y = 1;", {{"noNonNull"}}},
          {"x!.y++;", {{"noNonNull"}}},
          {"delete x!.y;", {{"noNonNull"}}},
          {"[x!.y] = [1];", {{"noNonNull"}}},
          {"for (x!.y of z) {}", {{"noNonNull"}}},
          {"function f(a = x!) {}", {{"noNonNull"}}},
      });
}
