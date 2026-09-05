#include "util/string.h"
#include "util/vector.h"

namespace fastlint::test {
using litestl::util::string;
using litestl::util::Vector;

struct TestSource {
  string path;
  string source;
};

extern Vector<TestSource> tsTestSources;

void loadTestSource(string path);
void loadTestSources();
} // namespace fastlint::test