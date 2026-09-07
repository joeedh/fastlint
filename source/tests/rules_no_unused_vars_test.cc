#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

// Codes begin with a newline so the line numbers match upstream's cases.

TEST(rules_no_unused_vars, typescript_cases)
{
  const char *ignoreUnderscore = R"([{"varsIgnorePattern": "^_"}])";
  const char *reportUsed =
      R"([{"reportUsedIgnorePattern": true, "varsIgnorePattern": "^_"}])";
  test::runRuleTests(
      rules::kNoUnusedVars,
      {
          {"\nimport { ClassDecoratorFactory } from "
           "'decorators';\n@ClassDecoratorFactory()\n"
           "export class Foo {}"},
          {"\nimport { AccessorDecorator } from 'decorators';\nexport class Foo {\n  "
           "@AccessorDecorator\n  set bar() {}\n}"},
          {"\nimport { MethodDecoratorFactory } from 'decorators';\nexport class Foo {\n "
           " "
           "@MethodDecoratorFactory(false)\n  bar() {}\n}"},
          {"\nimport { ConstructorParameterDecoratorFactory } from 'decorators';\nexport "
           "class "
           "Service {\n  constructor(\n    "
           "@ConstructorParameterDecoratorFactory(APP_CONFIG) "
           "config: AppConfig,\n  ) {\n    this.title = config.title;\n  }\n}"},
          {"\nimport { ParameterDecorator } from 'decorators';\nexport class Foo {\n  "
           "static "
           "greet(@ParameterDecorator name: string) {\n    return name;\n  }\n}"},
          {"\nimport { Input, Output, EventEmitter } from 'decorators';\nexport class "
           "SomeComponent {\n  @Input() data;\n  @Output()\n  click = new "
           "EventEmitter();\n}"},
          {"\nimport { foo, bar } from 'decorators';\nexport class B {\n  @foo x;\n  "
           "@bar\n  "
           "y;\n}"},
          {"\ninterface Base {}\nclass Thing implements Base {}\nnew Thing();"},
          {"\ninterface Base {}\nconst a: Base = {};\nconsole.log(a);"},
          {"\nimport { Foo } from 'foo';\nfunction bar<T>(): T {}\nbar<Foo>();"},
          {"\nimport { Foo } from 'foo';\nconst bar = function <T>(): T "
           "{};\nbar<Foo>();"},
          {"\nimport { Foo } from 'foo';\nconst bar = <T,>(): T => {};\nbar<Foo>();"},
          {"\nimport { Foo } from 'foo';\n<Foo>(<T,>(): T => {})();"},
          {"\nimport { Nullable } from 'nullable';\nconst a: Nullable<string> = "
           "'hello';\nconsole.log(a);"},
          {"\nimport { Nullable } from 'nullable';\nimport { SomeOther } from "
           "'other';\nconst a: "
           "Nullable<SomeOther> = 'hello';\nconsole.log(a);"},
          {"\nimport { Nullable } from 'nullable';\nconst a: Nullable | undefined = "
           "'hello';\nconsole.log(a);"},
          {"\nimport { Nullable } from 'nullable';\nconst a: Nullable[] = "
           "'hello';\nconsole.log(a);"},
          {"\nimport { Nullable } from 'nullable';\nconst a: Array<Array<Nullable>> = "
           "'hello';\nconsole.log(a);"},
          {"\nimport { Nullable } from 'nullable';\nimport { Component } from "
           "'react';\nclass Foo "
           "implements Component<Nullable> {}\nnew Foo();"},
          {"\nimport { Nullable } from 'nullable';\nimport { Component } from "
           "'react';\nclass Foo "
           "extends Component<Nullable, {}> {}\nnew Foo();"},
          {"\nimport { Nullable } from 'nullable';\nimport { SomeOther } from "
           "'some';\nimport { "
           "Component, Component2 } from 'react';\nclass Foo implements "
           "Component<Nullable<SomeOther>, {}>, Component2 {}\nnew Foo();"},
          {"\nimport { Nullable } from 'nullable';\nimport { Another } from "
           "'some';\nclass A {\n  "
           "do = (a: Nullable<Another>) => {\n    console.log(a);\n  };\n}\nnew A();"},
          {"\nimport { Nullable } from 'nullable';\nimport { Another } from "
           "'some';\nclass A {\n  "
           "do(): Nullable<Another> {\n    return null;\n  }\n}\nnew A();"},
          {"\nimport { Nullable } from 'nullable';\nimport { Another } from "
           "'some';\nexport "
           "interface A {\n  do(a: Nullable<Another>);\n}"},
          {"\nimport { Nullable } from 'nullable';\nimport { Another } from "
           "'some';\nexport "
           "interface A {\n  other: Nullable<Another>;\n}"},
          {"\nimport { Nullable } from 'nullable';\nfunction foo(a: Nullable) {\n  "
           "console.log(a);\n}\nfoo();"},
          {"\nimport { Nullable } from 'nullable';\nfunction foo(): Nullable {\n  return "
           "null;\n}\nfoo();"},
          {"\nimport { Nullable } from 'nullable';\nimport { SomeOther } from "
           "'some';\nimport { "
           "Another } from 'some';\nexport interface A extends Nullable<SomeOther> {\n  "
           "other: "
           "Nullable<Another>;\n}"},
          {"\nimport { Foo } from './types';\nclass Bar<T extends Foo> {\n  prop: "
           "T;\n}\nnew "
           "Bar<number>();"},
          {"\nimport { Foo } from './types';\nclass Bar<T = Foo> {\n  prop: T;\n}\nnew "
           "Bar<number>();"},
          {"\nimport { Foo } from './types';\nclass Foo<T = any> {\n  prop: T;\n}\nnew "
           "Foo();"},
          {"\ntype Foo = 'a' | 'b' | 'c';\ntype Bar = number;\nexport const map: { [name "
           "in "
           "Foo]: Bar } = {\n  a: 1,\n  b: 2,\n  c: 3,\n};"},
          {"\ntype Foo = 'a' | 'b' | 'c';\ntype Bar = number;\nexport const map: { [name "
           "in "
           "Foo as string]: Bar } = {\n  a: 1,\n  b: 2,\n  c: 3,\n};"},
          {"\nimport { Nullable } from 'nullable';\nclass A<T> {\n  bar: T;\n}\nnew "
           "A<Nullable>();"},
          {"\nimport { Nullable } from 'nullable';\nimport { SomeOther } from "
           "'other';\nfunction "
           "foo<T extends Nullable>(): T {}\nfoo<SomeOther>();"},
          {"\nimport { Nullable } from 'nullable';\nimport { SomeOther } from "
           "'other';\ninterface "
           "A<T extends Nullable> {\n  bar: T;\n}\nexport const a: A<SomeOther> = {\n  "
           "foo: "
           "'bar',\n};"},
          {"\nexport class App {\n  constructor(private logger: Logger) {\n    "
           "console.log(this.logger);\n  }\n}"},
          {"\nexport class App {\n  constructor(bar: string);\n  constructor(private "
           "logger: "
           "Logger) {\n    console.log(this.logger);\n  }\n}"},
          {"\nexport class App {\n  constructor(\n    baz: string,\n    private logger: "
           "Logger,\n  ) {\n    console.log(baz);\n    console.log(this.logger);\n  "
           "}\n}"},
          {"\nexport class App {\n  constructor(private logger: Logger) {}\n  meth() {\n "
           "   "
           "console.log(this.logger);\n  }\n}"},
          {"\nimport { Component, Vue } from 'vue-property-decorator';\nimport "
           "HelloWorld from "
           "'./components/HelloWorld.vue';\n@Component({\n  components: {\n    "
           "HelloWorld,\n  "
           "},\n})\nexport default class App extends Vue {}"},
          {"\nimport firebase, { User } from "
           "'firebase/app';\nfirebase.initializeApp({});\nexport "
           "function authenticated(cb: (user: User | null) => void): void {\n  "
           "firebase.auth().onAuthStateChanged(user => cb(user));\n}"},
          {"\nimport webpack from 'webpack';\nexport default function "
           "webpackLoader(this: "
           "webpack.loader.LoaderContext) {}"},
          {"\nimport execa, { Options as ExecaOptions } from 'execa';\nexport function "
           "foo(options: ExecaOptions): execa {\n  options();\n}"},
          {"export const a: Array<{ b: B }> = [];"},
          {"\nexport enum FormFieldIds {\n  PHONE = 'phone',\n  EMAIL = 'email',\n}"},
          {"\nenum FormFieldIds {\n  PHONE = 'phone',\n  EMAIL = 'email',\n}\nexport "
           "interface "
           "IFoo {\n  fieldName: FormFieldIds;\n}"},
          {"\nenum FormFieldIds {\n  PHONE = 'phone',\n  EMAIL = 'email',\n}\nexport "
           "interface "
           "IFoo {\n  fieldName: FormFieldIds.EMAIL;\n}"},
          {"\nimport * as fastify from 'fastify';\nimport { Server, IncomingMessage, "
           "ServerResponse } from 'http';\nconst server: fastify.FastifyInstance<Server, "
           "IncomingMessage, ServerResponse> =\n  fastify({});\nserver.get('/ping');"},
          {"\ndeclare namespace Foo {\n  function bar(line: string, index: number | "
           "null, "
           "tabSize: number): number;\n  var baz: string;\n}\nconsole.log(Foo);"},
          {"\nimport foo from 'foo';\nexport interface Bar extends foo.i18n {}"},
          {"\nimport foo from 'foo';\nimport bar from 'foo';\nexport interface Bar "
           "extends "
           "foo.i18n<bar> {}"},
          {"\nimport { TypeA } from './interface';\nexport const a = "
           "<GenericComponent<TypeA> "
           "/>;",
           nullptr,
           "test.tsx"},
          {"\nconst text = 'text';\nexport function Foo() {\n  return (\n    <div>\n     "
           " <input "
           "type=\"search\" size={30} placeholder={text} />\n    </div>\n  );\n}",
           nullptr,
           "test.tsx"},
          {"\nimport { observable } from 'mobx';\nexport default class ListModalStore "
           "{\n  "
           "@observable\n  orderList: IObservableArray<BizPurchaseOrderTO> = "
           "observable([]);\n}"},
          {"\nimport { Dec, TypeA, Class } from 'test';\nexport default class Foo {\n  "
           "constructor(\n    @Dec(Class)\n    private readonly prop: TypeA<Class>,\n  ) "
           "{}\n}"},
          {"\nexport function foo(): void;\nexport function foo(): void;\nexport "
           "function foo(): "
           "void {}"},
          {"\nexport function foo(a: number): number;\nexport function foo(a: string): "
           "string;\nexport function foo(a: number | string): number | string {\n  "
           "return a;\n}"},
          {"\nexport type T = {\n  new (): T;\n  new (arg: number): T;\n  new <T>(arg: "
           "number): "
           "T;\n};"},
          {"export type T = new () => T;"},
          {"export type T = new <T>(arg: number) => T;"},
          {"\nenum Foo {\n  a,\n}\nexport type T = {\n  [Foo.a]: 1;\n};"},
          {"\ntype Foo = string;\nexport class Bar {\n  [x: Foo]: any;\n}"},
          {"\ntype Foo = string;\nexport class Bar {\n  [x: Foo]: Foo;\n}"},
          {"\nnamespace Foo {\n  export const Foo = 1;\n}\nexport { Foo };"},
          {"\nexport namespace Foo {\n  export const item: Foo = 1;\n}"},
          {"\nnamespace foo.bar {\n  export interface User {\n    name: string;\n  }\n}"},
          {"\nexport interface Foo {\n  bar: string;\n  baz: Foo['bar'];\n}"},
          {"export type Bar = Array<Bar>;"},
          {"\nfunction Foo() {}\nnamespace Foo {\n  export const x = 1;\n}\nexport { Foo "
           "};"},
          {"\nclass Foo {}\nnamespace Foo {\n  export const x = 1;\n}\nexport { Foo };"},
          {"\nnamespace Foo {}\nconst Foo = 1;\nexport { Foo };"},
          {"\ntype Foo = {\n  error: Error | null;\n};\nexport function foo() {\n  "
           "return new "
           "Promise<Foo>();\n}"},
          {"\nexport function foo<T>(value: T): T {\n  return { value };\n}\nexport type "
           "Foo<T> "
           "= typeof foo<T>;"},
          {"\nexport interface Event<T> {\n  (\n    listener: (e: T) => any,\n    "
           "thisArgs?: "
           "any,\n    disposables?: Disposable[],\n  ): Disposable;\n}",
           R"([{"args": "after-used", "argsIgnorePattern": "^_", "ignoreRestSiblings": true, "varsIgnorePattern": "^_$"}])"},
          {"\nexport class Test {\n  constructor(@Optional() value: number[] = []) {\n   "
           " "
           "console.log(value);\n  }\n}\nfunction Optional() {\n  return () => {};\n}"},
          {"\nimport { FooType } from './fileA';\nexport abstract class Foo {\n  "
           "protected "
           "abstract readonly type: FooType;\n}"},
          {"export type F<A extends unknown[]> = (...a: A) => unknown;"},
          {"\nimport { Foo } from './bar';\nexport type F<A extends unknown[]> = (...a: "
           "Foo<A>) "
           "=> unknown;"},
          {"\ntype StyledPaymentProps = {\n  isValid: boolean;\n};\nexport const "
           "StyledPayment = "
           "styled.div<StyledPaymentProps>``;"},
          {"\nimport type { foo } from './a';\nexport type Bar = typeof foo;"},
          {"\ninterface Foo {}\ntype Bar = {};\ndeclare class Clazz {}\ndeclare function "
           "func();\ndeclare enum Enum {}\ndeclare namespace Name {}\ndeclare const "
           "v1;\ndeclare "
           "var v2;\ndeclare let v3;\ndeclare const { v4 };\ndeclare const { v4: v5 "
           "};\ndeclare "
           "const [v6];",
           nullptr,
           "foo.d.ts"},
          {"export type Test<U> = U extends (k: infer I) => void ? I : never;"},
          {"export type Test<U> = U extends { [k: string]: infer I } ? I : never;"},
          {"\nexport type Test<U> = U extends (arg: {\n  [k: string]: (arg2: infer I) => "
           "void;\n}) => void\n  ? I\n  : never;"},
          {"\nimport React from 'react';\nexport const ComponentFoo: React.FC = () => "
           "{\n  return "
           "<div>Foo Foo</div>;\n};",
           nullptr,
           "test.tsx"},
          {"\ndeclare module 'foo' {\n  type Test = 1;\n}"},
          {"\ndeclare module 'foo' {\n  type Test = 1;\n  const x: Test = 1;\n  export = "
           "x;\n}"},
          {"\ndeclare global {\n  interface Foo {}\n}"},
          {"\ndeclare global {\n  namespace jest {\n    interface Matchers<R> {\n      "
           "toBeSeven: () => R;\n    }\n  }\n}"},
          {"\nexport declare namespace Foo {\n  namespace Bar {\n    namespace Baz {\n   "
           "   "
           "namespace Bam {\n        const x = 1;\n      }\n    }\n  }\n}"},
          {"\nclass Foo<T> {\n  value: T;\n}\nclass Bar<T> {\n  foo = Foo<T>;\n}\nnew "
           "Bar();"},
          {"\ndeclare namespace A {\n  export interface A {}\n}", nullptr, "foo.d.ts"},
          {"\ndeclare function A(A: string): string;", nullptr, "foo.d.ts"},
          {"\ntype Color = 'red' | 'blue';\ntype Quantity = 'one' | 'two';\nexport type "
           "SeussFish = `${Quantity | Color} fish`;"},
          {"\ninterface IItem {\n  title: string;\n  url: string;\n  children?: "
           "IItem[];\n}",
           nullptr,
           "foo.d.ts"},
          {"\nnamespace _Foo {\n  export const bar = 1;\n  export const baz = "
           "Foo.bar;\n}",
           ignoreUnderscore},
          {"\ninterface _Foo {\n  a: string;\n  b: Foo;\n}", ignoreUnderscore},
          {"\nfunction foo() {}\nexport class Foo {\n  constructor() {\n    foo();\n  "
           "}\n}"},
          {"\nfunction foo() {}\nexport class Foo {\n  static {}\n  constructor() {\n    "
           "foo();\n  }\n}"},
          {"\ninterface Foo {\n  bar: string;\n}\nexport const Foo = 'bar';"},
          {"\nexport const Foo = 'bar';\ninterface Foo {\n  bar: string;\n}"},
          {"\nlet foo = 1;\nfoo ??= 2;"},
          {"\nlet foo = 1;\nfoo &&= 2;"},
          {"\nlet foo = 1;\nfoo ||= 2;"},
          {"\nconst foo = 1;\nexport = foo;"},
          {"\nconst Foo = 1;\ninterface Foo {\n  bar: string;\n}\nexport = Foo;"},
          {"\ninterface Foo {\n  bar: string;\n}\nexport = Foo;"},
          {"\ntype Foo = 1;\nexport = Foo;"},
          {"\ntype Foo = 1;\nexport = {} as Foo;"},
          {"\ndeclare module 'foo' {\n  type Foo = 1;\n  export = Foo;\n}"},
          {"\nnamespace Foo {\n  export const foo = 1;\n}\nexport namespace Bar {\n  "
           "export import "
           "TheFoo = Foo;\n}"},
          {"\ntype _Foo = 1;\nexport const x: _Foo = 1;",
           R"([{"reportUsedIgnorePattern": false, "varsIgnorePattern": "^_"}])"},
          {"\nexport const foo: number = 1;\nexport type Foo = typeof foo;"},
          {"\nimport { foo } from 'foo';\nexport type Foo = typeof foo;\nexport const "
           "bar = (): "
           "Foo => foo;"},
          {"\nimport { SomeType } from 'foo';\nexport const value = 1234 as typeof "
           "SomeType;"},
          {"\nimport { foo } from 'foo';\nexport type Bar = typeof foo;"},
          {"\nexport enum Foo {\n  _A,\n}",
           R"([{"reportUsedIgnorePattern": true, "varsIgnorePattern": "_"}])"},
          {"\nconst command = (): ParameterDecorator => {\n  return () => "
           "{};\n};\nexport class "
           "Foo {\n  bar(@command() command: string) {\n    console.log(command);\n  "
           "}\n}"},
          {"\nexport namespace Foo {\n  const foo: 1234;\n}", nullptr, "foo.d.ts"},
          {"\ndeclare module 'foo' {\n  const foo: 1234;\n}", nullptr, "foo.d.ts"},
          {"\nconst foo: 1234;", nullptr, "foo.d.ts"},
          {"\nexport namespace Foo {\n  export import Bar = Something.Bar;\n  const foo: "
           "1234;\n}",
           nullptr,
           "foo.d.ts"},
          {"\nexport import Bar = Something.Bar;\nconst foo: 1234;", nullptr, "foo.d.ts"},
          {"\ndeclare module 'foo' {\n  export import Bar = Something.Bar;\n  const foo: "
           "1234;\n  "
           "export const bar: string;\n  export namespace NS {\n    const baz: 1234;\n  "
           "}\n}",
           nullptr,
           "foo.d.ts"},
          {"\nexport namespace Foo {\n  const foo: 1234;\n  export const bar: string;\n  "
           "export "
           "namespace NS {\n    const baz: 1234;\n  }\n}",
           nullptr,
           "foo.d.ts"},
          {"\nexport namespace Foo {\n  type Foo = 1;\n  type Bar = 1;\n  export default "
           "function "
           "foo(): Bar;\n}",
           nullptr,
           "foo.d.ts"},
          {"\ntype Foo = 1;\ntype Bar = 1;\nexport default function foo(): Bar;",
           nullptr,
           "foo.d.ts"},
          {"\nclass Foo {}\ndeclare class Bar {}", nullptr, "foo.d.ts"},
          {"\nusing resource = getResource();\nresource;"},
          {"\nusing resource = getResource();", R"([{"ignoreUsingDeclarations": true}])"},
          {"\nawait using resource = getResource();",
           R"([{"ignoreUsingDeclarations": true}])"},
      },
      {
          {"\nimport { ClassDecoratorFactory } from 'decorators';\nexport class Foo {}",
           {{"unusedVar",
             2,
             10,
             2,
             31,
             "'ClassDecoratorFactory' is defined but never used."}}},
          {"\nimport { Foo, Bar } from 'foo';\nfunction baz<Foo>(): Foo {}\nbaz<Bar>();",
           {{"unusedVar", 2, 10, 2, 13}}},
          {"\nimport { Nullable } from 'nullable';\nconst a: string = "
           "'hello';\nconsole.log(a);",
           {{"unusedVar", 2, 10, 2, 18}}},
          {"\nimport { Nullable } from 'nullable';\nimport { SomeOther } from "
           "'other';\nconst a: "
           "Nullable<string> = 'hello';\nconsole.log(a);",
           {{"unusedVar", 3, 10, 3, 19}}},
          {"\nimport { Nullable } from 'nullable';\nimport { Another } from "
           "'some';\nclass A {\n  "
           "do = (a: Nullable) => {\n    console.log(a);\n  };\n}\nnew A();",
           {{"unusedVar", 3, 10, 3, 17}}},
          {"\nimport { Nullable } from 'nullable';\nimport { Another } from "
           "'some';\nexport "
           "interface A {\n  other: Nullable;\n}",
           {{"unusedVar", 3, 10, 3, 17}}},
          {"\nimport { Nullable } from 'nullable';\nfunction foo(): string | null {\n  "
           "return "
           "null;\n}\nfoo();",
           {{"unusedVar", 2, 10, 2, 18}}},
          {"\nimport { Nullable } from 'nullable';\nimport { SomeOther } from "
           "'some';\nimport { "
           "Another } from 'some';\nabstract class A extends Nullable {\n  other: "
           "Nullable<Another>;\n}\nnew A();",
           {{"unusedVar", 3, 10, 3, 19}}},
          {"\nenum FormFieldIds {\n  PHONE = 'phone',\n  EMAIL = 'email',\n}",
           {{"unusedVar", 2, 6, 2, 18, "'FormFieldIds' is defined but never used."}}},
          {"\nimport test from 'test';\nimport baz from 'baz';\nexport interface Bar "
           "extends "
           "baz.test {}",
           {{"unusedVar", 2, 8, 2, 12}}},
          {"\nimport test from 'test';\nimport baz from 'baz';\nexport class Bar "
           "implements "
           "baz.test {}",
           {{"unusedVar", 2, 8, 2, 12}}},
          {"\nnamespace Foo {}", {{"unusedVar", 2, 11, 2, 14}}},
          {"\nnamespace Foo {\n  export const Foo = 1;\n}",
           {{"unusedVar", 2, 11, 2, 14}}},
          {"\nnamespace Foo {\n  const Foo = 1;\n  console.log(Foo);\n}",
           {{"unusedVar", 2, 11, 2, 14}}},
          {"\nnamespace Foo {\n  export const Bar = 1;\n  console.log(Foo.Bar);\n}",
           {{"unusedVar", 2, 11, 2, 14}}},
          {"\nnamespace Foo {\n  namespace Foo {\n    export const Bar = 1;\n    "
           "console.log(Foo.Bar);\n  }\n}",
           {{"unusedVar", 2, 11, 2, 14}, {"unusedVar", 3, 13, 3, 16}}},
          {"\ninterface Foo {\n  bar: string;\n  baz: Foo['bar'];\n}",
           {{"unusedVar", 2, 11, 2, 14}}},
          {"\ntype Foo = Array<Foo>;", {{"unusedVar", 2, 6, 2, 9}}},
          {"\nimport React from 'react';\nimport { Fragment } from 'react';\nexport "
           "const "
           "ComponentFoo = () => {\n  return <div>Foo Foo</div>;\n};",
           {{"unusedVar", 2, 8, 2, 13}, {"unusedVar", 3, 10, 3, 18}},
           nullptr,
           nullptr,
           "test.tsx"},
          {"\ndeclare module 'foo' {\n  type Test = any;\n  const x = 1;\n  export = "
           "x;\n}",
           {{"unusedVar", 3, 8, 3, 12}}},
          {"\nexport namespace Foo {\n  namespace Bar {\n    namespace Baz {\n      "
           "namespace Bam "
           "{\n        const x = 1;\n      }\n    }\n  }\n}",
           {{"unusedVar", 3, 13, 3, 16},
            {"unusedVar", 4, 15, 4, 18},
            {"unusedVar", 5, 17, 5, 20},
            {"unusedVar", 6, 15, 6, 16, "'x' is assigned a value but never used."}}},
          {"\ninterface Foo {\n  a: string;\n}\ninterface Foo {\n  b: Foo;\n}",
           {{"unusedVar", 2, 11, 2, 14}}},
          {"\nlet x = null;\nx = foo(x);",
           {{"unusedVar", 3, 1, 3, 2, "'x' is assigned a value but never used."}}},
          {"\ninterface Foo {\n  bar: string;\n}\nconst Foo = 'bar';",
           {{"unusedVar", 5, 7, 5, 10}}},
          {"\nlet foo = 1;\nfoo += 1;", {{"unusedVar", 3, 1, 3, 4}}},
          {"\ninterface Foo {\n  bar: string;\n}\ntype Bar = 1;\nexport = Bar;",
           {{"unusedVar", 2, 11, 2, 14}}},
          {"\ninterface Foo {\n  bar: string;\n}\ntype Bar = 1;\nexport = Foo;",
           {{"unusedVar", 5, 6, 5, 9}}},
          {"\nnamespace Foo {\n  export const foo = 1;\n}\nexport namespace Bar {\n  "
           "import TheFoo "
           "= Foo;\n}",
           {{"unusedVar", 6, 10, 6, 16}}},
          {"\nconst foo: number = 1;",
           {{"unusedVar", 2, 7, 2, 10, "'foo' is assigned a value but never used."}}},
          {"\nenum Foo {\n  A = 1,\n  B = Foo.A,\n}", {{"unusedVar", 2, 6, 2, 9}}},
          {"\ntype _Foo = 1;\nexport const x: _Foo = 1;",
           {{"usedIgnoredVar",
             2,
             6,
             2,
             10,
             "'_Foo' is marked as ignored but is used. Used vars must not match /^_/u."}},
           nullptr,
           reportUsed},
          {"\ninterface _Foo {}\nexport const x: _Foo = 1;",
           {{"usedIgnoredVar", 2, 11, 2, 15}},
           nullptr,
           reportUsed},
          {"\nenum _Foo {\n  A = 1,\n}\nexport const x = _Foo.A;",
           {{"usedIgnoredVar", 2, 6, 2, 10}},
           nullptr,
           reportUsed},
          {"\nnamespace _Foo {}\nexport const x = _Foo;",
           {{"usedIgnoredVar", 2, 11, 2, 15}},
           nullptr,
           reportUsed},
          {"\nconst foo: number = 1;\nexport type Foo = typeof foo;",
           {{"usedOnlyAsType",
             2,
             7,
             2,
             10,
             "'foo' is assigned a value but only used as a type."}}},
          {"\ndeclare const foo: number;\nexport type Foo = typeof foo;",
           {{"usedOnlyAsType",
             2,
             15,
             2,
             18,
             "'foo' is defined but only used as a type."}}},
          {"\nconst foo: number = 1;\nexport type Foo = typeof foo | string;",
           {{"usedOnlyAsType", 2, 7, 2, 10}}},
          {"\nconst foo = {\n  bar: {\n    baz: 123,\n  },\n};\nexport type Bar = typeof "
           "foo.bar;",
           {{"usedOnlyAsType", 2, 7, 2, 10}}},
          {"\nconst foo = {\n  bar: {\n    baz: 123,\n  },\n};\nexport type Bar = "
           "(typeof "
           "foo)['bar'];",
           {{"usedOnlyAsType", 2, 7, 2, 10}}},
          {"\nconst command = (): ParameterDecorator => {\n  return () => "
           "{};\n};\nexport class "
           "Foo {\n  bar(@command() command: string) {}\n}",
           {{"unusedVar", 6, 18, 6, 25}}},
          {"\ndeclare const deco: () => ParameterDecorator;\nexport class Foo {\n  "
           "bar(@deco() "
           "deco, @deco() param) {}\n}",
           {{"unusedVar", 4, 15, 4, 19}, {"unusedVar", 4, 29, 4, 34}}},
          {"\nexport namespace Foo {\n  const foo: 1234;\n  export {};\n}",
           {{"unusedVar", 3, 9, 3, 12}},
           nullptr,
           nullptr,
           "foo.d.ts"},
          {"\nconst foo: 1234;\nexport {};",
           {{"unusedVar", 2, 7, 2, 10}},
           nullptr,
           nullptr,
           "foo.d.ts"},
          {"\ndeclare module 'foo' {\n  const foo: 1234;\n  export {};\n}",
           {{"unusedVar", 3, 9, 3, 12}},
           nullptr,
           nullptr,
           "foo.d.ts"},
          {"\nexport namespace Foo {\n  const foo: 1234;\n  const bar: 4567;\n  export { "
           "bar };\n}",
           {{"unusedVar", 3, 9, 3, 12}},
           nullptr,
           nullptr,
           "foo.d.ts"},
          {"\nconst foo: 1234;\nconst bar: 4567;\nexport const bazz: 4567;\nexport { bar "
           "};",
           {{"unusedVar", 2, 7, 2, 10}},
           nullptr,
           nullptr,
           "foo.d.ts"},
          {"\ndeclare module 'foo' {\n  const foo: string;\n  const bar: number;\n  "
           "export default "
           "bar;\n}",
           {{"unusedVar", 3, 9, 3, 12}},
           nullptr,
           nullptr,
           "foo.d.ts"},
          {"\nconst foo: string;\nexport const bar: number;\nexport * from '...';",
           {{"unusedVar", 2, 7, 2, 10}},
           nullptr,
           nullptr,
           "foo.d.ts"},
          {"\nnamespace Foo {\n  type Foo = 1;\n  type Bar = 1;\n  export = Bar;\n}",
           {{"unusedVar", 3, 8, 3, 11}},
           nullptr,
           nullptr,
           "foo.d.ts"},
          {"\ndeclare module 'foo' {\n  type Test = 1;\n  export {};\n}",
           {{"unusedVar", 3, 8, 3, 12}},
           nullptr,
           nullptr,
           "foo.d.ts"},
          {"\nexport declare namespace Foo {\n  namespace Bar {\n    namespace Baz {\n   "
           "   "
           "namespace Bam {\n        const x = 1;\n      }\n      export {};\n    }\n  "
           "}\n}",
           {{"unusedVar", 5, 17, 5, 20}}},
          {"\ndeclare module 'foo' {\n  namespace Bar {\n    namespace Baz {\n      "
           "namespace Bam "
           "{\n        const x = 1;\n      }\n      export {};\n    }\n  }\n}",
           {{"unusedVar", 5, 17, 5, 20}}},
          {"\ndeclare enum Foo {}\nexport {};", {{"unusedVar", 2, 14, 2, 17}}},
          {"\ndeclare class Bar {}\nexport {};",
           {{"unusedVar", 2, 15, 2, 18}},
           nullptr,
           nullptr,
           "foo.d.ts"},
          {"\nclass Foo {}\nexport {};",
           {{"unusedVar", 2, 7, 2, 10}},
           nullptr,
           nullptr,
           "foo.d.ts"},
          {"\nusing resource = getResource();",
           {{"unusedVar",
             2,
             7,
             2,
             15,
             "'resource' is assigned a value but never used."}}},
          {"\nawait using resource = getResource();", {{"unusedVar", 2, 13, 2, 21}}},
          {"\nexport const myTypeGuard2 = (data2: unknown): typeof data2 => {\n  return "
           "true;\n};",
           {{"usedOnlyAsType",
             2,
             30,
             2,
             35,
             "'data2' is defined but only used as a type."}}},
          {"\nexport const myTypeGuard = (data: unknown): data is string => {\n  return "
           "true;\n};",
           {{"usedOnlyAsType", 2, 29, 2, 33}}},
      });
}

TEST(rules_no_unused_vars, eslint_cases)
{
  const char *all = R"(["all"])";
  const char *local = R"(["local"])";
  test::runRuleTests(
      rules::kNoUnusedVars,
      {
          {"\nvar foo = 5;\nlabel: while (true) {\n  console.log(foo);\n  break "
           "label;\n}"},
          {"\nfor (let prop in box) {\n  box[prop] = parseInt(box[prop]);\n}"},
          {"\nvar box = { a: 2 };\nfor (var prop in box) {\n  box[prop] = "
           "parseInt(box[prop]);\n}"},
          {"\nf({\n  set foo(a) {\n    return;\n  },\n});"},
          {"\na;\nvar a;", all},
          {"\nvar a = 10;\nalert(a);", all},
          {"\nvar a = 10;\n(function () {\n  setTimeout(function () {\n    alert(a);\n  "
           "}, "
           "0);\n})();",
           all},
          {"\nvar a = 10;\nd[a] = 0;", all},
          {"(function g() {})();", all},
          {"\nvar c = 0;\nfunction f(a) {\n  var b = a;\n  return b;\n}\nf(c);", all},
          {"\nfunction a(x, y) {\n  return y;\n}\na();", all},
          {"\nvar arr1 = [1, 2];\nvar arr2 = [3, 4];\nfor (var i in arr1) {\n  arr1[i] = "
           "5;\n}\nfor "
           "(var i in arr2) {\n  arr2[i] = 10;\n}",
           all},
          {"var a = 10;", local, "test.js"},
          {"\nvar min = 'min';\nMath[min];", all},
          {"\nFoo.bar = function (baz) {\n  return baz;\n};", all},
          {"myFunc(function foo() {}.bind(this));"},
          {"myFunc(function foo() {}.toString());"},
          {"\nfunction foo(first, second) {\n  doStuff(function () {\n    "
           "console.log(second);\n  });\n}\nfoo();"},
          {"\n(function () {\n  var doSomething = function doSomething() {};\n  "
           "doSomething();\n})();"},
          {"\nfunction g(bar, baz) {\n  return baz;\n}\ng();", R"([{"vars": "all"}])"},
          {"\nfunction g(bar, baz) {\n  return bar;\n}\ng();",
           R"([{"args": "none", "vars": "all"}])"},
          {"\nfunction g(bar, baz) {\n  return 2;\n}\ng();",
           R"([{"args": "none", "vars": "all"}])"},
          {"\nfunction g(bar, baz) {\n  return bar + baz;\n}\ng();",
           R"([{"args": "all", "vars": "local"}])"},
          {"\nvar g = function (bar, baz) {\n  return 2;\n};\ng();",
           R"([{"args": "none", "vars": "all"}])"},
          {"\n(function z() {\n  z();\n})();"},
          {" "},
          {"\nvar who = 'Paul';\nmodule.exports = `Hello ${who}!`;"},
          {"export var foo = 123;"},
          {"export function foo() {}"},
          {"\nlet toUpper = partial => partial.toUpperCase;\nexport { toUpper };"},
          {"export class foo {}"},
          {"\nclass Foo {}\nvar x = new Foo();\nx.foo();"},
          {"\nconst foo = 'hello!';\nfunction bar(foobar = foo) {\n  "
           "foobar.replace(/!$/, ' "
           "world!');\n}\nbar();"},
          {"\nfunction Foo() {}\nvar x = new Foo();\nx.foo();"},
          {"\nfunction foo() {\n  var foo = 1;\n  return foo;\n}\nfoo();"},
          {"\nfunction foo(foo) {\n  return foo;\n}\nfoo(1);"},
          {"\nfunction foo() {\n  function foo() {\n    return 1;\n  }\n  return "
           "foo();\n}\nfoo();"},
          {"\nconst x = 1;\nconst [y = x] = [];\nfoo(y);"},
          {"\nconst x = 1;\nconst { y = x } = {};\nfoo(y);"},
          {"\nconst x = [];\nconst { z: [y] = x } = {};\nfoo(y);"},
          {"\nconst x = 1;\nlet y;\n[y = x] = [];\nfoo(y);"},
          {"\nconst x = 1;\nfunction foo(y = x) {\n  bar(y);\n}\nfoo();"},
          {"\nconst x = 1;\nfunction foo({ y = x } = {}) {\n  bar(y);\n}\nfoo();"},
          {"\nconst x = 1;\nfunction foo(\n  y = function (z = x) {\n    bar(z);\n  "
           "},\n) {\n  "
           "y();\n}\nfoo();"},
          {"\nvar x = 1;\nvar { y = x } = {};\nfoo(y);"},
          {"var _a;", R"([{"vars": "all", "varsIgnorePattern": "^_"}])"},
          {"\nvar a;\nfunction foo() {\n  var _b;\n}\nfoo();",
           R"([{"vars": "local", "varsIgnorePattern": "^_"}])",
           "test.js"},
          {"\nfunction foo(_a) {}\nfoo();",
           R"([{"args": "all", "argsIgnorePattern": "^_"}])"},
          {"\nfunction foo(a, _b) {\n  return a;\n}\nfoo();",
           R"([{"args": "after-used", "argsIgnorePattern": "^_"}])"},
          {"\nvar [firstItemIgnored, secondItem] = items;\nconsole.log(secondItem);",
           R"([{"vars": "all", "varsIgnorePattern": "[iI]gnored"}])"},
          {"\nconst [a, _b, c] = items;\nconsole.log(a + c);",
           R"([{"destructuredArrayIgnorePattern": "^_"}])"},
          {"\nconst [[a, _b, c]] = items;\nconsole.log(a + c);",
           R"([{"destructuredArrayIgnorePattern": "^_"}])"},
          {"\nconst {\n  x: [_a, foo],\n} = bar;\nconsole.log(foo);",
           R"([{"destructuredArrayIgnorePattern": "^_"}])"},
          {"\nfunction baz([_b, foo]) {\n  foo;\n}\nbaz();",
           R"([{"destructuredArrayIgnorePattern": "^_"}])"},
          {"\nlet _a, b;\nfoo.forEach(item => {\n  [_a, b] = item;\n  "
           "doSomething(b);\n});",
           R"([{"destructuredArrayIgnorePattern": "^_"}])"},
          {"\nlet _x, y;\n_x = 1;\n[_x, y] = foo;\ny;\nlet _a, b;\n[_a, b] = foo;\n_a = "
           "1;\nb;",
           R"([{"destructuredArrayIgnorePattern": "^_"}])"},
          {"\n(function (obj) {\n  var name;\n  for (name in obj) return;\n})({});"},
          {"\n(function (obj) {\n  var name;\n  for (name in obj) {\n    return;\n  "
           "}\n})({});"},
          {"\n(function (obj) {\n  for (var name in obj) {\n    return true;\n  "
           "}\n})({});"},
          {"\n(function (obj) {\n  for (const name in obj) return true;\n})({});"},
          {"\n(function (iter) {\n  let name;\n  for (name of iter) return;\n})({});"},
          {"\n(function (iter) {\n  for (let name of iter) {\n    return true;\n  "
           "}\n})({});"},
          {"\nlet x = 0;\nfoo = (0, x++);"},
          {"\nlet x = 0;\nfoo = (0, (x += 1));"},
          {"\nlet x = 0;\nfoo = (0, (x = x + 1));"},
          {"\ntry {\n} catch (err) {}", R"([{"caughtErrors": "none"}])"},
          {"\ntry {\n} catch (err) {\n  console.error(err);\n}",
           R"([{"caughtErrors": "all"}])"},
          {"\ntry {\n} catch (ignoreErr) {}",
           R"([{"caughtErrorsIgnorePattern": "^ignore"}])"},
          {"\nconst data = { type: 'coords', x: 1, y: 2 };\nconst { type, ...coords } = "
           "data;\nconsole.log(coords);",
           R"([{"ignoreRestSiblings": true}])"},
          {"\nvar a = 0,\n  b;\nb = a = a + 1;\nfoo(b);"},
          {"\nvar a = 0,\n  b;\nb = a += a + 1;\nfoo(b);"},
          {"\nvar a = 0,\n  b;\nb = a++;\nfoo(b);"},
          {"\nfunction foo(a) {\n  var b = (a = a + 1);\n  bar(b);\n}\nfoo();"},
          {"\nfunction foo(a) {\n  var b = a++;\n  bar(b);\n}\nfoo();"},
          {"\nvar unregisterFooWatcher;\nunregisterFooWatcher = $scope.$watch('foo', "
           "function () "
           "{\n  unregisterFooWatcher();\n});"},
          {"\nvar ref;\nref = setInterval(function () {\n  clearInterval(ref);\n}, 10);"},
          {"\nvar _timer;\nfunction f() {\n  _timer = setTimeout(function () {}, _timer "
           "? 100 : "
           "0);\n}\nf();"},
          {"\nfunction foo(cb) {\n  cb = (function () {\n    function something(a) {\n   "
           "   cb(1 "
           "+ a);\n    }\n    register(something);\n  })();\n}\nfoo();"},
          {"\nfunction* foo(cb) {\n  cb = yield function (a) {\n    cb(1 + a);\n  "
           "};\n}\nfoo();"},
          {"\nfunction foo(cb) {\n  cb = tag`hello${function (a) {\n    cb(1 + a);\n  "
           "}}`;\n}\nfoo();"},
          {"\nfunction foo(cb) {\n  var b;\n  cb = b = function (a) {\n    cb(1 + a);\n  "
           "};\n  "
           "b();\n}\nfoo();"},
          {"\nfunction someFunction() {\n  var a = 0,\n    i;\n  for (i = 0; i < 2; i++) "
           "{\n    a "
           "= myFunction(a);\n  }\n}\nsomeFunction();"},
          {"\n(function (a, b, { c, d }) {\n  d;\n});",
           R"([{"argsIgnorePattern": "c"}])"},
          {"\n(function (a, b, c) {\n  c;\n});", R"([{"argsIgnorePattern": "c"}])"},
          {"\n(class {\n  set foo(UNUSED) {}\n});"},
          {"\nclass Foo {\n  set bar(UNUSED) {}\n}\nconsole.log(Foo);"},
          {"({ a, ...rest }) => rest;",
           R"([{"args": "all", "ignoreRestSiblings": true}])"},
          {"\nlet foo, rest;\n({ foo, ...rest } = something);\nconsole.log(rest);",
           R"([{"ignoreRestSiblings": true}])"},
          {"\nvar a = function () {\n  a();\n};\na();"},
          {"\nconst a = () => () => {\n  a();\n};\na();"},
          {"export * as ns from 'source';"},
          {"import.meta;"},
          {"\nvar a;\na ||= 1;"},
          {"\nclass Foo {\n  static {}\n}",
           R"([{"ignoreClassWithStaticInitBlock": true}])"},
          {"\nclass Foo {\n  static {}\n}",
           R"([{"ignoreClassWithStaticInitBlock": false, "varsIgnorePattern": "^Foo"}])"},
          {"\nconst a = 5;\nconst _c = a + 5;",
           R"([{"args": "all", "reportUsedIgnorePattern": true, "varsIgnorePattern": "^_"}])"},
          {"\n(function foo(a, _b) {\n  return a + 5;\n})(5);",
           R"([{"args": "all", "argsIgnorePattern": "^_", "reportUsedIgnorePattern": true}])"},
      },
      {
          {"\nfunction foox() {\n  return foox();\n}",
           {{"unusedVar", 2, 10, 2, 14, "'foox' is defined but never used."}}},
          {"\n(function () {\n  function foox() {\n    if (true) {\n      return "
           "foox();\n    "
           "}\n  }\n})();",
           {{"unusedVar", 3, 12, 3, 16}}},
          {"var a = 10;",
           {{"unusedVar", 1, 5, 1, 6, "'a' is assigned a value but never used."}}},
          {"\nfunction f() {\n  var a = 1;\n  return function () {\n    f((a *= 2));\n  "
           "};\n}",
           {{"unusedVar", 2, 10, 2, 11}}},
          {"\nfunction f() {\n  var a = 1;\n  return function () {\n    f(++a);\n  };\n}",
           {{"unusedVar", 2, 10, 2, 11}}},
          {"\nfunction foo(first, second) {\n  doStuff(function () {\n    "
           "console.log(second);\n  "
           "});\n}",
           {{"unusedVar", 2, 10, 2, 13}}},
          {"\nvar a = 10;\na = 20;", {{"unusedVar", 3, 1, 3, 2}}, nullptr, all},
          {"\nvar a = 10;\n(function () {\n  var a = 1;\n  alert(a);\n})();",
           {{"unusedVar", 2, 5, 2, 6}},
           nullptr,
           all},
          {"\nvar a = 10,\n  b = 0,\n  c = null;\nalert(a + b);",
           {{"unusedVar", 4, 3, 4, 4}},
           nullptr,
           all},
          {"\nvar a = 10,\n  b = 0,\n  c = null;\nsetTimeout(function () {\n  var b = "
           "2;\n  var c = "
           "2;\n  alert(a + b + c);\n}, 0);",
           {{"unusedVar", 3, 3, 3, 4}, {"unusedVar", 4, 3, 4, 4}},
           nullptr,
           all},
          {"\nfunction f() {\n  var a = [];\n  return a.map(function g() {});\n}",
           {{"unusedVar", 2, 10, 2, 11}},
           nullptr,
           all},
          {"\nfunction foo() {\n  function foo(x) {\n    return x;\n  }\n  return "
           "function () {\n  "
           "  return foo;\n  };\n}",
           {{"unusedVar", 2, 10, 2, 13}}},
          {"\nfunction f() {\n  var x;\n  function a() {\n    x = 42;\n  }\n  function "
           "b() {\n    "
           "alert(x);\n  }\n}",
           {{"unusedVar", 2, 10, 2, 11},
            {"unusedVar", 4, 12, 4, 13},
            {"unusedVar", 7, 12, 7, 13}},
           nullptr,
           all},
          {"\nfunction f(a) {}\nf();", {{"unusedVar", 2, 12, 2, 13}}, nullptr, all},
          {"\nfunction a(x, y, z) {\n  return y;\n}\na();",
           {{"unusedVar", 2, 18, 2, 19}},
           nullptr,
           all},
          {"var min = Math.min;", {{"unusedVar", 1, 5, 1, 8}}, nullptr, all},
          {"\nFoo.bar = function (baz) {\n  return 1;\n};",
           {{"unusedVar", 2, 21, 2, 24}},
           nullptr,
           all},
          {"\nfunction gg(baz, bar) {\n  return baz;\n}\ngg();",
           {{"unusedVar", 2, 18, 2, 21}},
           nullptr,
           R"([{"vars": "all"}])"},
          {"\n(function (foo, baz, bar) {\n  return baz;\n})();",
           {{"unusedVar", 2, 22, 2, 25}},
           nullptr,
           R"([{"args": "after-used", "vars": "all"}])"},
          {"\n(function (foo, baz, bar) {\n  return baz;\n})();",
           {{"unusedVar", 2, 12, 2, 15}, {"unusedVar", 2, 22, 2, 25}},
           nullptr,
           R"([{"args": "all", "vars": "all"}])"},
          {"\n(function z(foo) {\n  var bar = 33;\n})();",
           {{"unusedVar", 2, 13, 2, 16}, {"unusedVar", 3, 7, 3, 10}},
           nullptr,
           R"([{"args": "all", "vars": "all"}])"},
          {"\n(function z(foo) {\n  z();\n})();",
           {{"unusedVar", 2, 13, 2, 16}},
           nullptr,
           "[{}]"},
          {"\nfunction f() {\n  var a = 1;\n  return function () {\n    f((a = 2));\n  "
           "};\n}",
           {{"unusedVar", 2, 10, 2, 11}, {"unusedVar", 3, 7, 3, 8}},
           nullptr,
           "[{}]"},
          {"import x from 'y';", {{"unusedVar", 1, 8, 1, 9}}},
          {"\nexport function fn2({ x, y }) {\n  console.log(x);\n}",
           {{"unusedVar", 2, 26, 2, 27}}},
          {"\nexport function fn2(x, y) {\n  console.log(x);\n}",
           {{"unusedVar", 2, 24, 2, 25}}},
          {"\nvar _a;\nvar b;",
           {{"unusedVar",
             3,
             5,
             3,
             6,
             "'b' is defined but never used. Allowed unused vars must match /^_/u."}},
           nullptr,
           R"([{"vars": "all", "varsIgnorePattern": "^_"}])"},
          {"\nvar a;\nfunction foo() {\n  var _b;\n  var c_;\n}\nfoo();",
           {{"unusedVar", 5, 7, 5, 9}},
           nullptr,
           R"([{"vars": "local", "varsIgnorePattern": "^_"}])",
           "test.js"},
          {"\nfunction foo(a, _b) {}\nfoo();",
           {{"unusedVar",
             2,
             14,
             2,
             15,
             "'a' is defined but never used. Allowed unused args must match /^_/u."}},
           nullptr,
           R"([{"args": "all", "argsIgnorePattern": "^_"}])"},
          {"\nfunction foo(a, _b, c) {\n  return a;\n}\nfoo();",
           {{"unusedVar", 2, 21, 2, 22}},
           nullptr,
           R"([{"args": "after-used", "argsIgnorePattern": "^_"}])"},
          {"\nfunction foo(_a) {}\nfoo();",
           {{"unusedVar",
             2,
             14,
             2,
             16,
             "'_a' is defined but never used. Allowed unused args must match "
             "/[iI]gnored/u."}},
           nullptr,
           R"([{"args": "all", "argsIgnorePattern": "[iI]gnored"}])"},
          {"var [firstItemIgnored, secondItem] = items;",
           {{"unusedVar", 1, 24, 1, 34}},
           nullptr,
           R"([{"vars": "all", "varsIgnorePattern": "[iI]gnored"}])"},
          {"\nconst array = ['a', 'b', 'c'];\nconst [a, _b, c] = array;\nconst newArray "
           "= [a, c];",
           {{"unusedVar", 4, 7, 4, 15}},
           nullptr,
           R"([{"destructuredArrayIgnorePattern": "^_"}])"},
          {"\nconst array = ['a', 'b', 'c', 'd', 'e'];\nconst [a, _b, c] = array;",
           {{"unusedVar",
             3,
             8,
             3,
             9,
             "'a' is assigned a value but never used. Allowed unused elements of array "
             "destructuring must match /^_/u."},
            {"unusedVar", 3, 15, 3, 16}},
           nullptr,
           R"([{"destructuredArrayIgnorePattern": "^_"}])"},
          {"\nconst array = [obj];\nconst [{ _a, foo }] = array;\nconsole.log(foo);",
           {{"unusedVar", 3, 10, 3, 12}},
           nullptr,
           R"([{"destructuredArrayIgnorePattern": "^_"}])"},
          {"\nlet _a, b;\nfoo.forEach(item => {\n  [a, b] = item;\n});",
           {{"unusedVar", 2, 5, 2, 7}, {"unusedVar", 2, 9, 2, 10}},
           nullptr,
           R"([{"destructuredArrayIgnorePattern": "^_"}])"},
          {"\n(function (obj) {\n  var name;\n  for (name in obj) {\n    i();\n    "
           "return;\n  "
           "}\n})({});",
           {{"unusedVar", 4, 8, 4, 12, "'name' is assigned a value but never used."}}},
          {"\n(function (obj) {\n  for (var name in obj) {\n  }\n})({});",
           {{"unusedVar", 3, 12, 3, 16}}},
          {"\n(function (iter) {\n  var name;\n  for (name of iter) {\n  }\n})({});",
           {{"unusedVar", 4, 8, 4, 12}}},
          {"\nconst data = { type: 'coords', x: 1, y: 2 };\nconst { type, ...coords } = "
           "data;\nconsole.log(coords);",
           {{"unusedVar", 3, 9, 3, 13}}},
          {"\nconst data = { type: 'coords', x: 2, y: 2 };\nconst { type, ...coords } = "
           "data;\nconsole.log(type);",
           {{"unusedVar", 3, 18, 3, 24}},
           nullptr,
           R"([{"ignoreRestSiblings": true}])"},
          {"\nlet type, coords;\n({ type, ...coords } = data);\nconsole.log(type);",
           {{"unusedVar", 3, 13, 3, 19}},
           nullptr,
           R"([{"ignoreRestSiblings": true}])"},
          {"\nconst data = { vars: ['x', 'y'], x: 1, y: 2 };\nconst {\n  vars: [x],\n  "
           "...coords\n} = data;\nconsole.log(coords);",
           {{"unusedVar", 4, 10, 4, 11}}},
          {"({ a, ...rest }) => {};",
           {{"unusedVar", 1, 10, 1, 14}},
           nullptr,
           R"([{"args": "all", "ignoreRestSiblings": true}])"},
          {"export default function (a) {}", {{"unusedVar", 1, 26, 1, 27}}},
          {"\nexport default function (a, b) {\n  console.log(a);\n}",
           {{"unusedVar", 2, 29, 2, 30}}},
          {"export default (function (a) {});", {{"unusedVar", 1, 27, 1, 28}}},
          {"export default a => {};", {{"unusedVar", 1, 16, 1, 17}}},
          {"\ntry {\n} catch (err) {}", {{"unusedVar", 3, 10, 3, 13}}},
          {"\ntry {\n} catch (err) {}",
           {{"unusedVar",
             3,
             10,
             3,
             13,
             "'err' is defined but never used. Allowed unused caught errors must match "
             "/^ignore/u."}},
           nullptr,
           R"([{"caughtErrors": "all", "caughtErrorsIgnorePattern": "^ignore"}])"},
          {"\ntry {\n} catch (err) {}",
           {{"unusedVar", 3, 10, 3, 13}},
           nullptr,
           R"([{"caughtErrors": "all", "varsIgnorePattern": "^err"}])"},
          {"\ntry {\n} catch (ignoreErr) {}\ntry {\n} catch (err) {}",
           {{"unusedVar", 5, 10, 5, 13}},
           nullptr,
           R"([{"caughtErrors": "all", "caughtErrorsIgnorePattern": "^ignore"}])"},
          {"\nvar a = 0;\na = a + 1;", {{"unusedVar", 3, 1, 3, 2}}},
          {"\nvar a = 0;\na = a + a;", {{"unusedVar", 3, 1, 3, 2}}},
          {"\nvar a = 0;\na += a + 1;", {{"unusedVar", 3, 1, 3, 2}}},
          {"\nvar a = 0;\na++;", {{"unusedVar", 3, 1, 3, 2}}},
          {"\nfunction foo(a) {\n  a = a + 1;\n}\nfoo();", {{"unusedVar", 3, 3, 3, 4}}},
          {"\nfunction foo(a) {\n  a++;\n}\nfoo();", {{"unusedVar", 3, 3, 3, 4}}},
          {"\nvar a = 3;\na = a * 5 + 6;", {{"unusedVar", 3, 1, 3, 2}}},
          {"\nvar a = 2,\n  b = 4;\na = a * 2 + b;", {{"unusedVar", 4, 1, 4, 2}}},
          {"\nfunction foo(cb) {\n  cb = function (a) {\n    cb(1 + a);\n  };\n  "
           "bar(not_cb);\n}\nfoo();",
           {{"unusedVar", 3, 3, 3, 5}}},
          {"\nfunction foo(cb) {\n  cb = (function (a) {\n    return cb(1 + a);\n  "
           "})();\n}\nfoo();",
           {{"unusedVar", 3, 3, 3, 5}}},
          {"\nfunction foo(cb) {\n  cb =\n    (function (a) {\n      cb(1 + a);\n    "
           "},\n    "
           "cb);\n}\nfoo();",
           {{"unusedVar", 3, 3, 3, 5}}},
          {"\nfunction foo(cb) {\n  cb =\n    (0,\n    function (a) {\n      cb(1 + "
           "a);\n    "
           "});\n}\nfoo();",
           {{"unusedVar", 3, 3, 3, 5}}},
          {"\nwhile (a) {\n  function foo(b) {\n    b = b + 1;\n  }\n  foo();\n}",
           {{"unusedVar", 4, 5, 4, 6}}},
          {"(function (a, b, c) {});",
           {{"unusedVar",
             1,
             12,
             1,
             13,
             "'a' is defined but never used. Allowed unused args must match /c/u."},
            {"unusedVar", 1, 15, 1, 16}},
           nullptr,
           R"([{"argsIgnorePattern": "c"}])"},
          {"(function (a, b, { c, d }) {});",
           {{"unusedVar", 1, 12, 1, 13},
            {"unusedVar", 1, 15, 1, 16},
            {"unusedVar", 1, 23, 1, 24}},
           nullptr,
           R"([{"argsIgnorePattern": "c"}])"},
          {"\n(function ({ a }, b) {\n  return b;\n})();", {{"unusedVar", 2, 14, 2, 15}}},
          {"\n(function ({ a }, { b, c }) {\n  return b;\n})();",
           {{"unusedVar", 2, 14, 2, 15}, {"unusedVar", 2, 24, 2, 25}}},
          {"\nlet x = 0;\n(x++, (x = 0));", {{"unusedVar", 3, 8, 3, 9}}},
          {"\nlet x = 0;\n(x++, (x = 0));\nx = 3;", {{"unusedVar", 4, 1, 4, 2}}},
          {"\nlet x = 0;\n(x++, 0);", {{"unusedVar", 3, 2, 3, 3}}},
          {"\nlet x = 0;\n(0, x++);", {{"unusedVar", 3, 5, 3, 6}}},
          {"\nlet x = 0;\nfoo = (x++, 0);", {{"unusedVar", 3, 8, 3, 9}}},
          {"\nlet x = 0;\n((x += 1), 0);", {{"unusedVar", 3, 3, 3, 4}}},
          {"\nlet x = 0;\n(0, (x += 1));", {{"unusedVar", 3, 6, 3, 7}}},
          {"\nlet z = 0;\n((z = z + 1), (z = 2));", {{"unusedVar", 3, 16, 3, 17}}},
          {"\nlet z = 0;\n((z = z + 1), (z = 2));\nz = z + 3;",
           {{"unusedVar", 4, 1, 4, 2}}},
          {"\nlet x = 0;\n(0, (x = x + 1));", {{"unusedVar", 3, 6, 3, 7}}},
          {"\nlet x = 0;\nfoo = ((x = x + 1), 0);", {{"unusedVar", 3, 9, 3, 10}}},
          {"\n(function ([a], b) {\n  return b;\n})();", {{"unusedVar", 2, 13, 2, 14}}},
          {"\n(function ([a, b], [c]) {\n  return b;\n})();",
           {{"unusedVar", 2, 13, 2, 14}, {"unusedVar", 2, 21, 2, 22}}},
          {"(function (_a) {})();",
           {{"unusedVar", 1, 12, 1, 14}},
           nullptr,
           R"([{"args": "all", "varsIgnorePattern": "^_"}])"},
          {"\nvar a = function () {\n  a();\n};", {{"unusedVar", 2, 5, 2, 6}}},
          {"\nvar a = function () {\n  return function () {\n    a();\n  };\n};",
           {{"unusedVar", 2, 5, 2, 6}}},
          {"\nconst a = () => () => {\n  a();\n};", {{"unusedVar", 2, 7, 2, 8}}},
          {"\nlet myArray = [1, 2, 3, 4].filter(x => x == 0);\nmyArray = "
           "myArray.filter(x => x == "
           "1);",
           {{"unusedVar", 3, 1, 3, 8}}},
          {"\nconst a = 1;\na += 1;", {{"unusedVar", 3, 1, 3, 2}}},
          {"\nlet x = [];\nx = x.concat(x);", {{"unusedVar", 3, 1, 3, 2}}},
          {"\nlet a = 'a';\na = 10;\nfunction foo() {\n  a = 11;\n  a = () => {\n    a = "
           "13;\n  "
           "};\n}",
           {{"unusedVar", 3, 1, 3, 2}, {"unusedVar", 4, 10, 4, 13}}},
          {"\nlet foo;\ninit();\nfoo = foo + 2;\nfunction init() {\n  foo = 1;\n}",
           {{"unusedVar", 4, 1, 4, 4}}},
          {"\nfunction foo(n) {\n  if (n < 2) return 1;\n  return n * foo(n - 1);\n}",
           {{"unusedVar", 2, 10, 2, 13}}},
          {"\nlet c = 'c';\nc = 10;\nfunction foo1() {\n  c = 11;\n  c = () => {\n    c "
           "= 13;\n  "
           "};\n}\nc = foo1;",
           {{"unusedVar", 10, 1, 10, 2}}},
          {"\nclass Foo {\n  static {}\n}",
           {{"unusedVar", 2, 7, 2, 10}},
           nullptr,
           R"([{"ignoreClassWithStaticInitBlock": false}])"},
          {"\nclass Foo {\n  static {}\n}", {{"unusedVar", 2, 7, 2, 10}}},
          {"\nclass Foo {\n  static {\n    var bar;\n  }\n}",
           {{"unusedVar", 4, 9, 4, 12}},
           nullptr,
           R"([{"ignoreClassWithStaticInitBlock": true}])"},
          {"class Foo {}",
           {{"unusedVar", 1, 7, 1, 10}},
           nullptr,
           R"([{"ignoreClassWithStaticInitBlock": true}])"},
          {"\nconst _a = 5;\nconst _b = _a + 5;",
           {{"usedIgnoredVar",
             2,
             7,
             2,
             9,
             "'_a' is marked as ignored but is used. Used vars must not match /^_/u."}},
           nullptr,
           R"([{"args": "all", "reportUsedIgnorePattern": true, "varsIgnorePattern": "^_"}])"},
          {"\n(function foo(_a) {\n  return _a + 5;\n})(5);",
           {{"usedIgnoredVar",
             2,
             15,
             2,
             17,
             "'_a' is marked as ignored but is used. Used args must not match /^_/u."}},
           nullptr,
           R"([{"args": "all", "argsIgnorePattern": "^_", "reportUsedIgnorePattern": true}])"},
          {"\nconst [a, _b] = items;\nconsole.log(a + _b);",
           {{"usedIgnoredVar",
             2,
             11,
             2,
             13,
             "'_b' is marked as ignored but is used. Used elements of array "
             "destructuring must "
             "not match /^_/u."}},
           nullptr,
           R"([{"destructuredArrayIgnorePattern": "^_", "reportUsedIgnorePattern": true}])"},
          {"\ntry {\n} catch (_err) {\n  console.error(_err);\n}",
           {{"usedIgnoredVar",
             3,
             10,
             3,
             14,
             "'_err' is marked as ignored but is used. Used caught errors must not match "
             "/^_/u."}},
           nullptr,
           R"([{"caughtErrors": "all", "caughtErrorsIgnorePattern": "^_", "reportUsedIgnorePattern": true}])"},
          {"\ntry {\n} catch (_) {\n  _ = 'foo';\n}",
           {{"unusedVar",
             4,
             3,
             4,
             4,
             "'_' is assigned a value but never used. Allowed unused caught errors must "
             "match "
             "/foo/u."}},
           nullptr,
           R"([{"caughtErrorsIgnorePattern": "foo"}])"},
          {"\ntry {\n} catch ({ message, errors: [firstError] }) {}",
           {{"unusedVar", 3, 12, 3, 19}, {"unusedVar", 3, 30, 3, 40}},
           nullptr,
           R"([{"caughtErrorsIgnorePattern": "foo"}])"},
          {"\n_ => {\n  _ = _ + 1;\n};",
           {{"unusedVar",
             3,
             3,
             3,
             4,
             "'_' is assigned a value but never used. Allowed unused args must match "
             "/ignored/u."}},
           nullptr,
           R"([{"argsIgnorePattern": "ignored", "varsIgnorePattern": "_"}])"},
      });
}

TEST(rules_no_unused_vars, import_removal)
{
  const char *autofix = R"([{"enableAutofixRemoval": {"imports": true}}])";
  test::runRuleTests(
      rules::kNoUnusedVars,
      {},
      {
          {"import { A } from 'a';\nexport const x = 1;\n",
           {{"unusedVar"}},
           "export const x = 1;\n",
           autofix},
          {"import { A, B } from 'a';\nexport const x: B = 1;\n",
           {{"unusedVar"}},
           "import { B } from 'a';\nexport const x: B = 1;\n",
           autofix},
          {"import A, { B } from 'a';\nexport const x: B = 1;\n",
           {{"unusedVar"}},
           "import { B } from 'a';\nexport const x: B = 1;\n",
           autofix},
          {"import * as A from 'a';\nexport const x = 1;\n",
           {{"unusedVar"}},
           "export const x = 1;\n",
           autofix},
          // Without the option the removal is only a suggestion.
          {"import { A } from 'a';\nexport const x = 1;\n", {{"unusedVar"}}},
      });
}
