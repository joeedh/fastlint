#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST(rules_no_unreachable, cases)
{
  test::runRuleTests(
      rules::kNoUnreachable,
      {
          {"function foo() { function bar() { return 1; } return bar(); }"},
          {"function foo() { return bar(); function bar() { return 1; } }"},
          {"function foo() { return x; var x; }"},
          {"function foo() { var x = 1; var y = 2; }"},
          {"function foo() { var x = 1; var y = 2; return; }"},
          {"while (true) { switch (foo) { case 1: x = 1; x = 2;} }"},
          {"while (true) { break; var x; }"},
          {"while (true) { continue; var x, y; }"},
          {"while (true) { throw 'message'; var x; }"},
          {"while (true) { if (true) break; var x = 1; }"},
          {"while (true) continue;"},
          {"switch (foo) { case 1: break; var x; }"},
          {"switch (foo) { case 1: break; var x; default: throw true; };"},
          {"const arrow_direction = arrow => {  switch (arrow) { default: throw new "
           "Error();  };}"},
          {"var x = 1; y = 2; throw 'uh oh'; var y;"},
          {"function foo() { var x = 1; if (x) { return; } x = 2; }"},
          {"function foo() { var x = 1; if (x) { } else { return; } x = 2; }"},
          {"function foo() { var x = 1; switch (x) { case 0: break; default: return; } x "
           "= 2; }"},
          {"function foo() { var x = 1; while (x) { return; } x = 2; }"},
          {"function foo() { var x = 1; for (x in {}) { return; } x = 2; }"},
          {"function foo() { var x = 1; try { return; } finally { x = 2; } }"},
          {"function foo() { var x = 1; for (;;) { if (x) break; } x = 2; }"},
          {"A: { break A; } foo()"},
          {"function foo() { try { return; } catch (e) {} bar(); }"},
          {"function foo() { return; function bar() {} }"},
          {"function foo() { return; declare function bar(): void; }"},
          {"function foo() { return; type T = number; }"},
          {"function foo() { return; interface I {} }"},
          {"function foo() { return; ; }"},
          {"if (x) { return; } else { return; }"},
      },
      {
          {"function foo() { return x; var x = 1; }",
           {{"unreachableCode", 1, 28, 1, 38, "Unreachable code."}}},
          {"function foo() { return x; var x, y = 1; }", {{"unreachableCode"}}},
          {"while (true) { continue; var x = 1; }", {{"unreachableCode"}}},
          {"function foo() { return; x = 1; }", {{"unreachableCode", 1, 26, 1, 32}}},
          {"function foo() { throw error; x = 1; }", {{"unreachableCode"}}},
          {"while (true) { break; x = 1; }", {{"unreachableCode"}}},
          {"while (true) { continue; x = 1; }", {{"unreachableCode"}}},
          {"function foo() { switch (foo) { case 1: return; x = 1; } }",
           {{"unreachableCode"}}},
          {"function foo() { switch (foo) { case 1: throw e; x = 1; } }",
           {{"unreachableCode"}}},
          {"while (true) { switch (foo) { case 1: break; x = 1; } }",
           {{"unreachableCode"}}},
          {"while (true) { switch (foo) { case 1: continue; x = 1; } }",
           {{"unreachableCode"}}},
          {"var x = 1; throw 'uh oh'; var y = 2;", {{"unreachableCode"}}},
          {"function foo() { var x = 1; if (x) { return; } else { throw e; } x = 2; }",
           {{"unreachableCode", 1, 66, 1, 72}}},
          {"function foo() { var x = 1; if (x) return; else throw -1; x = 2; }",
           {{"unreachableCode"}}},
          {"function foo() { var x = 1; try { return; } finally {} x = 2; }",
           {{"unreachableCode"}}},
          {"function foo() { var x = 1; try { } finally { return; } x = 2; }",
           {{"unreachableCode"}}},
          {"function foo() { var x = 1; do { return; } while (x); x = 2; }",
           {{"unreachableCode"}}},
          {"function foo() { var x = 1; while (x) { if (x) break; else continue; x = 2; "
           "} }",
           {{"unreachableCode"}}},
          {"function foo() { var x = 1; for (;;) { if (x) continue; } x = 2; }",
           {{"unreachableCode"}}},
          {"function foo() { var x = 1; while (true) { } x = 2; }",
           {{"unreachableCode"}}},
          {"function foo() { try { return; } catch (e) { return; } bar(); }",
           {{"unreachableCode"}}},
          {"function foo() { try { throw e; } catch (e) { return; } bar(); }",
           {{"unreachableCode"}}},
          // Consecutive unreachable statements make one report.
          {"function foo() {\n    return;\n    a();\n    b()\n    // comment\n    "
           "c();\n}",
           {{"unreachableCode", 3, 5, 6, 9}}},
          {"function foo() {\n    return;\n    a();\n    if (b()) {\n        c()\n    } "
           "else {\n  "
           "      d()\n    }\n}",
           {{"unreachableCode", 3, 5, 8, 6}}},
          {"function foo() {\n    if (a) {\n        return\n        b()\n        c()\n   "
           " } else "
           "{\n        throw err\n        d()\n    }\n}",
           {{"unreachableCode", 4, 9, 5, 12}, {"unreachableCode", 8, 9, 8, 12}}},
          {"function foo() {\n    if (a) {\n        return\n        b()\n        c()\n   "
           " } else "
           "{\n        throw err\n        d()\n    }\n    e()\n}",
           {{"unreachableCode", 4, 9, 5, 12},
            {"unreachableCode", 8, 9, 8, 12},
            {"unreachableCode", 10, 5, 10, 8}}},
          // A hoisted function in the middle splits the run.
          {"function foo() { return; a(); function bar() {} b(); }",
           {{"unreachableCode"}, {"unreachableCode"}}},
          {"function foo() { return; let x = 1; }", {{"unreachableCode"}}},
          {"function foo() { return; const x = 1; }", {{"unreachableCode"}}},
          {"class C { static { return; foo(); } }", {{"unreachableCode"}}},
      });
}
