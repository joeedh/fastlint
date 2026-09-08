#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST(rules_no_fallthrough, cases)
{
  const char *allowEmpty = R"([{"allowEmptyCase": true}])";
  const char *customPattern = R"([{"commentPattern": "break[\\s\\w]+omitted"}])";
  const char *reportUnused = R"([{"reportUnusedFallthroughComment": true}])";
  test::runRuleTests(
      rules::kNoFallthrough,
      {
          {"switch(foo) { case 0: a(); /* falls through */ case 1: b(); }"},
          {"switch(foo) { case 0: a()\n /* falls through */ case 1: b(); }"},
          {"switch(foo) { case 0: a(); /* fall through */ case 1: b(); }"},
          {"switch(foo) { case 0: a(); /* fallthrough */ case 1: b(); }"},
          {"switch(foo) { case 0: a(); /* FALLS THROUGH */ case 1: b(); }"},
          {"switch(foo) { case 0: { a(); /* falls through */ } case 1: b(); }"},
          {"switch(foo) { case 0: { a()\n /* falls through */ } case 1: b(); }"},
          {"switch(foo) { case 0: { a(); }\n/* falls through */ case 1: b(); }"},
          {"switch(foo) { case 0: { a(); /* falls through */ } case 1: { b(); /* falls "
           "through */ "
           "} case 2: c(); }"},
          {"switch(foo) { case 0: { a(); }\n/* falls through */ case 1: { b(); }\n/* "
           "falls "
           "through */ case 2: c(); }"},
          {"switch(foo) { case 0: a(); /* fallthrough */ case 1: b(); }"},
          {"switch(foo) { case 0: a(); /* fall through */ case 1: b(); }"},
          {"function foo() { switch(foo) { case 0: a(); return; case 1: b(); }; }"},
          {"switch(foo) { case 0: a(); throw 'foo'; case 1: b(); }"},
          {"while (a) { switch(foo) { case 0: a(); continue; case 1: b(); } }"},
          {"switch(foo) { case 0: a(); break; case 1: b(); }"},
          {"switch(foo) { case 0: case 1: a(); break; case 2: b(); }"},
          {"switch(foo) { case 0: case 1: break; case 2: b(); }"},
          {"switch(foo) { case 0: case 1: break; default: b(); }"},
          {"switch(foo) { case 0: case 1: a(); }"},
          {"switch(foo) { case 0: case 1: a(); break; }"},
          {"switch(foo) { case 0: case 1: break; }"},
          {"switch(foo) { case 0:\n case 1: break; }"},
          {"switch(foo) { case 0: // comment\n case 1: break; }"},
          {"function foo() { switch(foo) { case 0: a(); return; } }"},
          {"switch(foo) { case 0: a(); break; }"},
          {"switch(foo) { case 0: a(); break; case 1: b(); break; }"},
          {"switch(foo) { case 0: a(); break; /* comment */ case 1: b(); break; }"},
          {"switch(foo) { case 0: a(); break; case 1: b(); break; default: c(); }"},
          {"switch (foo) { case 0: a(); return; default: c(); }"},
          {"switch (foo) { case 0: a(); return; case 1: b(); return; default: c(); }"},
          {"switch(foo) { default: a(); break; case 1: b(); }"},
          {"switch(foo) { case 0: { a(); } break; case 1: b(); }"},
          {"switch(foo) { case 0: if (a) { break; } else { throw 0; } case 1: b(); }"},
          // A case whose body is an exhaustive switch cannot fall through.
          {"switch(foo) { case 0: switch (x) { case 1: return; default: return; } case "
           "1: "
           "b(); }"},
          {"switch(foo) { case 0: try { break; } finally {} case 1: b(); }"},
          {"switch(foo) { case 0: try {} finally { break; } case 1: b(); }"},
          {"switch(foo) { case 0: try { throw 0; } catch (err) { break; } case 1: b(); "
           "}"},
          {"switch(foo) { case 0: do { throw 0; } while(a); case 1: b(); }"},
          {"switch(foo) { case 0: a(); \n// eslint-disable-next-line no-fallthrough\n "
           "case 1: "
           "b(); }"},
          {"switch(foo) { case 0: a();\n/* break omitted */ case 1: b(); }",
           customPattern},
          {"switch(foo) { case 0: a();\n/* break is omitted */ case 1: b(); }",
           customPattern},
          {"switch(foo) { case 0: a();\n// falls through\ncase 1: b(); }"},
          {"switch(foo) { case 0:\n\ncase 1: b(); }", allowEmpty},
          {"switch(foo) { case 0:\n\n\ncase 1: b(); }", allowEmpty},
          {"switch(foo) { case 0:\n// comment\n\ncase 1: b(); }", allowEmpty},
          // A fallthrough comment on the last case has no next clause to permit.
          {"switch(foo) { case 0: a(); break; /* falls through */ }", reportUnused},
          // A non-matching comment before the next case is not a fallthrough comment.
          {"switch(foo) { case 0: a(); break; /* just a comment */ case 1: b(); }",
           reportUnused},
      },
      {
          {"switch(foo) { case 0: a();\ncase 1: b() }",
           {{"case", 2, 1, 2, 12, "Expected a 'break' statement before 'case'."}}},
          {"switch(foo) { case 0: a();\ndefault: b() }",
           {{"default", 2, 1, 2, 13, "Expected a 'break' statement before 'default'."}}},
          {"switch(foo) { case 0: a(); default: b() }", {{"default", 1, 28, 1, 40}}},
          {"switch(foo) { case 0: if (a) { break; } default: b() }", {{"default"}}},
          {"switch(foo) { case 0: try { throw 0; } catch (err) {} default: b() }",
           {{"default"}}},
          {"switch(foo) { case 0: while (a) { break; } default: b() }", {{"default"}}},
          {"switch(foo) { case 0: do { break; } while (a); default: b() }",
           {{"default"}}},
          {"switch(foo) { case 0:\n\n default: b() }", {{"default", 3, 2}}},
          {"switch(foo) { case 0: {} default: b() }", {{"default"}}},
          {"switch(foo) { case 0: a(); { /* falls through */ } default: b() }",
           {{"default"}}},
          {"switch(foo) { case 0: { /* falls through */ } a(); default: b() }",
           {{"default"}}},
          {"switch(foo) { case 0: if (a) { /* falls through */ } default: b() }",
           {{"default"}}},
          {"switch(foo) { case 0: { { /* falls through */ } } default: b() }",
           {{"default"}}},
          {"switch(foo) { case 0: { /* comment */ } default: b() }", {{"default"}}},
          {"switch(foo) { case 0:\n // comment\n default: b() }", {{"default"}}},
          {"switch(foo) { case 0: a(); /* falling through */ default: b() }",
           {{"default"}}},
          {"switch(foo) { case 0: a();\n/* no break */\ncase 1: b(); }",
           {{"case"}},
           nullptr,
           customPattern},
          {"switch(foo) { case 0: a();\n/* no break */\n/* todo: fix readability "
           "*/\ndefault: "
           "b() }",
           {{"default"}},
           nullptr,
           customPattern},
          {"switch(foo) { case 0: { a();\n/* no break */\n/* todo: fix readability */ "
           "}\ndefault: "
           "b() }",
           {{"default"}},
           nullptr,
           customPattern},
          {"switch(foo) { case 0: a(); case 1: b(); case 2: c(); }",
           {{"case"}, {"case"}}},
          {"switch(foo) { case 0:\n\ncase 1: b(); }", {{"case", 3, 1}}},
          {"switch(foo) { case 0: a();\n\ncase 1: b(); }",
           {{"case"}},
           nullptr,
           allowEmpty},
          {"switch(foo) { case 0: a();\n/* falls through */\n\ncase 1: b(); }"},
          // A case that exits cannot fall through, so its fallthrough comment is
          // unused and reported at the comment when the option is on.
          {"switch(foo) { case 0: a(); break;\n/* falls through */\ncase 1: b(); }",
           {{"unusedFallthroughComment", 2, 1}},
           nullptr,
           reportUnused},
          {"switch(foo) { default: a(); break;\n/* falls through */\ncase 1: b(); }",
           {{"unusedFallthroughComment", 2, 1}},
           nullptr,
           reportUnused},
          {"switch(foo) { case 0: a(); break;\n// falls through\ncase 1: b(); }",
           {{"unusedFallthroughComment", 2, 1}},
           nullptr,
           reportUnused},
      });
}
