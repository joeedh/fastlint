#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST(rules_no_console, cases)
{
  test::runRuleTests(
      rules::kNoConsole,
      {
          {"Console.info(foo)"},
          {"console.info(foo)", R"([{"allow": ["info"]}])"},
          {"console.warn(foo)", R"([{"allow": ["warn"]}])"},
          {"console.error(foo)", R"([{"allow": ["error"]}])"},
          {"console.log(foo)", R"([{"allow": ["log", "warn"]}])"},
          {"console.info(foo)", R"([{"allow": ["info", "log", "warn", "error"]}])"},
          {"var console = require('myconsole'); console.log(foo)", nullptr, "test.js"},
          {"function f(console) { console.log(1); }"},
      },
      {
          {"console.log(foo)",
           {{"unexpected", 1, 1, 1, 12, "Unexpected console statement."}}},
          {"console.error(foo)", {{"unexpected", 1, 1, 1, 14}}},
          {"console.info(foo)", {{"unexpected"}}},
          {"console.warn(foo)", {{"unexpected"}}},
          {"console[\"log\"](foo)", {{"unexpected", 1, 1, 1, 15}}},
          {"console.log(foo)",
           {{"limited",
             1,
             1,
             1,
             12,
             "Unexpected console statement. Only these console methods are allowed: "
             "error."}},
           nullptr,
           R"([{"allow": ["error"]}])"},
          {"console.error(foo)",
           {{"limited",
             0,
             0,
             0,
             0,
             "Unexpected console statement. Only these console methods are allowed: "
             "warn, "
             "info."}},
           nullptr,
           R"([{"allow": ["warn", "info"]}])"},
          {"if (a) console.log(a)\nconsole.log(b);",
           {{"unexpected", 1, 8}, {"unexpected", 2, 1}}},
      });
}
