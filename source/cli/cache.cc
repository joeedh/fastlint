// The `cache` command: maintenance of the `lint` result cache. Only
// `cache verify` exists so far (docs/rules.md "Result cache").

#include "cli/files.h"
#include "fastlint/cache/store.h"
#include "fastlint/version.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fastlint::cli {

using litestl::util::string;
using litestl::util::Vector;

int cacheCommand(int argc, char **argv)
{
  if (argc < 3 || std::strcmp(argv[2], "verify") != 0) {
    std::fprintf(stderr, "usage: fastlint cache verify [--cache-dir <dir>]\n");
    return 2;
  }
  std::string dir = "node_modules/.cache/fastlint";
  for (int i = 3; i < argc; i++) {
    if (std::strcmp(argv[i], "--cache-dir") == 0 && i + 1 < argc) {
      dir = argv[++i];
    } else {
      std::fprintf(stderr, "unknown option '%s'\n", argv[i]);
      return 2;
    }
  }

  std::string db = dir + "/lint.db";
  std::error_code ec;
  if (!std::filesystem::is_regular_file(db, ec)) {
    std::fprintf(stderr, "cache: no store at %s\n", db.c_str());
    return 2;
  }

  cache::Store store;
  cache::StoreOptions options;
  options.path = string(db.c_str());
  options.tsgoVersion = string(fastlint::version());
  string error;
  if (!store.open(options, error)) {
    std::fprintf(stderr, "cache: %s\n", error.c_str());
    return 2;
  }
  Vector<string> problems;
  if (!store.verify(problems, error)) {
    std::fprintf(stderr, "cache: %s\n", error.c_str());
    return 2;
  }
  if (problems.isEmpty()) {
    std::printf("cache ok: %s\n", db.c_str());
    return 0;
  }
  std::fprintf(stderr, "cache problems in %s:\n", db.c_str());
  for (const string &problem : problems) {
    std::fprintf(stderr, "  %s\n", problem.c_str());
  }
  return 1;
}

} // namespace fastlint::cli
