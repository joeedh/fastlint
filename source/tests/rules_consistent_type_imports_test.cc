#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

// Codes begin with a newline so the line numbers match upstream's cases.

TEST(rules_consistent_type_imports, valid)
{
  const char *noType = R"([{"prefer": "no-type-imports"}])";
  const char *inlineStyle = R"([{"fixStyle": "inline-type-imports"}])";
  test::runRuleTests(
      rules::kConsistentTypeImports,
      {
          {"\nimport Foo from 'foo';\nconst foo: Foo = new Foo();"},
          {"\nimport foo from 'foo';\nconst foo: foo.Foo = foo.fn();"},
          {"\nimport { A, B } from 'foo';\nconst foo: A = B();\nconst bar = new A();"},
          {"\nimport Foo from 'foo';"},
          {"\nimport Foo from 'foo';\ntype T<Foo> = Foo; // shadowing"},
          {"\nimport Foo from 'foo';\nfunction fn() {\n  type Foo = {}; // shadowing\n  "
           "let "
           "foo: Foo;\n}"},
          {"\nimport { A, B } from 'foo';\nconst b = B;"},
          {"\nimport { A, B, C as c } from 'foo';\nconst d = c;"},
          {"\nimport {} from 'foo'; // empty"},
          {"\nlet foo: import('foo');\nlet bar: import('foo').Bar;",
           R"([{"disallowTypeAnnotations": false}])"},
          {"\nimport Foo from 'foo';\nlet foo: Foo;", noType},
          {"\nimport type Type from 'foo';\n\ntype T = typeof Type;\ntype T = typeof "
           "Type.foo;"},
          {"\nimport type { Type } from 'foo';\n\ntype T = typeof Type;\ntype T = typeof "
           "Type.foo;"},
          {"\nimport type * as Type from 'foo';\n\ntype T = typeof Type;\ntype T = "
           "typeof "
           "Type.foo;"},
          {"\nimport Type from 'foo';\n\ntype T = typeof Type;\ntype T = typeof "
           "Type.foo;",
           noType},
          {"\nimport { Type } from 'foo';\n\ntype T = typeof Type;\ntype T = typeof "
           "Type.foo;",
           noType},
          {"\nimport * as Type from 'foo';\n\ntype T = typeof Type;\ntype T = typeof "
           "Type.foo;",
           noType},
          {"\nimport * as Type from 'foo' assert { type: 'json' };\nconst a: typeof Type "
           "= "
           "Type;",
           noType},
          {"\nimport { type A } from 'foo';\ntype T = A;"},
          {"\nimport { type A, B } from 'foo';\ntype T = A;\nconst b = B;"},
          {"\nimport { type A, type B } from 'foo';\ntype T = A;\ntype Z = B;"},
          {"\nimport { B } from 'foo';\nimport { type A } from 'foo';\ntype T = "
           "A;\nconst b = "
           "B;"},
          {"\nimport { B, type A } from 'foo';\ntype T = A;\nconst b = B;", inlineStyle},
          {"\nimport { B } from 'foo';\nimport type A from 'baz';\ntype T = A;\nconst b "
           "= B;",
           inlineStyle},
          {"\nimport { type B } from 'foo';\nimport type { A } from 'foo';\ntype T = "
           "A;\nconst "
           "b = B;",
           inlineStyle},
          {"\nimport { B, type C } from 'foo';\nimport type A from 'baz';\ntype T = "
           "A;\ntype Z "
           "= C;\nconst b = B;",
           R"([{"fixStyle": "inline-type-imports", "prefer": "type-imports"}])"},
          {"\nimport { B } from 'foo';\nimport { A } from 'foo';\ntype T = A;\nconst b = "
           "B;",
           R"([{"fixStyle": "inline-type-imports", "prefer": "no-type-imports"}])"},
          {"\nimport Type from 'foo';\n\nexport { Type }; // is a value export\nexport "
           "default "
           "Type; // is a value export"},
          {"\nimport type Type from 'foo';\n\nexport { Type }; // is a type-only "
           "export\nexport "
           "default Type; // is a type-only export\nexport type { Type }; // is a "
           "type-only "
           "export"},
          {"\nimport { Type } from 'foo';\n\nexport { Type }; // is a value "
           "export\nexport "
           "default Type; // is a value export"},
          {"\nimport * as Type from 'foo';\n\nexport { Type }; // is a value "
           "export\nexport "
           "default Type; // is a value export"},
          {"\nimport type * as Type from 'foo';\n\nexport { Type }; // is a type-only "
           "export\nexport default Type; // is a type-only export\nexport type { Type }; "
           "// is "
           "a type-only export"},
          {"\nimport Type from 'foo';\n\nexport { Type }; // is a type-only "
           "export\nexport "
           "default Type; // is a type-only export\nexport type { Type }; // is a "
           "type-only "
           "export",
           noType},
          {"\nimport React from 'react';\n\nexport const ComponentFoo: React.FC = () => "
           "{\n  "
           "return <div>Foo Foo</div>;\n};",
           nullptr,
           "test.tsx"},
          {"\nimport Default, * as Rest from 'module';\nconst a: typeof Default = "
           "Default;\nconst b: typeof Rest = Rest;"},
          {"\nimport type * as constants from './constants';\n\nexport type Y = {\n  "
           "[constants.X]: ReadonlyArray<string>;\n};"},
          {"\nimport A from 'foo';\nexport = A;"},
          {"\nimport type A from 'foo';\nexport = A;"},
          {"\nimport type A from 'foo';\nexport = {} as A;"},
          {"\nimport { type A } from 'foo';\nexport = {} as A;"},
          {"\nimport type T from 'mod';\nconst x = T;"},
          {"\nimport type { T } from 'mod';\nconst x = T;"},
          {"\nimport { type T } from 'mod';\nconst x = T;"},
          {"\nimport { Foo } from 'foo';\nimport Bar = Foo.Bar;"},
      },
      {});
}

TEST(rules_consistent_type_imports, invalid)
{
  const char *noType = R"([{"prefer": "no-type-imports"}])";
  const char *inlineStyle =
      R"([{"fixStyle": "inline-type-imports", "prefer": "type-imports"}])";
  test::runRuleTests(
      rules::kConsistentTypeImports,
      {},
      {
          {"\nimport Foo from 'foo';\nlet foo: Foo;\ntype Bar = Foo;\ninterface Baz {\n  "
           "foo: "
           "Foo;\n}\nfunction fn(a: Foo): Foo {}",
           {{"typeOverValue",
             2,
             1,
             2,
             23,
             "All imports in the declaration are only used as types. Use `import "
             "type`."}},
           "\nimport type Foo from 'foo';\nlet foo: Foo;\ntype Bar = Foo;\ninterface Baz "
           "{\n  "
           "foo: Foo;\n}\nfunction fn(a: Foo): Foo {}"},
          {"\nimport Foo from 'foo';\nlet foo: Foo;",
           {{"typeOverValue", 2, 1, 2, 23}},
           "\nimport type Foo from 'foo';\nlet foo: Foo;",
           inlineStyle},
          {"\nimport { A, B } from 'foo';\nlet foo: A;\nlet bar: B;",
           {{"typeOverValue", 2, 1, 2, 28}},
           "\nimport type { A, B } from 'foo';\nlet foo: A;\nlet bar: B;"},
          {"\nimport { A as a, B as b } from 'foo';\nlet foo: a;\nlet bar: b;",
           {{"typeOverValue", 2, 1, 2, 38}},
           "\nimport type { A as a, B as b } from 'foo';\nlet foo: a;\nlet bar: b;"},
          {"\nimport Foo from 'foo';\ntype Bar = typeof Foo; // TSTypeQuery",
           {{"typeOverValue", 2, 1, 2, 23}},
           "\nimport type Foo from 'foo';\ntype Bar = typeof Foo; // TSTypeQuery"},
          {"\nimport foo from 'foo';\ntype Bar = foo.Bar; // TSQualifiedName",
           {{"typeOverValue", 2, 1, 2, 23}},
           "\nimport type foo from 'foo';\ntype Bar = foo.Bar; // TSQualifiedName"},
          {"\nimport foo from 'foo';\ntype Baz = (typeof foo.bar)['Baz']; // "
           "TSQualifiedName & "
           "TSTypeQuery",
           {{"typeOverValue", 2, 1, 2, 23}},
           "\nimport type foo from 'foo';\ntype Baz = (typeof foo.bar)['Baz']; // "
           "TSQualifiedName & TSTypeQuery"},
          {"\nimport * as A from 'foo';\nlet foo: A.Foo;",
           {{"typeOverValue", 2, 1, 2, 26}},
           "\nimport type * as A from 'foo';\nlet foo: A.Foo;"},
          {"\nimport A, { B } from 'foo';\nlet foo: A;\nlet bar: B;",
           {{"typeOverValue", 2, 1, 2, 28}},
           "\nimport type { B } from 'foo';\nimport type A from 'foo';\nlet foo: A;\nlet "
           "bar: B;"},
          {"\nimport A, {} from 'foo';\nlet foo: A;",
           {{"typeOverValue", 2, 1, 2, 25}},
           "\nimport type A from 'foo';\nlet foo: A;"},
          {"\nimport { A, B } from 'foo';\nconst foo: A = B();",
           {{"someImportsAreOnlyTypes",
             2,
             1,
             2,
             28,
             "Imports \"A\" are only used as type."}},
           "\nimport type { A } from 'foo';\nimport { B } from 'foo';\nconst foo: A = "
           "B();"},
          {"\nimport { A, B, C } from 'foo';\nconst foo: A = B();\nlet bar: C;",
           {{"someImportsAreOnlyTypes",
             2,
             1,
             2,
             31,
             "Imports \"A\" and \"C\" are only used as type."}},
           "\nimport type { A, C } from 'foo';\nimport { B } from 'foo';\nconst foo: A = "
           "B();\nlet bar: C;"},
          {"\nimport { A, B, C, D } from 'foo';\nconst foo: A = B();\ntype T = { bar: C; "
           "baz: D };",
           {{"someImportsAreOnlyTypes",
             2,
             1,
             2,
             34,
             "Imports \"A\", \"C\" and \"D\" are only used as type."}},
           "\nimport type { A, C, D } from 'foo';\nimport { B } from 'foo';\nconst foo: "
           "A = "
           "B();\ntype T = { bar: C; baz: D };"},
          {"\nimport A, { B, C, D } from 'foo';\nB();\ntype T = { foo: A; bar: C; baz: D "
           "};",
           {{"someImportsAreOnlyTypes", 2, 1, 2, 34}},
           "\nimport type { C, D } from 'foo';\nimport type A from 'foo';\nimport { B } "
           "from "
           "'foo';\nB();\ntype T = { foo: A; bar: C; baz: D };"},
          {"\nimport A, { B } from 'foo';\nB();\ntype T = A;",
           {{"someImportsAreOnlyTypes", 2, 1, 2, 28}},
           "\nimport type A from 'foo';\nimport { B } from 'foo';\nB();\ntype T = A;"},
          {"\nimport type Already1Def from 'foo';\nimport type { Already1 } from "
           "'foo';\nimport "
           "A, { B } from 'foo';\nimport { C, D, E } from 'bar';\nimport type { Already2 "
           "} from "
           "'bar';\ntype T = { b: B; c: C; d: D };",
           {{"someImportsAreOnlyTypes", 4, 1, 4, 28},
            {"someImportsAreOnlyTypes", 5, 1, 5, 31}},
           "\nimport type Already1Def from 'foo';\nimport type { Already1, B } from "
           "'foo';\nimport A from 'foo';\nimport { E } from 'bar';\nimport type { "
           "Already2, C, "
           "D } from 'bar';\ntype T = { b: B; c: C; d: D };"},
          {"\nimport A, { /* comment */ B } from 'foo';\ntype T = B;",
           {{"someImportsAreOnlyTypes", 2, 1, 2, 42}},
           "\nimport type { /* comment */ B } from 'foo';\nimport A from 'foo';\ntype T "
           "= B;"},
          {"\nimport { A, B, C } from 'foo';\nimport { D, E, F, } from 'bar';\ntype T = "
           "A | D;",
           {{"someImportsAreOnlyTypes", 2, 1, 2, 31},
            {"someImportsAreOnlyTypes", 3, 1, 3, 32}},
           "\nimport type { A } from 'foo';\nimport { B, C } from 'foo';\nimport type { "
           "D } from "
           "'bar';\nimport { E, F, } from 'bar';\ntype T = A | D;"},
          {"\nimport { A, B, C } from 'foo';\nimport { D, E, F, } from 'bar';\ntype T = "
           "B | E;",
           {{"someImportsAreOnlyTypes", 2, 1, 2, 31},
            {"someImportsAreOnlyTypes", 3, 1, 3, 32}},
           "\nimport type { B } from 'foo';\nimport { A, C } from 'foo';\nimport type { "
           "E } from "
           "'bar';\nimport { D, F, } from 'bar';\ntype T = B | E;"},
          {"\nimport { A, B, C } from 'foo';\nimport { D, E, F, } from 'bar';\ntype T = "
           "C | F;",
           {{"someImportsAreOnlyTypes", 2, 1, 2, 31},
            {"someImportsAreOnlyTypes", 3, 1, 3, 32}},
           "\nimport type { C } from 'foo';\nimport { A, B } from 'foo';\nimport type { "
           "F } from "
           "'bar';\nimport { D, E, } from 'bar';\ntype T = C | F;"},
          {"\nimport { Type1, Type2 } from 'named_types';\nimport Type from "
           "'default_type';\nimport * as Types from 'namespace_type';\nimport Default, { "
           "Named "
           "} from 'default_and_named_type';\ntype T = Type1 | Type2 | Type | Types.A | "
           "Default "
           "| Named;",
           {{"typeOverValue", 2, 1, 2, 44},
            {"typeOverValue", 3, 1, 3, 33},
            {"typeOverValue", 4, 1, 4, 41},
            {"typeOverValue", 5, 1, 5, 57}},
           "\nimport type { Type1, Type2 } from 'named_types';\nimport type Type from "
           "'default_type';\nimport type * as Types from 'namespace_type';\nimport type "
           "{ Named "
           "} from 'default_and_named_type';\nimport type Default from "
           "'default_and_named_type';\ntype T = Type1 | Type2 | Type | Types.A | Default "
           "| "
           "Named;"},
          {"\nimport { Value1, Type1 } from 'named_import';\nimport Type2, { Value2 } "
           "from "
           "'default_import';\nimport Value3, { Type3 } from 'default_import2';\nimport "
           "Type4, "
           "{ Type5, Value4 } from 'default_and_named_import';\ntype T = Type1 | Type2 | "
           "Type3 "
           "| Type4 | Type5;",
           {{"someImportsAreOnlyTypes", 2, 1, 2, 46},
            {"someImportsAreOnlyTypes", 3, 1, 3, 48},
            {"someImportsAreOnlyTypes", 4, 1, 4, 49},
            {"someImportsAreOnlyTypes",
             5,
             1,
             5,
             65,
             "Imports \"Type4\" and \"Type5\" are only used as type."}},
           "\nimport type { Type1 } from 'named_import';\nimport { Value1 } from "
           "'named_import';\nimport type Type2 from 'default_import';\nimport { Value2 } "
           "from "
           "'default_import';\nimport type { Type3 } from 'default_import2';\nimport "
           "Value3 "
           "from 'default_import2';\nimport type { Type5 } from "
           "'default_and_named_import';\nimport type Type4 from "
           "'default_and_named_import';\nimport { Value4 } from "
           "'default_and_named_import';\ntype T = Type1 | Type2 | Type3 | Type4 | "
           "Type5;"},
          {"\nlet foo: import('foo');\nlet bar: import('foo').Bar;",
           {{"noImportTypeAnnotations",
             2,
             10,
             2,
             23,
             "`import()` type annotations are forbidden."},
            {"noImportTypeAnnotations", 3, 10, 3, 27}}},
          {"\nimport type Foo from 'foo';\nlet foo: Foo;",
           {{"avoidImportType",
             2,
             1,
             2,
             28,
             "Use an `import` instead of an `import type`."}},
           "\nimport Foo from 'foo';\nlet foo: Foo;",
           noType},
          {"\nimport type { Foo } from 'foo';\nlet foo: Foo;",
           {{"avoidImportType", 2, 1, 2, 32}},
           "\nimport { Foo } from 'foo';\nlet foo: Foo;",
           noType},
          {"\nimport Type from 'foo';\n\ntype T = typeof Type;\ntype T = typeof "
           "Type.foo;",
           {{"typeOverValue", 2, 1, 2, 24}},
           "\nimport type Type from 'foo';\n\ntype T = typeof Type;\ntype T = typeof "
           "Type.foo;"},
          {"\nimport * as Type from 'foo';\n\ntype T = typeof Type;\ntype T = typeof "
           "Type.foo;",
           {{"typeOverValue", 2, 1, 2, 29}},
           "\nimport type * as Type from 'foo';\n\ntype T = typeof Type;\ntype T = "
           "typeof "
           "Type.foo;"},
          {"\nimport type * as Type from 'foo';\n\ntype T = typeof Type;\ntype T = "
           "typeof "
           "Type.foo;",
           {{"avoidImportType", 2, 1, 2, 34}},
           "\nimport * as Type from 'foo';\n\ntype T = typeof Type;\ntype T = typeof "
           "Type.foo;",
           noType},
          {"\nimport Type from 'foo';\n\nexport type { Type }; // is a type-only export",
           {{"typeOverValue", 2, 1, 2, 24}},
           "\nimport type Type from 'foo';\n\nexport type { Type }; // is a type-only "
           "export"},
          {"\nimport { Type } from 'foo';\n\nexport type { Type }; // is a type-only "
           "export",
           {{"typeOverValue", 2, 1, 2, 28}},
           "\nimport type { Type } from 'foo';\n\nexport type { Type }; // is a "
           "type-only export"},
          {"\nimport type Type from 'foo';\n\nexport { Type }; // is a type-only "
           "export\nexport "
           "default Type; // is a type-only export\nexport type { Type }; // is a "
           "type-only "
           "export",
           {{"avoidImportType", 2, 1, 2, 29}},
           "\nimport Type from 'foo';\n\nexport { Type }; // is a type-only "
           "export\nexport "
           "default Type; // is a type-only export\nexport type { Type }; // is a "
           "type-only "
           "export",
           noType},
          {"\nimport type /*comment*/ * as AllType from 'foo';\nimport type /*comment*/ "
           "{ Type } "
           "from 'foo';\n\ntype T = { a: AllType; c: Type };",
           {{"avoidImportType", 2, 1, 2, 49}, {"avoidImportType", 3, 1, 3, 45}},
           "\nimport /*comment*/ * as AllType from 'foo';\nimport { /*comment*/ Type } "
           "from "
           "'foo';\n\ntype T = { a: AllType; c: Type };",
           noType},
          {"\nimport Default, * as Rest from 'module';\nconst a: Rest.A = '';",
           {{"someImportsAreOnlyTypes", 2, 1, 2, 41}},
           "\nimport type * as Rest from 'module';\nimport Default from 'module';\nconst "
           "a: "
           "Rest.A = '';"},
          {"\nimport Default, * as Rest from 'module';\nconst a: Default = '';",
           {{"someImportsAreOnlyTypes", 2, 1, 2, 41}},
           "\nimport type Default from 'module';\nimport * as Rest from 'module';\nconst "
           "a: "
           "Default = '';"},
          {"\nimport Default, * as Rest from 'module';\nconst a: Default = '';\nconst b: "
           "Rest.A "
           "= '';",
           {{"typeOverValue", 2, 1, 2, 41}},
           "\nimport type * as Rest from 'module';\nimport type Default from "
           "'module';\nconst "
           "a: Default = '';\nconst b: Rest.A = '';"},
          {"\nimport { type A, B } from 'foo';\ntype T = A;\nconst b = B;",
           {{"avoidImportType", 2, 10, 2, 16}},
           "\nimport { A, B } from 'foo';\ntype T = A;\nconst b = B;",
           noType},
          {"\nimport { A, B, type C } from 'foo';\ntype T = A | C;\nconst b = B;",
           {{"someImportsAreOnlyTypes", 2, 1, 2, 36}},
           "\nimport type { A } from 'foo';\nimport { B, type C } from 'foo';\ntype T = "
           "A | "
           "C;\nconst b = B;"},
          // inline-type-imports
          {"\nimport { A, B } from 'foo';\nlet foo: A;\nlet bar: B;",
           {{"typeOverValue", 2, 1, 2, 28}},
           "\nimport { type A, type B } from 'foo';\nlet foo: A;\nlet bar: B;",
           inlineStyle},
          {"\nimport { A, B } from 'foo';\n\nlet foo: A;\nB();",
           {{"someImportsAreOnlyTypes", 2, 1, 2, 28}},
           "\nimport { type A, B } from 'foo';\n\nlet foo: A;\nB();",
           inlineStyle},
          {"\nimport { A } from 'foo';\nimport { B } from 'foo';\ntype T = A;\ntype U = "
           "B;",
           {{"typeOverValue", 2, 1, 2, 25}, {"typeOverValue", 3, 1, 3, 25}},
           "\nimport { type A } from 'foo';\nimport { type B } from 'foo';\ntype T = "
           "A;\ntype U "
           "= B;",
           inlineStyle},
          {"\nimport { A } from 'foo';\nimport B from 'foo';\ntype T = A;\ntype U = B;",
           {{"typeOverValue", 2, 1, 2, 25}, {"typeOverValue", 3, 1, 3, 21}},
           "\nimport { type A } from 'foo';\nimport type B from 'foo';\ntype T = "
           "A;\ntype U = "
           "B;",
           inlineStyle},
          {"\nimport A, { B, C } from 'foo';\ntype T = B;\ntype U = C;\nA();",
           {{"someImportsAreOnlyTypes", 2, 1, 2, 31}},
           "\nimport A, { type B, type C } from 'foo';\ntype T = B;\ntype U = C;\nA();",
           inlineStyle},
          {"\nimport A, { B, C } from 'foo';\ntype T = B;\ntype U = C;\ntype V = A;",
           {{"typeOverValue", 2, 1, 2, 31}},
           "\nimport { type B, type C } from 'foo';\nimport type A from 'foo';\ntype T = "
           "B;\ntype "
           "U = C;\ntype V = A;",
           inlineStyle},
          {"\nimport A, { B, C as D } from 'foo';\ntype T = B;\ntype U = D;\ntype V = A;",
           {{"typeOverValue", 2, 1, 2, 36}},
           "\nimport { type B, type C as D } from 'foo';\nimport type A from "
           "'foo';\ntype T = "
           "B;\ntype U = D;\ntype V = A;",
           inlineStyle},
          {"\nimport { /* comment */ A, B } from 'foo';\ntype T = A;",
           {{"someImportsAreOnlyTypes", 2, 1, 2, 42}},
           "\nimport { /* comment */ type A, B } from 'foo';\ntype T = A;",
           inlineStyle},
          {"\nimport { A, B, C } from 'foo';\nimport type { D } from 'deez';\n\nconst "
           "foo: A = "
           "B();\nlet bar: C;\nlet baz: D;",
           {{"someImportsAreOnlyTypes", 2, 1, 2, 31}},
           "\nimport { type A, B, type C } from 'foo';\nimport type { D } from "
           "'deez';\n\nconst "
           "foo: A = B();\nlet bar: C;\nlet baz: D;",
           inlineStyle},
          {"\nimport A from 'foo';\nexport = {} as A;",
           {{"typeOverValue", 2, 1, 2, 21}},
           "\nimport type A from 'foo';\nexport = {} as A;",
           inlineStyle},
          {"\nimport { A } from 'foo';\nexport = {} as A;",
           {{"typeOverValue", 2, 1, 2, 25}},
           "\nimport { type A } from 'foo';\nexport = {} as A;",
           inlineStyle},
          {"\nimport Foo from 'foo';\n@deco\nclass A {\n  constructor(foo: Foo) {}\n}",
           {{"typeOverValue", 2, 1, 2, 23}},
           "\nimport type Foo from 'foo';\n@deco\nclass A {\n  constructor(foo: Foo) "
           "{}\n}"},
          {"\nimport Foo from 'foo';\nclass A {\n  foo(@deco foo: Foo) {}\n}",
           {{"typeOverValue", 2, 1, 2, 23}},
           "\nimport type Foo from 'foo';\nclass A {\n  foo(@deco foo: Foo) {}\n}"},
          {"\nimport 'foo';\nimport { Foo, Bar } from 'foo';\nfunction test(foo: Foo) {}",
           {{"someImportsAreOnlyTypes", 3, 1, 3, 32}},
           "\nimport 'foo';\nimport type { Foo } from 'foo';\nimport { Bar } from "
           "'foo';\nfunction test(foo: Foo) {}"},
      });
}
