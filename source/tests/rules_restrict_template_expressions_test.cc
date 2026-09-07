#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST_TAGGED(rules_restrict_template_expressions, cases, "integration")
{
  test::runTypedRuleTests(
      rules::kRestrictTemplateExpressions,
      {
          // Base cases.
          {R"(const msg = `arg = ${'foo'}`;)"},
          {R"(
const arg = 'foo';
const msg = `arg = ${arg}`;
)"},
          {R"(
const arg = 'foo';
const msg = `arg = ${arg || 'default'}`;
)"},
          {R"(
function test<T extends string>(arg: T) {
  return `arg = ${arg}`;
}
)"},
          {R"(
function test<T extends string & { _kind: 'MyBrandedString' }>(arg: T) {
  return `arg = ${arg}`;
}
)"},
          // A tagged template is not checked.
          {R"(tag`arg = ${null}`;)"},
          {R"(
const arg = {};
tag`arg = ${arg}`;
)"},
          // allowNumber (default on).
          {R"(
const arg = 123;
const msg = `arg = ${arg}`;
)"},
          {R"(
const arg = 123n;
const msg = `arg = ${arg || 'default'}`;
)"},
          {R"(
function test<T extends number>(arg: T) {
  return `arg = ${arg}`;
}
)"},
          {R"(
function test<T extends bigint>(arg: T) {
  return `arg = ${arg}`;
}
)"},
          {R"(
function test<T extends string | number>(arg: T) {
  return `arg = ${arg}`;
}
)"},
          // allowBoolean (default on).
          {R"(
const arg = true;
const msg = `arg = ${arg}`;
)"},
          {R"(
function test<T extends string | boolean>(arg: T) {
  return `arg = ${arg}`;
}
)"},
          // allowArray.
          {R"(
const arg = [];
const msg = `arg = ${arg}`;
)",
           R"([{"allowArray": true}])"},
          {R"(
function test<T extends string[]>(arg: T) {
  return `arg = ${arg}`;
}
)",
           R"([{"allowArray": true}])"},
          {R"(
declare const arg: [number, string];
const msg = `arg = ${arg}`;
)",
           R"([{"allowArray": true}])"},
          {R"(
declare const arg: string[][];
const msg = `arg = ${arg}`;
)",
           R"([{"allowArray": true}])"},
          {R"(
declare const arg: never[];
const msg = `arg = ${arg}`;
)",
           R"([{"allowArray": true, "allowNever": true}])"},
          {R"(
declare const arg: any[];
const msg = `arg = ${arg}`;
)",
           R"([{"allowAny": true, "allowArray": true}])"},
          // allowAny (default on).
          {R"(
const arg: any = 123;
const msg = `arg = ${arg}`;
)"},
          {R"(
const user = JSON.parse('{ "name": "foo" }');
const msg = `arg = ${user.name}`;
)"},
          // allowNullish (default on).
          {R"(
const arg = null;
const msg = `arg = ${arg}`;
)"},
          {R"(
declare const arg: string | null | undefined;
const msg = `arg = ${arg}`;
)"},
          {R"(
function test<T extends null | undefined>(arg: T) {
  return `arg = ${arg}`;
}
)"},
          // allowRegExp (default on).
          {R"(
const arg = new RegExp('foo');
const msg = `arg = ${arg}`;
)"},
          {R"(
const arg = /foo/;
const msg = `arg = ${arg}`;
)"},
          {R"(
declare const arg: string | RegExp;
const msg = `arg = ${arg}`;
)"},
          {R"(
function test<T extends string | RegExp>(arg: T) {
  return `arg = ${arg}`;
}
)"},
          // allowNever.
          {R"(
declare const value: never;
const stringy = `${value}`;
)",
           R"([{"allowNever": true}])"},
          {R"(
function test(arg: 'one' | 'two') {
  switch (arg) {
    case 'one':
      return 1;
    case 'two':
      return 2;
    default:
      throw new Error(`Unrecognised arg: ${arg}`);
  }
}
)",
           R"([{"allowNever": true}])"},
          // Every tester on at once.
          {R"(
type All = string | number | boolean | null | undefined | RegExp | never;
function test<T extends All>(arg: T) {
  return `arg = ${arg}`;
}
)",
           R"([{"allowBoolean": true, "allowNever": true, "allowNullish": true, "allowNumber": true, "allowRegExp": true}])"},
          // A library specifier in `allow`.
          {R"(const msg = `arg = ${Promise.resolve()}`;)",
           R"([{"allow": [{"from": "lib", "name": "Promise"}]}])"},
          // The default `allow` permits the library error type.
          {R"(const msg = `arg = ${new Error()}`;)"},
          // Default-permissive scalars.
          {R"(const msg = `arg = ${false}`;)"},
          {R"(const msg = `arg = ${null}`;)"},
          {R"(const msg = `arg = ${undefined}`;)"},
          {R"(const msg = `arg = ${123}`;)"},
          {R"(const msg = `arg = ${'abc'}`;)"},
      },
      {
          {R"(
const msg = `arg = ${123}`;
)",
           {{"invalidType",
             2,
             22,
             2,
             25,
             "Invalid type \"123\" of template literal expression."}},
           nullptr,
           R"([{"allowNumber": false}])"},
          {R"(
const msg = `arg = ${false}`;
)",
           {{"invalidType",
             2,
             22,
             2,
             27,
             "Invalid type \"false\" of template literal expression."}},
           nullptr,
           R"([{"allowBoolean": false}])"},
          {R"(
const msg = `arg = ${null}`;
)",
           {{"invalidType",
             2,
             22,
             2,
             26,
             "Invalid type \"null\" of template literal expression."}},
           nullptr,
           R"([{"allowNullish": false}])"},
          {R"(
declare const arg: number[];
const msg = `arg = ${arg}`;
)",
           {{"invalidType", 3, 22, 3, 25}},
           nullptr,
           R"([{"allowArray": true, "allowNumber": false}])"},
          {R"(const msg = `arg = ${Promise.resolve()}`;)",
           {{"invalidType", 1, 22, 1, 39}}},
          {R"(const msg = `arg = ${new Error()}`;)",
           {{"invalidType", 1, 22, 1, 33}},
           nullptr,
           R"([{"allow": []}])"},
          {R"(
declare const arg: object[];
const msg = `arg = ${arg}`;
)",
           {{"invalidType", 3, 22, 3, 25}},
           nullptr,
           R"([{"allowArray": true}])"},
          {R"(
declare const arg: number;
const msg = `arg = ${arg}`;
)",
           {{"invalidType",
             3,
             22,
             3,
             25,
             "Invalid type \"number\" of template literal expression."}},
           nullptr,
           R"([{"allowNumber": false}])"},
          {R"(
declare const arg: boolean;
const msg = `arg = ${arg}`;
)",
           {{"invalidType",
             3,
             22,
             3,
             25,
             "Invalid type \"boolean\" of template literal expression."}},
           nullptr,
           R"([{"allowBoolean": false}])"},
          {R"(
const arg = {};
const msg = `arg = ${arg}`;
)",
           {{"invalidType",
             3,
             22,
             3,
             25,
             "Invalid type \"{}\" of template literal expression."}},
           nullptr,
           R"([{"allowBoolean": true, "allowNullish": true, "allowNumber": true}])"},
          {R"(
function test(arg: any) {
  return `arg = ${arg}`;
}
)",
           {{"invalidType",
             3,
             19,
             3,
             22,
             "Invalid type \"any\" of template literal expression."}},
           nullptr,
           R"([{"allowAny": false, "allowBoolean": true, "allowNullish": true, "allowNumber": true}])"},
          {R"(
const arg = new RegExp('foo');
const msg = `arg = ${arg}`;
)",
           {{"invalidType",
             3,
             22,
             3,
             25,
             "Invalid type \"RegExp\" of template literal expression."}},
           nullptr,
           R"([{"allowRegExp": false}])"},
          {R"(
const arg = /foo/;
const msg = `arg = ${arg}`;
)",
           {{"invalidType",
             3,
             22,
             3,
             25,
             "Invalid type \"RegExp\" of template literal expression."}},
           nullptr,
           R"([{"allowRegExp": false}])"},
          {R"(
declare const value: never;
const stringy = `${value}`;
)",
           {{"invalidType",
             3,
             20,
             3,
             25,
             "Invalid type \"never\" of template literal expression."}},
           nullptr,
           R"([{"allowNever": false}])"},
          {R"(
class Base {}
class Derived extends Base {}
const bar = new Derived();
`${bar}`;
)",
           {{"invalidType",
             5,
             4,
             5,
             7,
             "Invalid type \"Derived\" of template literal expression."}},
           nullptr,
           R"([{"allow": []}])"},
      },
      "basic");
}
