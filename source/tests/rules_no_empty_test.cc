#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST(rules_no_empty, cases)
{
  test::runRuleTests(
      rules::kNoEmpty,
      {
          {"if (foo) { bar() }"},
          {"while (foo) { bar() }"},
          {"for (;foo;) { bar() }"},
          {"try { foo() } catch (ex) { foo() }"},
          {"switch(foo) {case 'foo': break;}"},
          {"(function() { }())"},
          {"var foo = () => {};"},
          {"function foo() { }"},
          {"class A { m() {} }"},
          {"if (foo) {/* empty */}"},
          {"if (foo) {\n// empty\n}"},
          {"switch(foo) {\n// empty\n}"},
          {"try { foo() } catch (ex) {\n// empty\n}"},
          {"try { foo(); } catch (ex) {}", R"([{"allowEmptyCatch": true}])"},
          {"try { foo(); } catch (ex) {} finally { bar(); }",
           R"([{"allowEmptyCatch": true}])"},
      },
      {
          {"try {} catch (ex) {throw ex}",
           {{"unexpected", 1, 5, 1, 7, "Empty block statement."}}},
          {"try { foo() } catch (ex) {}", {{"unexpected", 1, 26, 1, 28}}},
          {"try { foo() } catch (ex) {throw ex} finally {}", {{"unexpected"}}},
          {"if (foo) {}", {{"unexpected", 1, 10, 1, 12}}},
          {"while (foo) {}", {{"unexpected"}}},
          {"for (;foo;) {}", {{"unexpected"}}},
          {"switch(foo) {}", {{"unexpected", 1, 13, 1, 15, "Empty switch statement."}}},
          {"switch (foo) { }", {{"unexpected", 1, 14, 1, 17}}},
          {"try { foo(); } catch (ex) {}",
           {{"unexpected"}},
           nullptr,
           R"([{"allowEmptyCatch": false}])"},
          {"try { foo(); } catch (ex) {} finally {}",
           {{"unexpected", 1, 38, 1, 40}},
           nullptr,
           R"([{"allowEmptyCatch": true}])"},
          {"if (a) {} else {}", {{"unexpected", 1, 8}, {"unexpected", 1, 16}}},
      });
}
