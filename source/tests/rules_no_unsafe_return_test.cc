#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST_TAGGED(rules_no_unsafe_return, cases, "integration")
{
  test::runTypedRuleTests(
      rules::kNoUnsafeReturn,
      {
          {R"(
function foo() {
  return;
}
)"},
          {R"(
function foo() {
  return 1;
}
)"},
          {R"(
function foo() {
  return '';
}
)"},
          {R"(
function foo() {
  return true;
}
)"},
          {R"(
function foo() {
  return [];
}
)"},
          {R"(
function foo(): any {
  return {} as any;
}
)"},
          {R"(
declare function foo(arg: () => any): void;
foo((): any => 'foo' as any);
)"},
          {R"(
declare function foo(arg: null | (() => any)): void;
foo((): any => 'foo' as any);
)"},
          {R"(
function foo(): any[] {
  return [] as any[];
}
)"},
          {R"(
function foo(): Set<any> {
  return new Set<any>();
}
)"},
          {R"(
async function foo(): Promise<any> {
  return Promise.resolve({} as any);
}
)"},
          {R"(
async function foo(): Promise<any> {
  return {} as any;
}
)"},
          {R"(
function foo(): object {
  return Promise.resolve({} as any);
}
)"},
          {R"(
function foo(): ReadonlySet<number> {
  return new Set<any>();
}
)"},
          {R"(
function foo(): Set<number> {
  return new Set([1]);
}
)"},
          {R"(
type Foo<T = number> = { prop: T };
function foo(): Foo {
  return { prop: 1 } as Foo<number>;
}
)"},
          {R"(
type Foo = { prop: any };
function foo(): Foo {
  return { prop: '' } as Foo;
}
)"},
          {R"(
function fn<T extends any>(x: T) {
  return x;
}
)"},
          {R"(
function fn<T extends any>(x: T): unknown {
  return x as any;
}
)"},
          {R"(
function fn<T extends any>(x: T): unknown[] {
  return x as any[];
}
)"},
          {R"(
function fn<T extends any>(x: T): Set<unknown> {
  return x as Set<any>;
}
)"},
          {R"(
async function fn<T extends any>(x: T): Promise<unknown> {
  return x as any;
}
)"},
          {R"(
function fn<T extends any>(x: T): Promise<unknown> {
  return Promise.resolve(x as any);
}
)"},
          {R"(
type Wrapper<T> = { inner: T };
type Extractor<D extends Wrapper<any>> = D extends Wrapper<infer V> ? V : never;
const fn =
  <D extends Wrapper<any>>(foo: Extractor<D>) =>
  () =>
    foo;
)"},
          {R"(
function test(): Map<string, string> {
  return new Map();
}
)"},
          {R"(
function foo(): any {
  return [] as any[];
}
)"},
          {R"(
function foo(): unknown {
  return [] as any[];
}
)"},
          {R"(
declare const value: Promise<any>;
function foo() {
  return value;
}
)"},
          {"const foo: (() => void) | undefined = () => 1;"},
          {R"(
class Foo {
  public foo(): this {
    return this;
  }

  protected then(resolve: () => void): void {
    resolve();
  }
}
)"},
      },
      {
          {R"(
function foo() {
  return 1 as any;
}
)",
           {{"unsafeReturn", 3, 3, 3, 19, "Unsafe return of a value of type `any`."}}},
          {R"(
function foo() {
  return Object.create(null);
}
)",
           {{"unsafeReturn", 3, 3, 3, 30}}},
          {R"(
const foo = () => {
  return 1 as any;
};
)",
           {{"unsafeReturn", 3, 3, 3, 19}}},
          {"const foo = () => Object.create(null);", {{"unsafeReturn", 1, 19, 1, 38}}},
          {R"(
function foo() {
  return [] as any[];
}
)",
           {{"unsafeReturn", 3, 3, 3, 22, "Unsafe return of a value of type `any[]`."}}},
          {R"(
function foo() {
  return [] as Array<any>;
}
)",
           {{"unsafeReturn", 3, 3, 3, 27, "Unsafe return of a value of type `any[]`."}}},
          {R"(
function foo() {
  return [] as readonly any[];
}
)",
           {{"unsafeReturn", 3, 3, 3, 31, "Unsafe return of a value of type `any[]`."}}},
          {R"(
function foo() {
  return [] as Readonly<any[]>;
}
)",
           {{"unsafeReturn", 3, 3, 3, 32, "Unsafe return of a value of type `any[]`."}}},
          {R"(
const foo = () => {
  return [] as any[];
};
)",
           {{"unsafeReturn", 3, 3, 3, 22}}},
          {"const foo = () => [] as any[];", {{"unsafeReturn", 1, 19, 1, 30}}},
          {R"(
function foo(): Set<string> {
  return new Set<any>();
}
)",
           {{"unsafeReturnAssignment",
             3,
             3,
             3,
             25,
             "Unsafe return of type `Set<any>` from function with return type "
             "`Set<string>`."}}},
          {R"(
function foo(): Map<string, string> {
  return new Map<string, any>();
}
)",
           {{"unsafeReturnAssignment",
             3,
             3,
             3,
             33,
             "Unsafe return of type `Map<string, any>` from function with return type "
             "`Map<string, string>`."}}},
          {R"(
function foo(): Set<string[]> {
  return new Set<any[]>();
}
)",
           {{"unsafeReturnAssignment", 3, 3, 3, 27}}},
          {R"(
function foo(): Set<Set<Set<string>>> {
  return new Set<Set<Set<any>>>();
}
)",
           {{"unsafeReturnAssignment", 3, 3, 3, 35}}},
          {R"(
type Fn = () => Set<string>;
const foo1: Fn = () => new Set<any>();
const foo2: Fn = function test() {
  return new Set<any>();
};
)",
           {{"unsafeReturnAssignment", 3, 24, 3, 38},
            {"unsafeReturnAssignment", 5, 3, 5, 25}}},
          {R"(
type Fn = () => Set<string>;
function receiver(arg: Fn) {}
receiver(() => new Set<any>());
receiver(function test() {
  return new Set<any>();
});
)",
           {{"unsafeReturnAssignment", 4, 16, 4, 30},
            {"unsafeReturnAssignment", 6, 3, 6, 25}}},
          {R"(
function foo() {
  return this;
}

function bar() {
  return () => this;
}
)",
           {{"unsafeReturnThis", 3, 3, 3, 15}, {"unsafeReturnThis", 7, 16, 7, 20}}},
          {R"(
declare function foo(arg: null | (() => any)): void;
foo(() => 'foo' as any);
)",
           {{"unsafeReturn", 3, 11, 3, 23}}},
          {R"(
let value: NotKnown;

function example() {
  return value;
}
)",
           {{"unsafeReturn", 5, 3, 5, 16, "Unsafe return of a value of type error."}}},
          {R"(
declare const value: any;
async function foo() {
  return value;
}
)",
           {{"unsafeReturn", 4, 3, 4, 16, "Unsafe return of a value of type `any`."}}},
          {R"(
declare const value: Promise<any>;
async function foo(): Promise<number> {
  return value;
}
)",
           {{"unsafeReturn",
             4,
             3,
             4,
             16,
             "Unsafe return of a value of type `Promise<any>`."}}},
          {R"(
async function foo(arg: number) {
  return arg as Promise<any>;
}
)",
           {{"unsafeReturn",
             3,
             3,
             3,
             30,
             "Unsafe return of a value of type `Promise<any>`."}}},
          {R"(
function foo(): Promise<any> {
  return {} as any;
}
)",
           {{"unsafeReturn", 3, 3, 3, 20, "Unsafe return of a value of type `any`."}}},
          {R"(
function foo(): Promise<object> {
  return {} as any;
}
)",
           {{"unsafeReturn", 3, 3, 3, 20}}},
          {R"(
async function foo(): Promise<object> {
  return Promise.resolve<any>({});
}
)",
           {{"unsafeReturn",
             3,
             3,
             3,
             35,
             "Unsafe return of a value of type `Promise<any>`."}}},
          {R"(
async function foo(): Promise<object> {
  return Promise.resolve<Promise<Promise<any>>>({} as Promise<any>);
}
)",
           {{"unsafeReturn", 3, 3, 3, 69}}},
          {R"(
async function foo(): Promise<object> {
  return {} as Promise<Promise<Promise<Promise<any>>>>;
}
)",
           {{"unsafeReturn", 3, 3, 3, 56}}},
          {R"(
async function foo() {
  return {} as Promise<Promise<Promise<Promise<any>>>>;
}
)",
           {{"unsafeReturn", 3, 3, 3, 56}}},
          {R"(
async function foo() {
  return {} as Promise<any> | Promise<object>;
}
)",
           {{"unsafeReturn", 3, 3, 3, 47}}},
          {R"(
async function foo() {
  return {} as Promise<any | object>;
}
)",
           {{"unsafeReturn", 3, 3, 3, 38}}},
          {R"(
async function foo() {
  return {} as Promise<any> & { __brand: 'any' };
}
)",
           {{"unsafeReturn", 3, 3, 3, 50}}},
          {R"(
interface Alias<T> extends Promise<any> {
  foo: 'bar';
}

declare const value: Alias<number>;
async function foo() {
  return value;
}
)",
           {{"unsafeReturn",
             8,
             3,
             8,
             16,
             "Unsafe return of a value of type `Promise<any>`."}}},
      },
      "loose");
}
