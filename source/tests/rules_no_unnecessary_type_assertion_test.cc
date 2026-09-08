#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

namespace {
constexpr const char *kUnchanged =
    "This assertion is unnecessary since it does not change the type of the "
    "expression.";
constexpr const char *kContextual =
    "This assertion is unnecessary since the receiver accepts the original type of "
    "the expression.";
} // namespace

TEST_TAGGED(rules_no_unnecessary_type_assertion, cases, "integration")
{
  test::runTypedRuleTests(
      rules::kNoUnnecessaryTypeAssertion,
      {
          // No assertion at all.
          {R"(
declare const a: number;
const b = a;
)"},
          // A nullable operand whose context rejects the nullable member.
          {R"(
declare const x: number | undefined;
let foo: number = x!;
)"},
          // `unknown` where the context is not `unknown`.
          {R"(
const foo: unknown = {};
const baz: {} = foo!;
)"},
          // A variable with no initializer may be read before assignment.
          {R"(
let x: number;
x!;
)"},
          // The right-hand side of a plain assignment is left alone.
          {R"(
let x: number | undefined = undefined;
let y: number | undefined = undefined;
y = x!;
)"},
          // A property value carries no contextual type, so a `!` on a nullable
          // operand there is not reported even when the property is optional.
          {R"(
declare const m: Map<string, string>;
declare const k: string;
const o: { file?: string } = { file: m.get(k)! };
)"},
      },
      {
          // A call result that is already non-nullable.
          {R"(
declare function foo(): number;
const a = foo()!;
)",
           {{"unnecessaryAssertion", 3, 11, 3, 17, kUnchanged}},
           R"(
declare function foo(): number;
const a = foo();
)"},
          {R"(
const b = new Date()!;
)",
           {{"unnecessaryAssertion", 2, 11, 2, 22, kUnchanged}},
           R"(
const b = new Date();
)"},
          // The assertion on an assignment target never changes the value's type.
          {R"(
let x: number | undefined;
let y: number | undefined;
y = x!;
y! = 0;
)",
           {{"contextuallyUnnecessary", 5, 1, 5, 3, kContextual}},
           R"(
let x: number | undefined;
let y: number | undefined;
y = x!;
y = 0;
)"},
          // A nullable operand whose context accepts the same nullable member.
          {R"(
const x: number | null = null;
const y: number | null = x!;
)",
           {{"contextuallyUnnecessary", 3, 26, 3, 28, kContextual}},
           R"(
const x: number | null = null;
const y: number | null = x;
)"},
      },
      "basic");
}
