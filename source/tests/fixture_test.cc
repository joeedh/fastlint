#include "testing/fixture.h"
#include "testing/snapshot.h"
#include "testing/test.h"

using namespace fastlint::test;

TEST(fixture, reads_every_file_in_sorted_order)
{
  Vector<Fixture> found = readFixtures("tests/fixtures/testing", ".txt");
  REQUIRE_EQ(found.size(), size_t(3));
  CHECK_EQ(found[0].relpath, string("a.txt"));
  CHECK_EQ(found[1].relpath, string("b.txt"));
  CHECK_EQ(found[2].relpath, string("nested/c.txt"));
}

TEST(fixture, opens_a_subcase_per_file)
{
  // The subcase path already carries the file name, so the entry needs no
  // label of its own.
  forEachFile(
      "tests/fixtures/testing", ".txt", [](const Fixture &file) { SNAPSHOT(file.text); });
}
