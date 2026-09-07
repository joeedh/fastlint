#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST(rules_no_var, cases)
{
  const char *unexpected = "Unexpected var, use let or const instead.";
  test::runRuleTests(
      rules::kNoVar,
      {
          {"const JOE = 'schmoe';"},
          {"let moo = 'car';"},
          {"for (const x of y) {}"},
          {"declare global { var x: number; }"},
      },
      {
          {"var foo = bar;",
           {{"unexpectedVar", 1, 1, 1, 15, unexpected}},
           "let foo = bar;"},
          {"var foo = bar, toast = most;",
           {{"unexpectedVar"}},
           "let foo = bar, toast = most;"},
          {"var foo = bar; let toast = most;",
           {{"unexpectedVar"}},
           "let foo = bar; let toast = most;"},
          {"for (var a of b) { console.log(a); }",
           {{"unexpectedVar"}},
           "for (let a of b) { console.log(a); }"},
          {"for (var a in b) { console.log(a); }",
           {{"unexpectedVar"}},
           "for (let a in b) { console.log(a); }"},
          {"for (var i = 0; i < 10; i++) {}",
           {{"unexpectedVar"}},
           "for (let i = 0; i < 10; i++) {}"},
          {"function f() { var x = 1; return x; }",
           {{"unexpectedVar"}},
           "function f() { let x = 1; return x; }"},
          {"while (a) { var b = 1; b; }",
           {{"unexpectedVar"}},
           "while (a) { let b = 1; b; }"},
          // A script's top-level `var` is a global-object property.
          {"var foo = bar;", {{"unexpectedVar"}}, nullptr, nullptr, "test.js"},
          // The same declaration in a module is fixable.
          {"export {}; var foo = bar;",
           {{"unexpectedVar"}},
           "export {}; let foo = bar;",
           nullptr,
           "test.js"},
          // Redeclarations need `var`.
          {"var a; a = 1; var a;", {{"unexpectedVar"}, {"unexpectedVar"}}},
          // A reference outside the block would break.
          {"if (x) { var y = 1; } y;", {{"unexpectedVar"}}},
          // A reference before the declaration would hit the temporal dead zone.
          {"y; var y = 1;", {{"unexpectedVar"}}},
          {"var self = self;", {{"unexpectedVar"}}},
          {"var a = 1, b = a + c(a);", {{"unexpectedVar"}}, "let a = 1, b = a + c(a);"},
          // A `case` clause is not a block.
          {"switch (x) { case 1: var z = 1; z; }", {{"unexpectedVar"}}},
          // A closure in a loop sees every iteration's `let`, not one `var`.
          {"for (;;) { var i = 1; setTimeout(() => i); }", {{"unexpectedVar"}}},
          {"for (;;) { var i; i = 1; }", {{"unexpectedVar"}}},
          {"var let = 1;", {{"unexpectedVar"}}, nullptr, nullptr, "test.js"},
          {"var {a, b} = c; a; b;", {{"unexpectedVar"}}, "let {a, b} = c; a; b;"},
      });
}
