#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST(rules_no_duplicate_case, cases)
{
  test::runRuleTests(
      rules::kNoDuplicateCase,
      {
          {"var a = 1; switch (a) {case 1: break; case 2: break; default: break;}"},
          {"var a = '1'; switch (a) {case '1': break; case '2': break; default: break;}"},
          {"var a = 1, one = 1; switch (a) {case one: break; case 2: break; default: "
           "break;}"},
          {"var a = 1, p = {p: {p1: 1, p2: 1}}; switch (a) {case p.p.p1: break; case "
           "p.p.p2: "
           "break; default: break;}"},
          {"var a = 1, f = function(b) { return b ? { p1: 1 } : { p1: 2 }; }; switch (a) "
           "{case f(true).p1: break; case f(true, false).p1: break; default: break;}"},
          {"var a = 1; switch (a) {case a: break; case a + 1: break; default: break;}"},
          {"switch (a) { case 1: break; case '1': break; }"},
          {"switch (a) { case `${x}`: break; case `${y}`: break; }"},
          {"switch (a) { case x.y: break; case x?.y: break; }"},
      },
      {
          {"var a = 1; switch (a) {case 1: break; case 1: break; case 2: break; default: "
           "break;}",
           {{"unexpected", 1, 39, 1, 53, "Duplicate case label."}}},
          {"var a = '1'; switch (a) {case '1': break; case '1': break; case '2': break; "
           "default: "
           "break;}",
           {{"unexpected"}}},
          {"var a = 1, one = 1; switch (a) {case one: break; case one: break; case 2: "
           "break; "
           "default: break;}",
           {{"unexpected"}}},
          {"var a = 1, p = {p: {p1: 1, p2: 1}}; switch (a) {case p.p.p1: break; case "
           "p.p.p1: "
           "break; default: break;}",
           {{"unexpected"}}},
          {"var a = 1, f = function(b) { return b ? { p1: 1 } : { p1: 2 }; }; switch (a) "
           "{case f(true).p1: break; case f(true).p1: break; default: break;}",
           {{"unexpected"}}},
          {"switch (a) { case 1: case 2: case 1: break; default: break; }",
           {{"unexpected", 1, 30, 1, 44}}},
          {"switch (a) { case 1: break; case 1: break; case 1: break; }",
           {{"unexpected"}, {"unexpected"}}},
          {"switch (a) { case x.y: break; case x . y: break; }", {{"unexpected"}}},
          {"switch (a) { case `foo`: break; case `foo`: break; }", {{"unexpected"}}},
          {"switch (a) { case a + 1: break; case a+1: break; }", {{"unexpected"}}},
      });
}
