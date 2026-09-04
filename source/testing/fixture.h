#pragma once

#include "util/string.h"
#include "util/vector.h"

#include <functional>

namespace fastlint::test {

using litestl::util::string;
using litestl::util::Vector;

/** One file from a fixture directory, read into memory. */
struct Fixture {
  /** Path relative to the directory passed to forEachFile, with `/` separators. */
  string relpath;
  string path;
  string text;
};

/**
 * Opens a SUBCASE per file under `dir` with the given extension, in sorted
 * order, and calls `body` with its contents. Paths are relative to the tests
 * directory the test executable was compiled against.
 */
void forEachFile(const char *dir,
                 const char *extension,
                 const std::function<void(const Fixture &)> &body);

/** Every file under `dir` with the given extension, sorted, without subcases. */
Vector<Fixture> readFixtures(const char *dir, const char *extension);

/** Absolute path of the repository's `tests/` directory for this build. */
string testsDir();

} // namespace fastlint::test
