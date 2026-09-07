#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

namespace {
constexpr const char *kTruthy = "Unnecessary conditional, value is always truthy.";
constexpr const char *kFalsy = "Unnecessary conditional, value is always falsy.";
constexpr const char *kNever = "Unnecessary conditional, value is `never`.";
constexpr const char *kNeverNullish =
    "Unnecessary conditional, expected left-hand side of `??` operator to be possibly "
    "null or undefined.";
constexpr const char *kAlwaysNullish =
    "Unnecessary conditional, left-hand side of `??` operator is always `null` or "
    "`undefined`.";
} // namespace

TEST_TAGGED(rules_no_unnecessary_condition, cases, "integration")
{
  test::runTypedRuleTests(
      rules::kNoUnnecessaryCondition,
      {
          // A genuine boolean condition.
          {R"(
declare const b1: boolean;
declare const b2: boolean;
const t = b1 && b2;
if (b1) {
}
)"},
          // A number can be zero, so the condition is real.
          {R"(
declare const x: number;
if (x) {
}
)"},
          // A possibly-nullish left side of `??` is a real check.
          {R"(
declare const x: string | undefined;
const y = x ?? 'd';
)"},
          // Indexing into an array is exempt, since the type omits out-of-bounds.
          {R"(
declare const arr: number[];
if (arr[0]) {
}
)"},
      },
      {
          // An object is always truthy.
          {R"(
declare const b1: object;
declare const b2: boolean;
const t1 = b1 && b2;
)",
           {{"alwaysTruthy", 4, 12, 4, 14, kTruthy}}},
          // A union of falsy literals is always falsy.
          {R"(
declare const b1: '' | false;
declare const b2: boolean;
const t1 = b1 && b2;
)",
           {{"alwaysFalsy", 4, 12, 4, 14, kFalsy}}},
          // A `never` value.
          {R"(
declare const b1: never;
declare const b2: boolean;
const t1 = b1 && b2;
)",
           {{"never", 4, 12, 4, 14, kNever}}},
          // A `true` literal used as an `if` test.
          {R"(
declare const b: true;
if (b) {
}
)",
           {{"alwaysTruthy", 3, 5, 3, 6, kTruthy}}},
          // A non-nullish left side of `??`.
          {R"(
function test(a: string) {
  return a ?? 'default';
}
)",
           {{"neverNullish", 3, 10, 3, 11, kNeverNullish}}},
          // A left side of `??` constrained to `null`.
          {R"(
function test<T extends null>(a: T) {
  return a ?? 'default';
}
)",
           {{"alwaysNullish", 3, 10, 3, 11, kAlwaysNullish}}},
      },
      "basic");
}
