#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST(rules_no_debugger, cases)
{
  test::runRuleTests(
      rules::kNoDebugger,
      {
          {"var test = { debugger: 1 }; test.debugger;"},
          {"const debugger_ = 1; debugger_;", nullptr, "test.js"},
          {"let a = 1;\nif (a) { a++; }"},
      },
      {
          {"if (foo) debugger", {{"unexpected", 1, 10, 1, 18}}},
          {"debugger;\nfoo();\n", {{"unexpected", 1, 1, 1, 10}}, "foo();\n"},
          {"function f() {\n  debugger;\n  return 1;\n}\n",
           {{"unexpected", 2, 3, 2, 12, "Unexpected 'debugger' statement."}},
           "function f() {\n  return 1;\n}\n"},
      });
}
