#pragma once

#include "testing/describe.h"
#include "util/string.h"

#include <string>
#include <string_view>
#include <type_traits>

namespace fastlint::test {

/**
 * Compares a value against the stored snapshot for the running test, recording
 * a failure with a unified diff when they differ. `label` names the entry;
 * passing nullptr numbers it by call order within the test.
 */
void snapshotCheck(const string &actual, const char *label, const char *file, int line);

/** Reports a snapshot mismatch, with the rendered diff attached. */
void reportSnapshotFailure(const string &key,
                           const string &detail,
                           const string &diff,
                           const char *file,
                           int line);

/** Loads, updates and writes the `.snap` files touched by this run. */
struct SnapshotSummary {
  int written = 0;
  int newEntries = 0;
  int obsolete = 0;
};

SnapshotSummary finishSnapshots();

/** The unified diff a mismatch prints, exposed so the framework can test it. */
string renderSnapshotDiff(const string &stored, const string &actual, bool color);

/** Clears the per-test numbering used by unlabelled snapshots. */
void resetSnapshotCounters();

/** Text of a value as a snapshot stores it: strings verbatim, others rendered. */
template <typename T> string snapshotText(const T &value)
{
  if constexpr (std::is_convertible_v<const T &, std::string_view>) {
    return string(std::string(std::string_view(value)));
  } else if constexpr (std::is_same_v<T, string>) {
    return value;
  } else {
    return render(value);
  }
}

} // namespace fastlint::test

#define SNAPSHOT(VALUE)                                                                  \
  ::fastlint::test::snapshotCheck(                                                       \
      ::fastlint::test::snapshotText(VALUE), nullptr, __FILE__, __LINE__)

#define SNAPSHOT_NAMED(LABEL, VALUE)                                                     \
  ::fastlint::test::snapshotCheck(                                                       \
      ::fastlint::test::snapshotText(VALUE), LABEL, __FILE__, __LINE__)
