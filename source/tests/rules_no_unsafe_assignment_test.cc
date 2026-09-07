#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST_TAGGED(rules_no_unsafe_assignment, cases, "integration")
{
  test::runTypedRuleTests(
      rules::kNoUnsafeAssignment,
      {
          {"const x = 1;"},
          {"const x: number = 1;"},
          {R"(
const x = 1,
  y = 1;
)"},
          {"let x;"},
          {R"(
let x = 1,
  y;
)"},
          {"function foo(a = 1) {}"},
          {R"(
class Foo {
  constructor(private a = 1) {}
}
)"},
          {R"(
class Foo {
  private a = 1;
}
)"},
          {R"(
class Foo {
  accessor a = 1;
}
)"},
          {"const x: Set<string> = new Set();"},
          {"const x: Set<string> = new Set<string>();"},
          {"const [x] = [1];"},
          {"const [x, y] = [1, 2] as number[];"},
          {"const [x, ...y] = [1, 2, 3, 4, 5];"},
          {"const [x, ...y] = [1];"},
          {"const [{ ...x }] = [{ x: 1 }] as [{ x: any }];"},
          {"function foo(x = 1) {}"},
          {"function foo([x] = [1]) {}"},
          {"function foo([x, ...y] = [1, 2, 3, 4, 5]) {}"},
          {"function foo([x, ...y] = [1]) {}"},
          {"const x = new Set<any>();"},
          {"const x = { y: 1 };"},
          {"const x = { y = 1 };"},
          {"const x = { y(){} };"},
          {"const x: { y: number } = { y: 1 };"},
          {"const x = [...[1, 2, 3]];"},
          {"const [{ [`x${1}`]: x }] = [{ [`x`]: 1 }] as [{ [`x`]: any }];"},
          {R"(
type T = [string, T[]];
const test: T = ['string', []] as T;
)"},
          {R"(
type Props = { a: string };
declare function Foo(props: Props): never;
<Foo a={'foo'} />;
)",
           nullptr,
           "casex.tsx"},
          {R"(
declare function Foo(props: { a: string }): never;
<Foo a="foo" />;
)",
           nullptr,
           "casex.tsx"},
          {R"(
declare function Foo(props: { a: string }): never;
<Foo a={} />;
)",
           nullptr,
           "casex.tsx"},
          {"const x: unknown = y as any;"},
          {"const x: unknown[] = y as any[];"},
          {"const x: Set<unknown> = y as Set<any>;"},
          {"const x: Map<string, string> = new Map();"},
          {R"(
type Foo = { bar: unknown };
const bar: any = 1;
const foo: Foo = { bar };
)"},
      },
      {
          {"const x = 1 as any;",
           {{"anyAssignment", 1, 7, 1, 19, "Unsafe assignment of an `any` value."}}},
          {R"(
const x = 1 as any,
  y = 1;
)",
           {{"anyAssignment", 2, 7, 2, 19}}},
          {"function foo(a = 1 as any) {}", {{"anyAssignment", 1, 14, 1, 26}}},
          {R"(
class Foo {
  constructor(private a = 1 as any) {}
}
)",
           {{"anyAssignment", 3, 23, 3, 35}}},
          {R"(
class Foo {
  private a = 1 as any;
}
)",
           {{"anyAssignment", 3, 3, 3, 24}}},
          {R"(
class Foo {
  accessor a = 1 as any;
}
)",
           {{"anyAssignment", 3, 3, 3, 25}}},
          {R"(
const [x] = spooky;
)",
           {{"anyAssignment",
             2,
             7,
             2,
             19,
             "Unsafe assignment of an error typed value."}}},
          {R"(
const [[[x]]] = [spooky];
)",
           {{"unsafeArrayPatternFromTuple",
             2,
             8,
             2,
             13,
             "Unsafe array destructuring of a tuple element with an error typed "
             "value."}}},
          {R"(
const {
  x: { y: z },
} = { x: spooky };
)",
           {{"unsafeObjectPattern",
             3,
             6,
             3,
             14,
             "Unsafe object destructuring of a property with an error typed value."},
            {"anyAssignment",
             4,
             7,
             4,
             16,
             "Unsafe assignment of an error typed value."}}},
          {R"(
let value: number;

value = spooky;
)",
           {{"anyAssignment",
             4,
             1,
             4,
             15,
             "Unsafe assignment of an error typed value."}}},
          {R"(
const [x] = 1 as any;
)",
           {{"anyAssignment", 2, 7, 2, 21}}},
          {R"(
const [x] = [] as any[];
)",
           {{"unsafeArrayPattern",
             2,
             7,
             2,
             10,
             "Unsafe array destructuring of an `any` array value."}}},
          {"const x: Set<string> = new Set<any>();",
           {{"unsafeAssignment",
             1,
             7,
             1,
             38,
             "Unsafe assignment of type `Set<any>` to a variable of type "
             "`Set<string>`."}}},
          {"const x: Map<string, string> = new Map<string, any>();",
           {{"unsafeAssignment",
             1,
             7,
             1,
             54,
             "Unsafe assignment of type `Map<string, any>` to a variable of type "
             "`Map<string, string>`."}}},
          {"const x: Set<string[]> = new Set<any[]>();",
           {{"unsafeAssignment",
             1,
             7,
             1,
             42,
             "Unsafe assignment of type `Set<any[]>` to a variable of type "
             "`Set<string[]>`."}}},
          {"const x: Set<Set<Set<string>>> = new Set<Set<Set<any>>>();",
           {{"unsafeAssignment",
             1,
             7,
             1,
             58,
             "Unsafe assignment of type `Set<Set<Set<any>>>` to a variable of type "
             "`Set<Set<Set<string>>>`."}}},
          {"const [x] = [1] as [any];", {{"unsafeArrayPatternFromTuple", 1, 8, 1, 9}}},
          {"function foo([x] = [1] as [any]) {}",
           {{"unsafeArrayPatternFromTuple", 1, 15, 1, 16}}},
          {"[x] = [1] as [any];", {{"unsafeArrayPatternFromTuple", 1, 2, 1, 3}}},
          {"const [[[[x]]]] = [[[[1 as any]]]];",
           {{"unsafeArrayPatternFromTuple", 1, 11, 1, 12}}},
          {"function foo([[[[x]]]] = [[[[1 as any]]]]) {}",
           {{"unsafeArrayPatternFromTuple", 1, 18, 1, 19}}},
          {"[[[[x]]]] = [[[[1 as any]]]];",
           {{"unsafeArrayPatternFromTuple", 1, 5, 1, 6}}},
          {"const [[[[x]]]] = [1 as any];",
           {{"unsafeArrayPatternFromTuple", 1, 8, 1, 15}}},
          {"function foo([[[[x]]]] = [1 as any]) {}",
           {{"unsafeArrayPatternFromTuple", 1, 15, 1, 22}}},
          {"const [{ x }] = [{ x: 1 }] as [{ x: any }];",
           {{"unsafeObjectPattern", 1, 10, 1, 11}}},
          {"function foo([{ x }] = [{ x: 1 }] as [{ x: any }]) {}",
           {{"unsafeObjectPattern", 1, 17, 1, 18}}},
          {"[{ x }] = [{ x: 1 }] as [{ x: any }];",
           {{"unsafeObjectPattern", 1, 4, 1, 5}}},
          {"const [{ ['x']: x }] = [{ ['x']: 1 }] as [{ ['x']: any }];",
           {{"unsafeObjectPattern", 1, 17, 1, 18}}},
          {"function foo([{ ['x']: x }] = [{ ['x']: 1 }] as [{ ['x']: any }]) {}",
           {{"unsafeObjectPattern", 1, 24, 1, 25}}},
          {"[{ ['x']: x }] = [{ ['x']: 1 }] as [{ ['x']: any }];",
           {{"unsafeObjectPattern", 1, 11, 1, 12}}},
          {"const [{ [`x`]: x }] = [{ [`x`]: 1 }] as [{ [`x`]: any }];",
           {{"unsafeObjectPattern", 1, 17, 1, 18}}},
          {"function foo([{ [`x`]: x }] = [{ [`x`]: 1 }] as [{ [`x`]: any }]) {}",
           {{"unsafeObjectPattern", 1, 24, 1, 25}}},
          {"[{ [`x`]: x }] = [{ [`x`]: 1 }] as [{ [`x`]: any }];",
           {{"unsafeObjectPattern", 1, 11, 1, 12}}},
          {"[[[[x]]]] = [1 as any];", {{"unsafeAssignment", 1, 1, 1, 23}}},
          {R"(
const x = [...(1 as any)];
)",
           {{"unsafeArraySpread",
             2,
             12,
             2,
             25,
             "Unsafe spread of an `any` value in an array."}}},
          {R"(
const x = [...([] as any[])];
)",
           {{"unsafeArraySpread", 2, 12, 2, 28}}},
          {"const { x } = { x: 1 } as { x: any };",
           {{"unsafeObjectPattern", 1, 9, 1, 10}}},
          {"function foo({ x } = { x: 1 } as { x: any }) {}",
           {{"unsafeObjectPattern", 1, 16, 1, 17}}},
          {"({ x } = { x: 1 } as { x: any });", {{"unsafeObjectPattern", 1, 4, 1, 5}}},
          {"const { x: y } = { x: 1 } as { x: any };",
           {{"unsafeObjectPattern", 1, 12, 1, 13}}},
          {"function foo({ x: y } = { x: 1 } as { x: any }) {}",
           {{"unsafeObjectPattern", 1, 19, 1, 20}}},
          {"({ x: y } = { x: 1 } as { x: any });", {{"unsafeObjectPattern", 1, 7, 1, 8}}},
          {R"(
const {
  x: { y },
} = { x: { y: 1 } } as { x: { y: any } };
)",
           {{"unsafeObjectPattern", 3, 8, 3, 9}}},
          {"function foo({ x: { y } } = { x: { y: 1 } } as { x: { y: any } }) {}",
           {{"unsafeObjectPattern", 1, 21, 1, 22}}},
          {R"(
({
  x: { y },
} = { x: { y: 1 } } as { x: { y: any } });
)",
           {{"unsafeObjectPattern", 3, 8, 3, 9}}},
          {R"(
const {
  x: [y],
} = { x: { y: 1 } } as { x: [any] };
)",
           {{"unsafeArrayPatternFromTuple", 3, 7, 3, 8}}},
          {"function foo({ x: [y] } = { x: { y: 1 } } as { x: [any] }) {}",
           {{"unsafeArrayPatternFromTuple", 1, 20, 1, 21}}},
          {R"(
({
  x: [y],
} = { x: { y: 1 } } as { x: [any] });
)",
           {{"unsafeArrayPatternFromTuple", 3, 7, 3, 8}}},
          {"const x = { y: 1 as any };", {{"anyAssignment", 1, 13, 1, 24}}},
          {"const x = { y: { z: 1 as any } };", {{"anyAssignment", 1, 18, 1, 29}}},
          {"const x: { y: Set<Set<Set<string>>> } = { y: new Set<Set<Set<any>>>() };",
           {{"unsafeAssignment",
             1,
             43,
             1,
             70,
             "Unsafe assignment of type `Set<Set<Set<any>>>` to a variable of type "
             "`Set<Set<Set<string>>>`."}}},
          {"const x = { ...(1 as any) };", {{"anyAssignment", 1, 7, 1, 28}}},
          {R"(
type Props = { a: string };
declare function Foo(props: Props): never;
<Foo a={1 as any} />;
)",
           {{"anyAssignment", 4, 9, 4, 17}},
           nullptr,
           nullptr,
           "casex.tsx"},
          {R"(
function foo() {
  const bar = this;
}
)",
           {{"anyAssignmentThis", 3, 9, 3, 19}}},
          {R"(
type T = [string, T[]];
const test: T = ['string', []] as any;
)",
           {{"anyAssignment", 3, 7, 3, 38}}},
          {R"(
type Foo = { bar: number };
const bar: any = 1;
const foo: Foo = { bar };
)",
           {{"anyAssignment", 4, 20, 4, 23}}},
      },
      "loose");
}
