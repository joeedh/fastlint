#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

namespace {
constexpr const char *kOr =
    "Prefer using nullish coalescing operator (`??`) instead of a logical or "
    "(`||`), as it is a safer operator.";
constexpr const char *kAssign =
    "Prefer using nullish coalescing operator (`??=`) instead of a logical "
    "assignment (`||=`), as it is a safer operator.";
} // namespace

TEST_TAGGED(rules_prefer_nullish_coalescing, cases, "integration")
{
  test::runTypedRuleTests(
      rules::kPreferNullishCoalescing,
      {
          // A non-nullable left side is fine.
          {R"(
declare const a: string;
const b = a || 'd';
)"},
          {R"(
declare let a: number;
a ||= 1;
)"},
          // `&&` is not this rule's concern.
          {R"(
declare const a: string | undefined;
declare const c: string;
const b = a && c;
)"},
          // A conditional test is ignored by default.
          {R"(
declare const a: string | undefined;
declare const b: string;
if (a || b) {
}
)"},
          {R"(
declare const a: string | undefined;
declare const b: string;
while (a || b) {}
)"},
          {R"(
declare const a: string | undefined;
const x = (a || 'd') ? 1 : 2;
)"},
          // An ignored primitive keeps its union out of the rule.
          {R"(
declare const a: string | undefined;
const b = a || 'd';
)",
           R"([{"ignorePrimitives": {"string": true}}])"},
          {R"(
declare const a: string | undefined;
const b = a || 'd';
)",
           R"([{"ignorePrimitives": true}])"},
          // A mixed logical expression is ignored when asked.
          {R"(
declare const a: string | undefined;
declare const b: string;
declare const c: string;
const x = (a || b) && c;
)",
           R"([{"ignoreMixedLogicalExpressions": true}])"},
          // A `Boolean(...)` coercion is ignored when asked.
          {R"(
declare const a: string | undefined;
const b = Boolean(a || '');
)",
           R"([{"ignoreBooleanCoercion": true}])"},
      },
      {
          {R"(
declare const a: string | undefined;
const b = a || 'd';
)",
           {{"preferNullishOverOr", 3, 13, 3, 15, kOr}}},
          {R"(
declare let a: string | undefined;
a ||= 'd';
)",
           {{"preferNullishOverOr", 3, 3, 3, 6, kAssign}}},
          {R"(
declare const a: any;
const b = a || 'd';
)",
           {{"preferNullishOverOr", 3, 13, 3, 15, kOr}}},
          {R"(
declare const a: string | null;
const b = a || 'd';
)",
           {{"preferNullishOverOr", 3, 13, 3, 15, kOr}}},
          // An ignored primitive that is not in the union still reports.
          {R"(
declare const a: string | undefined;
const b = a || 'd';
)",
           {{"preferNullishOverOr", 3, 13, 3, 15, kOr}},
           nullptr,
           R"([{"ignorePrimitives": {"number": true}}])"},
          // A conditional test is checked when the option is off.
          {R"(
declare const a: string | undefined;
declare const b: string;
if (a || b) {
}
)",
           {{"preferNullishOverOr", 4, 7, 4, 9, kOr}},
           nullptr,
           R"([{"ignoreConditionalTests": false}])"},
      },
      "basic");
}
