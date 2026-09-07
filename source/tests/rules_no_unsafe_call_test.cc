#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST_TAGGED(rules_no_unsafe_call, cases, "integration")
{
  test::runTypedRuleTests(
      rules::kNoUnsafeCall,
      {
          {R"(
function foo(x: () => void) {
  x();
}
)"},
          {R"(
function foo(x?: { a: () => void }) {
  x?.a();
}
)"},
          {R"(
function foo(x: { a?: () => void }) {
  x.a?.();
}
)"},
          {"new Map();"},
          {"String.raw`foo`;"},
          {"const x = import('./foo');"},
          {R"(
let foo: any = 23;
String(foo); // ERROR: Unsafe call of an any typed value
)"},
          {R"(
function foo<T extends any>(x: T) {
  x();
}
)"},
          {R"(
// create a scope since it's illegal to declare a duplicate identifier
// 'Function' in the global script scope.
{
  type Function = () => void;
  const notGlobalFunctionType: Function = (() => {}) as Function;
  notGlobalFunctionType();
}
)"},
          {R"(
interface SurprisinglySafe extends Function {
  (): string;
}
declare const safe: SurprisinglySafe;
safe();
)"},
          {R"(
interface CallGoodConstructBad extends Function {
  (): void;
}
declare const safe: CallGoodConstructBad;
safe();
)"},
          {R"(
interface ConstructSignatureMakesSafe extends Function {
  new (): ConstructSignatureMakesSafe;
}
declare const safe: ConstructSignatureMakesSafe;
new safe();
)"},
          {R"(
interface SafeWithNonVoidCallSignature extends Function {
  (): void;
  (x: string): string;
}
declare const safe: SafeWithNonVoidCallSignature;
safe();
)"},
          {R"(
new Function('lol');
)"},
          {R"(
Function('lol');
)"},
      },
      {
          {R"(
function foo(x: any) {
  x();
}
)",
           {{"unsafeCall", 3, 3, 3, 4, "Unsafe call of an `any` typed value."}}},
          {R"(
function foo(x: any) {
  x?.();
}
)",
           {{"unsafeCall", 3, 3, 3, 4}}},
          {R"(
function foo(x: any) {
  x.a.b.c.d.e.f.g();
}
)",
           {{"unsafeCall", 3, 3, 3, 18}}},
          {R"(
function foo(x: any) {
  x.a.b.c.d.e.f.g?.();
}
)",
           {{"unsafeCall", 3, 3, 3, 18}}},
          {R"(
function foo(x: { a: any }) {
  x.a();
}
)",
           {{"unsafeCall", 3, 3, 3, 6}}},
          {R"(
function foo(x: { a: any }) {
  x?.a();
}
)",
           {{"unsafeCall", 3, 3, 3, 7}}},
          {R"(
function foo(x: { a: any }) {
  x.a?.();
}
)",
           {{"unsafeCall", 3, 3, 3, 6}}},
          {R"(
function foo(x: any) {
  new x();
}
)",
           {{"unsafeNew", 3, 3, 3, 10}}},
          {R"(
function foo(x: { a: any }) {
  new x.a();
}
)",
           {{"unsafeNew", 3, 3, 3, 12}}},
          {R"(
function foo(x: any) {
  x`foo`;
}
)",
           {{"unsafeTemplateTag", 3, 3, 3, 4}}},
          {R"(
function foo(x: { tag: any }) {
  x.tag`foo`;
}
)",
           {{"unsafeTemplateTag", 3, 3, 3, 8}}},
          {R"(
const methods = {
  methodA() {
    return this.methodB()
  },
  methodB() {
    return true
  },
  methodC() {
    return this()
  }
};
)",
           {{"unsafeCallThis", 4, 12, 4, 24}, {"unsafeCallThis", 10, 12, 10, 16}}},
          {R"(
const t: Function = () => {};
t();
)",
           {{"unsafeCall", 3, 1, 3, 2, "Unsafe call of a `Function` typed value."}}},
          {R"(
const f: Function = () => {};
f`oo`;
)",
           {{"unsafeTemplateTag", 3, 1, 3, 2}}},
          {R"(
declare const maybeFunction: unknown;
if (typeof maybeFunction === 'function') {
  maybeFunction('call', 'with', 'any', 'args');
}
)",
           {{"unsafeCall", 4, 3, 4, 16}}},
          {R"(
interface Unsafe extends Function {}
declare const unsafe: Unsafe;
unsafe();
)",
           {{"unsafeCall", 4, 1, 4, 7}}},
          {R"(
interface Unsafe extends Function {}
declare const unsafe: Unsafe;
unsafe`bad`;
)",
           {{"unsafeTemplateTag", 4, 1, 4, 7}}},
          {R"(
interface Unsafe extends Function {}
declare const unsafe: Unsafe;
new unsafe();
)",
           {{"unsafeNew", 4, 1, 4, 13}}},
          {R"(
interface UnsafeToConstruct extends Function {
  (): void;
}
declare const unsafe: UnsafeToConstruct;
new unsafe();
)",
           {{"unsafeNew", 6, 1, 6, 13}}},
          {R"(
interface StillUnsafe extends Function {
  property: string;
}
declare const unsafe: StillUnsafe;
unsafe();
)",
           {{"unsafeCall", 6, 1, 6, 7}}},
          {R"(
let value: NotKnown;
value();
)",
           {{"errorCall", 3, 1, 3, 6}}},
          {R"(
let value: NotKnown;
value``;
)",
           {{"errorTemplateTag", 3, 1, 3, 6}}},
          {R"(
let value: NotKnown;
new value();
)",
           {{"errorNew", 3, 1, 3, 12}}},
          {R"(
function callThis(this: NotKnown) {
  this();
  this.method();
}
)",
           {{"errorCallThis", 3, 3, 3, 7}, {"errorCallThis", 4, 3, 4, 14}}},
      },
      "loose");
}
