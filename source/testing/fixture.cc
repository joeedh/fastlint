#include "testing/fixture.h"
#include "testing/test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef FASTLINT_TESTS_DIR
#define FASTLINT_TESTS_DIR "tests"
#endif

namespace fastlint::test {

string testsDir()
{
  return string(FASTLINT_TESTS_DIR);
}

Vector<Fixture> readFixtures(const char *dir, const char *extension)
{
  namespace fs = std::filesystem;

  fs::path root(dir);
  if (root.is_relative()) {
    root = fs::path(FASTLINT_TESTS_DIR).parent_path() / root;
  }

  Vector<Fixture> fixtures;
  if (!fs::exists(root)) {
    return fixtures;
  }

  std::vector<fs::path> found;
  for (const auto &entry : fs::recursive_directory_iterator(root)) {
    if (entry.is_regular_file() && entry.path().extension() == extension) {
      found.push_back(entry.path());
    }
  }
  std::sort(found.begin(), found.end());

  for (const fs::path &path : found) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();

    std::string relative = fs::relative(path, root).generic_string();
    Fixture fixture;
    fixture.relpath = string(relative.c_str());
    const std::string absolute = path.generic_string();
    fixture.path = string(absolute.c_str());
    const std::string text = buffer.str();
    fixture.text = string(text.c_str());
    fixtures.append(std::move(fixture));
  }
  return fixtures;
}

void forEachFile(const char *dir,
                 const char *extension,
                 const std::function<void(const Fixture &)> &body)
{
  for (const Fixture &fixture : readFixtures(dir, extension)) {
    SUBCASE(fixture.relpath.c_str())
    {
      body(fixture);
    }
  }
}

} // namespace fastlint::test
