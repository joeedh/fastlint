#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

namespace {
constexpr const char *kObject =
    "Unexpected object value in conditional. The condition is always true.";
constexpr const char *kNullish =
    "Unexpected nullish value in conditional. The condition is always false.";
} // namespace

TEST_TAGGED(rules_strict_boolean_expressions, cases, "integration")
{
  test::runTypedRuleTests(
      rules::kStrictBooleanExpressions,
      {
          // A boolean is always fine.
          {R"(
declare const x: boolean;
if (x) {
}
)"},
          {R"(
declare const x: boolean;
const y = !x;
)"},
          // `never` is fine.
          {R"(
declare const x: never;
if (x) {
}
)"},
          // Strings and numbers are allowed by default.
          {R"(
declare const x: string;
if (x) {
}
)"},
          {R"(
declare const x: number;
if (x) {
}
)"},
          // Nullable objects are allowed by default.
          {R"(
declare const x: object | null;
if (x) {
}
)"},
          // The right operand of a control-flow `||` is not a condition.
          {"(false && true) || [];"},
          // Options can admit nullable primitives.
          {R"(
declare const x: boolean | null;
if (x) {
}
)",
           R"([{"allowNullableBoolean": true}])"},
          {R"(
declare const x: string | null;
if (x) {
}
)",
           R"([{"allowNullableString": true}])"},
      },
      {
          // An object is always truthy.
          {R"(
declare const x: object;
if (x) {
}
)",
           {{"conditionErrorObject", 3, 5, 3, 6, kObject}}},
          // Nullish is always falsy.
          {R"(
declare const x: null | undefined;
if (x) {
}
)",
           {{"conditionErrorNullish", 3, 5, 3, 6, kNullish}}},
          // A bare string when `allowString` is off.
          {R"(
declare const x: string;
if (x) {
}
)",
           {{"conditionErrorString", 3, 5, 3, 6}},
           nullptr,
           R"([{"allowString": false}])"},
          // A bare number when `allowNumber` is off.
          {R"(
declare const x: number;
if (x) {
}
)",
           {{"conditionErrorNumber", 3, 5, 3, 6}},
           nullptr,
           R"([{"allowNumber": false}])"},
          // A nullable number is disallowed by default.
          {R"(
declare const x: number | null;
if (x) {
}
)",
           {{"conditionErrorNullableNumber", 3, 5, 3, 6}}},
          // A nullable string is disallowed by default.
          {R"(
declare const x: string | null;
if (x) {
}
)",
           {{"conditionErrorNullableString", 3, 5, 3, 6}}},
          // Only the outermost operands of a condition are checked, each by its type.
          {"if (('' && {}) || (0 && void 0)) { }",
           {{"conditionErrorString", 1, 6, 1, 8},
            {"conditionErrorObject", 1, 12, 1, 14},
            {"conditionErrorNumber", 1, 20, 1, 21},
            {"conditionErrorNullish", 1, 25, 1, 31}},
           nullptr,
           R"([{"allowNullableObject": false, "allowNumber": false, "allowString": false}])"},
      },
      "basic");
}
