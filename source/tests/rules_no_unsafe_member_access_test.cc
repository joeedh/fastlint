#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST_TAGGED(rules_no_unsafe_member_access, cases, "integration")
{
  const char *allowChaining = R"([{"allowOptionalChaining": true}])";
  test::runTypedRuleTests(
      rules::kNoUnsafeMemberAccess,
      {
          {R"(
function foo(x: { a: number }, y: any) {
  x[y++];
}
)"},
          {R"(
function foo(x: { a: number }) {
  x.a;
}
)"},
          {R"(
function foo(x?: { a: number }) {
  x?.a;
}
)"},
          {R"(
function foo(x: { a: number }) {
  x['a'];
}
)"},
          {R"(
function foo(x?: { a: number }) {
  x?.['a'];
}
)"},
          {R"(
function foo(x: { a: number }, y: string) {
  x[y];
}
)"},
          {R"(
function foo(x?: { a: number }, y: string) {
  x?.[y];
}
)"},
          {R"(
function foo(x: string[]) {
  x[1];
}
)"},
          {R"(
class B implements FG.A {}
)"},
          {R"(
interface B extends FG.A {}
)"},
          {R"(
class B implements F.S.T.A {}
)"},
          {R"(
interface B extends F.S.T.A {}
)"},
          {R"(
function foo(x?: { a: number }) {
  x?.a;
}
)",
           allowChaining},
          {R"(
function foo(x?: { a: number }, y: string) {
  x?.[y];
}
)",
           allowChaining},
          {R"(
function foo(x: { a: number }, y: 'a') {
  x?.[y];
}
)",
           allowChaining},
          {R"(
function foo(x: { a: number }, y: NotKnown) {
  x?.[y];
}
)",
           allowChaining},
      },
      {
          {R"(
function foo(x: any) {
  x.a;
}
)",
           {{"unsafeMemberExpression",
             3,
             5,
             3,
             6,
             "Unsafe member access .a on an `any` value."}}},
          {R"(
function foo(x: any) {
  x.a.b.c.d.e.f.g;
}
)",
           {{"unsafeMemberExpression", 3, 5, 3, 6}}},
          {R"(
function foo(x: { a: any }) {
  x.a.b.c.d.e.f.g;
}
)",
           {{"unsafeMemberExpression",
             3,
             7,
             3,
             8,
             "Unsafe member access .b on an `any` value."}}},
          {R"(
function foo(x: any) {
  x['a'];
}
)",
           {{"unsafeMemberExpression",
             3,
             5,
             3,
             8,
             "Unsafe member access ['a'] on an `any` value."}}},
          {R"(
function foo(x: any) {
  x['a']['b']['c'];
}
)",
           {{"unsafeMemberExpression", 3, 5, 3, 8}}},
          {R"(
let value: NotKnown;

value.property;
)",
           {{"errorMemberExpression",
             4,
             7,
             4,
             15,
             "Unsafe member access .property on a type that cannot be resolved."}}},
          {R"(
function foo(x: { a: number }, y: any) {
  x[y];
}
)",
           {{"unsafeComputedMemberAccess",
             3,
             5,
             3,
             6,
             "Computed name [y] resolves to an `any` value."}}},
          {R"(
function foo(x?: { a: number }, y: any) {
  x?.[y];
}
)",
           {{"unsafeComputedMemberAccess", 3, 7, 3, 8}}},
          {R"(
function foo(x: { a: number }, y: any) {
  x[(y += 1)];
}
)",
           {{"unsafeComputedMemberAccess",
             3,
             6,
             3,
             12,
             "Computed name [y += 1] resolves to an `any` value."}}},
          {R"(
function foo(x: { a: number }, y: any) {
  x[1 as any];
}
)",
           {{"unsafeComputedMemberAccess",
             3,
             5,
             3,
             13,
             "Computed name [1 as any] resolves to an `any` value."}}},
          {R"(
function foo(x: { a: number }, y: any) {
  x[y()];
}
)",
           {{"unsafeComputedMemberAccess", 3, 5, 3, 8}}},
          {R"(
function foo(x: string[], y: any) {
  x[y];
}
)",
           {{"unsafeComputedMemberAccess", 3, 5, 3, 6}}},
          {R"(
function foo(x: { a: number }, y: NotKnown) {
  x[y];
}
)",
           {{"errorComputedMemberAccess", 3, 5, 3, 6}}},
          {R"(
const methods = {
  methodA() {
    return this.methodB()
  },
  methodB() {
    const getProperty = () => Math.random() > 0.5 ? 'methodB' : 'methodC'
    return this[getProperty()]()
  },
  methodC() {
    return true
  },
  methodD() {
    return (this?.methodA)?.()
  }
};
)",
           {{"unsafeThisMemberExpression", 4, 17, 4, 24},
            {"unsafeThisMemberExpression", 8, 17, 8, 30},
            {"unsafeThisMemberExpression", 14, 19, 14, 26}}},
          {R"(
class C {
  getObs$: any;
  getPopularDepartments(): void {
    this.getObs$.pipe().subscribe(res => {
      console.log(res);
    });
  }
}
)",
           {{"unsafeMemberExpression", 5, 18, 5, 22},
            {"unsafeMemberExpression", 5, 25, 5, 34}}},
          {R"(
let value: any;

value?.middle.inner;
)",
           {{"unsafeMemberExpression", 4, 15, 4, 20}},
           nullptr,
           allowChaining},
          {R"(
let value: any;

value?.outer.middle.inner;
)",
           {{"unsafeMemberExpression", 4, 14, 4, 20}},
           nullptr,
           allowChaining},
          {R"(
let value: any;

value.outer?.middle.inner;
)",
           {{"unsafeMemberExpression", 4, 7, 4, 12},
            {"unsafeMemberExpression", 4, 21, 4, 26}},
           nullptr,
           allowChaining},
          {R"(
let value: any;

value.outer.middle?.inner;
)",
           {{"unsafeMemberExpression", 4, 7, 4, 12}},
           nullptr,
           allowChaining},
          {R"(
function foo(x: { a: number }, y: NotKnown) {
  x[y];
}
)",
           {{"errorComputedMemberAccess", 3, 5, 3, 6}},
           nullptr,
           allowChaining},
      },
      "loose");
}
