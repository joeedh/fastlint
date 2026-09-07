#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST_TAGGED(rules_no_unsafe_argument, cases, "integration")
{
  test::runTypedRuleTests(
      rules::kNoUnsafeArgument,
      {
          {R"(
doesNotExist(1 as any);
)"},
          {R"(
const foo = 1;
foo(1 as any);
)"},
          {R"(
declare function foo(arg: number): void;
foo(1, 1 as any, 2 as any);
)"},
          {R"(
declare function foo(arg: number, arg2: string): void;
foo(1, 'a');
)"},
          {R"(
declare function foo(arg: any): void;
foo(1 as any);
)"},
          {R"(
declare function foo(arg: unknown): void;
foo(1 as any);
)"},
          {R"(
declare function foo(...arg: number[]): void;
foo(1, 2, 3);
)"},
          {R"(
declare function foo(...arg: any[]): void;
foo(1, 2, 3, 4 as any);
)"},
          {R"(
declare function foo(arg: number, arg2: number): void;
const x = [1, 2] as const;
foo(...x);
)"},
          {R"(
declare function foo(arg: any, arg2: number): void;
const x = [1 as any, 2] as const;
foo(...x);
)"},
          {R"(
declare function foo(arg1: string, arg2: string): void;
const x: string[] = [];
foo(...x);
)"},
          {R"(
function foo(arg1: number, arg2: number) {}
foo(...([1, 1, 1] as [number, number, number]));
)"},
          {R"(
declare function foo(arg1: Set<string>, arg2: Map<string, string>): void;

const x = [new Map<string, string>()] as const;
foo(new Set<string>(), ...x);
)"},
          {R"(
declare function foo(arg1: unknown, arg2: Set<unknown>, arg3: unknown[]): void;
foo(1 as any, new Set<any>(), [] as any[]);
)"},
          {R"(
declare function foo(...params: [number, string, any]): void;
foo(1, 'a', 1 as any);
)"},
          {R"(
declare function foo<E extends string[]>(...params: E): void;

foo('a', 'b', 1 as any);
)"},
          {R"(
declare function toHaveBeenCalledWith<E extends any[]>(...params: E): void;
toHaveBeenCalledWith(1 as any);
)"},
          {R"(
declare function acceptsMap(arg: Map<string, string>): void;
acceptsMap(new Map());
)"},
          {R"(
type T = [number, T[]];
declare function foo(t: T): void;
declare const t: T;

foo(t);
)"},
          {R"(
type T = Array<T>;
declare function foo<T>(t: T): T;
const t: T = [];
foo(t);
)"},
          {R"(
function foo(templates: TemplateStringsArray) {}
foo``;
)"},
          {R"(
function foo(templates: TemplateStringsArray, arg: any) {}
foo`${1 as any}`;
)"},
          {R"(
declare function foo(...args: any): void;
foo(1 as any);
)"},
      },
      {
          {R"(
declare function foo(...args: [string, string]): void;

declare const spread: [string, ...string[]];
foo(...spread, 1 as any);
)",
           {{"unsafeArgument", 5, 16, 5, 24}}},
          {R"(
declare function foo(...args: [string, ...string[]]): void;

foo('a', 'b', 1 as any);
)",
           {{"unsafeArgument", 4, 15, 4, 23}}},
          {R"(
declare function foo(arg: number): void;
foo(1 as any);
)",
           {{"unsafeArgument",
             3,
             5,
             3,
             13,
             "Unsafe argument of type `any` assigned to a parameter of type `number`."}}},
          {R"(
declare function foo(arg: number): void;
foo(error);
)",
           {{"unsafeArgument",
             3,
             5,
             3,
             10,
             "Unsafe argument of type error typed assigned to a parameter of type "
             "`number`."}}},
          {R"(
declare function foo(arg1: number, arg2: string): void;
foo(1, 1 as any);
)",
           {{"unsafeArgument",
             3,
             8,
             3,
             16,
             "Unsafe argument of type `any` assigned to a parameter of type `string`."}}},
          {R"(
declare function foo(...arg: number[]): void;
foo(1, 2, 3, 1 as any);
)",
           {{"unsafeArgument",
             3,
             14,
             3,
             22,
             "Unsafe argument of type `any` assigned to a parameter of type `number`."}}},
          {R"(
declare function foo(arg: string, ...arg: number[]): void;
foo(1 as any, 1 as any);
)",
           {{"unsafeArgument",
             3,
             5,
             3,
             13,
             "Unsafe argument of type `any` assigned to a parameter of type `string`."},
            {"unsafeArgument",
             3,
             15,
             3,
             23,
             "Unsafe argument of type `any` assigned to a parameter of type `number`."}}},
          {R"(
declare function foo(arg1: string, arg2: number): void;

foo(...(x as any));
)",
           {{"unsafeSpread", 4, 5, 4, 18}}},
          {R"(
declare function foo(arg1: string, arg2: number): void;

foo(...(x as any[]));
)",
           {{"unsafeArraySpread",
             4,
             5,
             4,
             20,
             "Unsafe spread of an `any[]` array type."}}},
          {R"(
declare function foo(arg1: string, arg2: number): void;

declare const errors: error[];

foo(...errors);
)",
           {{"unsafeArraySpread", 6, 5, 6, 14, "Unsafe spread of an error array type."}}},
          {R"(
declare function foo(arg1: string, arg2: number): void;

const x = ['a', 1 as any] as const;
foo(...x);
)",
           {{"unsafeTupleSpread",
             5,
             5,
             5,
             9,
             "Unsafe spread of a tuple type. The argument is of type `any` and is "
             "assigned "
             "to a parameter of type `number`."}}},
          {R"(
declare function foo(arg1: string, arg2: number): void;

const x = ['a', error] as const;
foo(...x);
)",
           {{"unsafeTupleSpread",
             5,
             5,
             5,
             9,
             "Unsafe spread of a tuple type. The argument is error typed and is assigned "
             "to a parameter of type `number`."}}},
          {R"(
declare function foo(arg1: string, arg2: number): void;
foo(...(['foo', 1, 2] as [string, any, number]));
)",
           {{"unsafeTupleSpread", 3, 5, 3, 48}}},
          {R"(
declare function foo(arg1: string, arg2: number, arg2: string): void;

const x = [1] as const;
foo('a', ...x, 1 as any);
)",
           {{"unsafeArgument",
             5,
             16,
             5,
             24,
             "Unsafe argument of type `any` assigned to a parameter of type `string`."}}},
          {R"(
declare function foo(arg1: string, arg2: number, ...rest: string[]): void;

const x = [1, 2] as [number, ...number[]];
foo('a', ...x, 1 as any);
)",
           {{"unsafeArgument",
             5,
             16,
             5,
             24,
             "Unsafe argument of type `any` assigned to a parameter of type `string`."}}},
          {R"(
declare function foo(arg1: Set<string>, arg2: Map<string, string>): void;

const x = [new Map<any, string>()] as const;
foo(new Set<any>(), ...x);
)",
           {{"unsafeArgument",
             5,
             5,
             5,
             19,
             "Unsafe argument of type `Set<any>` assigned to a parameter of type "
             "`Set<string>`."},
            {"unsafeTupleSpread",
             5,
             21,
             5,
             25,
             "Unsafe spread of a tuple type. The argument is of type `Map<any, string>` "
             "and "
             "is assigned to a parameter of type `Map<string, string>`."}}},
          {R"(
declare function foo(...params: [number, string, any]): void;
foo(1 as any, 'a' as any, 1 as any);
)",
           {{"unsafeArgument", 3, 5, 3, 13}, {"unsafeArgument", 3, 15, 3, 25}}},
          {R"(
declare function foo(param1: string, ...params: [number, string, any]): void;
foo('a', 1 as any, 'a' as any, 1 as any);
)",
           {{"unsafeArgument", 3, 10, 3, 18}, {"unsafeArgument", 3, 20, 3, 30}}},
          {R"(
type T = [number, T[]];
declare function foo(t: T): void;
declare const t: T;
foo(t as any);
)",
           {{"unsafeArgument",
             5,
             5,
             5,
             13,
             "Unsafe argument of type `any` assigned to a parameter of type `T`."}}},
          {R"(
function foo(
  templates: TemplateStringsArray,
  arg1: number,
  arg2: any,
  arg3: string,
) {}
declare const arg: any;
foo<number>`${arg}${arg}${arg}`;
)",
           {{"unsafeArgument", 9, 15, 9, 18}, {"unsafeArgument", 9, 27, 9, 30}}},
          {R"(
function foo(templates: TemplateStringsArray, arg: number) {}
declare const arg: any;
foo`${arg}`;
)",
           {{"unsafeArgument", 4, 7, 4, 10}}},
          {R"(
type T = [number, T[]];
function foo(templates: TemplateStringsArray, arg: T) {}
declare const arg: any;
foo`${arg}`;
)",
           {{"unsafeArgument",
             5,
             7,
             5,
             10,
             "Unsafe argument of type `any` assigned to a parameter of type `T`."}}},
      });
}
