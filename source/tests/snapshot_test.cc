#include "testing/snapshot.h"
#include "testing/test.h"

#include "util/vector.h"

using namespace fastlint::test;

TEST(snapshot, stores_text_verbatim)
{
  SNAPSHOT_NAMED("greeting", string("hello\nworld"));
}

TEST(snapshot, renders_non_string_values)
{
  Vector<int> values;
  for (int i = 0; i < 4; i++) {
    values.append(i * i);
  }
  SNAPSHOT_NAMED("squares", values);
}

TEST(snapshot, numbers_unlabelled_entries_by_call_order)
{
  SNAPSHOT(1 + 1);
  SNAPSHOT(string("second"));
}

TEST(snapshot, escapes_lines_that_would_look_like_headers)
{
  SNAPSHOT_NAMED("headerish", string("### not a key\nplain"));
}

TEST(snapshot, renders_a_unified_diff_on_mismatch)
{
  const string stored("alpha\nbeta\ngamma\ndelta\n");
  const string actual("alpha\nbeta\nGAMMA\ndelta\n");
  SNAPSHOT_NAMED("diff", renderSnapshotDiff(stored, actual, false));
}

TEST(snapshot, reports_no_diff_for_equal_text)
{
  const string text("same\n");
  CHECK_EQ(renderSnapshotDiff(text, text, false).size(), size_t(0));
}
