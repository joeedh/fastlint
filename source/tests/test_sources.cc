#include "test_sources.h"
#include "util/alloc.h"
#include "util/string.h"
#include "util/vector.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace fastlint::test {
using litestl::util::string;
using litestl::util::Vector;

Vector<TestSource> tsTestSources;

void loadTestSource(string path)
{
  path = string("source/tests/ts_sources/" + path);

  // std::string is the file-I/O boundary: it keeps the length, so a NUL
  // byte inside a source survives, and it grows geometrically.
  std::string bytes;
  FILE *file = fopen(path.c_str(), "rb");
  if (file) {
    char buffer[4096];
    for (;;) {
      size_t read = fread(buffer, 1, sizeof(buffer), file);
      if (read == 0) {
        break;
      }
      bytes.append(buffer, read);
    }
    fclose(file);
  } else {
    fprintf(stderr, "Failed to open file: %s\n", path.c_str());
  }
  tsTestSources.append({path, string(bytes)});
}

void loadTestSources()
{
  if (!tsTestSources.isEmpty()) {
    return;
  }
  // The sources live for the whole run, so the leak tracker skips them.
  litestl::alloc::PermanentGuard guard;
  loadTestSource("ts_test_struct_intern2.ts");
  loadTestSource("ts_test_base.ts");
  loadTestSource("ts_test_nodes.ts");
}
} // namespace fastlint::test