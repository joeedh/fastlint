#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

namespace {
constexpr const char *kConditional =
    "Expected non-Promise value in a boolean conditional.";
constexpr const char *kSpread = "Expected a non-Promise value to be spread in an object.";
constexpr const char *kArgument =
    "Promise returned in function argument where a void return was expected.";
constexpr const char *kVariable =
    "Promise-returning function provided to variable where a void return was "
    "expected.";
} // namespace

TEST_TAGGED(rules_no_misused_promises, cases, "integration")
{
  test::runTypedRuleTests(
      rules::kNoMisusedPromises,
      {
          // No promise in the condition.
          {R"(
if (true) {
}
)"},
          // Conditional checking turned off.
          {R"(
if (Promise.resolve()) {
}
)",
           R"([{"checksConditionals": false}])"},
          // A union that is only sometimes a promise is a legitimate nullish check.
          {R"(
declare const p: Promise<number> | undefined;
if (p) {
}
)"},
          // A void callback that actually returns void is fine.
          {R"(
const fn = (cb: () => void) => cb();
fn(() => {});
)"},
          // A variable typed to return a promise accepts an async function.
          {R"(
let f: () => Promise<void>;
f = async () => {};
)"},
          // Spreading a non-promise object is fine.
          {"console.log({ ...{ a: 1 } });"},
      },
      {
          // A promise as an `if` test.
          {R"(
if (Promise.resolve()) {
}
)",
           {{"conditional", 2, 5, 2, 22, kConditional}}},
          // A promise as a ternary test.
          {"Promise.resolve() ? 123 : 456;",
           {{"conditional", 1, 1, 1, 18, kConditional}}},
          // A promise as a `while` test.
          {"while (Promise.resolve()) {}", {{"conditional", 1, 8, 1, 25, kConditional}}},
          // A promise negated in a condition.
          {R"(
if (!Promise.resolve()) {
}
)",
           {{"conditional", 2, 6, 2, 23, kConditional}}},
          // A promise as the left operand of a control-flow `||`.
          {"Promise.resolve() || false;", {{"conditional", 1, 1, 1, 18, kConditional}}},
          // An async callback where a void-returning one is expected.
          {R"(
const fnWithCallback = (arg: string, cb: (err: any, res: string) => void) => {
  cb(null, arg);
};

fnWithCallback('val', (err, res) => Promise.resolve(res));
)",
           {{"voidReturnArgument", 6, 23, 6, 57, kArgument}}},
          // An async function assigned to a void-returning variable.
          {R"(
const f: () => void = async () => {
  return 0;
};
)",
           {{"voidReturnVariable", 2, 23, 4, 2, kVariable}}},
          // An async function assigned to a void-returning variable.
          {R"(
let f: () => void;
f = async () => {
  return 3;
};
)",
           {{"voidReturnVariable", 3, 5, 5, 2, kVariable}}},
          // Spreading a promise into an object.
          {"console.log({ ...Promise.resolve({ key: 42 }) });",
           {{"spread", 1, 18, 1, 46, kSpread}}},
      },
      "basic");
}
