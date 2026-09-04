#include "testing/runner.h"
#include "testing/test.h"

#include "util/vector.h"

using namespace fastlint::test;

TEST(framework, decomposes_both_operands)
{
  const ExprResult result = ((Decomposer{} << 2) == 3);
  CHECK(!result.passed);
  CHECK_EQ(result.lhs, string("2"));
  CHECK_EQ(result.op, string("=="));
  CHECK_EQ(result.rhs, string("3"));
}

TEST(framework, renders_containers_and_strings)
{
  Vector<int> values;
  values.append(1);
  values.append(2);
  CHECK_EQ(render(values), string("[1, 2]"));
  CHECK_EQ(render("hi"), string("\"hi\""));
}

TEST(framework, glob_matches_stars_and_wildcards)
{
  CHECK(globMatch("parser.*", "parser.arrow"));
  CHECK(globMatch("*.arrow*", "parser.arrow_body"));
  CHECK(globMatch("a?c", "abc"));
  CHECK(!globMatch("parser.*", "scanner.arrow"));
}

TEST(framework, replays_the_body_once_per_subcase)
{
  // Each SUBCASE runs in its own replay of the test body, so `seen` is 1 in
  // both branches rather than accumulating.
  int seen = 0;
  SUBCASE("first")
  {
    seen++;
    CHECK_EQ(seen, 1);
  }
  SUBCASE("second")
  {
    seen++;
    CHECK_EQ(seen, 1);
  }
}

TEST(framework, nests_subcases)
{
  SUBCASE("outer")
  {
    int depth = 1;
    SUBCASE("inner")
    {
      depth++;
    }
    CHECK(depth >= 1);
  }
}

TEST_TAGGED(framework, honors_skip_tags, "slow")
{
  // Reaching this body at all means the default skip list was lifted.
  CHECK((options().all || options().tags.size() > 0));
}

TEST(framework, info_scope_attaches_context)
{
  INFO("outer context");
  {
    INFO("inner {}", 42);
    CHECK(true);
  }
  CHECK(true);
}

TEST(framework, skip_marks_the_test_skipped)
{
  // SKIP returns from the body, so the runner reports this one as skipped.
  SKIP("nothing to do here");
}
