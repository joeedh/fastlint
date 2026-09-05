#include "cli/files.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "fastlint/syntax/tree.h"
#include "util/vector.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

// `fastlint bench`: parse throughput with file I/O taken out of the timing.
// Every file is read up front, then the whole set is parsed `--repeat` times
// and the fastest pass is reported, so the number is the parser's and not the
// disk cache's.

namespace fastlint::cli {

using litestl::util::Vector;

namespace {

struct Source {
  std::filesystem::path path;
  std::string bytes;
  syntax::Parser::Options options;
};

struct Pass {
  double seconds = 0;
  size_t tokens = 0;
  size_t nodes = 0;
  size_t diagnostics = 0;
};

Pass parseAll(const Vector<Source> &sources)
{
  Pass pass;
  auto started = std::chrono::steady_clock::now();
  for (const Source &source : sources) {
    syntax::Diagnostics diagnostics;
    syntax::GrammarTree tree;
    syntax::Parser parser(std::string_view(source.bytes), source.options, diagnostics);
    parser.parseFile(tree);
    pass.tokens += tree.tokens().size();
    pass.nodes += tree.nodes().size();
    pass.diagnostics += diagnostics.size();
  }
  pass.seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  return pass;
}

} // namespace

int benchCommand(int argc, char **argv)
{
  Vector<std::filesystem::path> files;
  uint32_t repeat = 5;
  bool json = false;
  for (int i = 2; i < argc; i++) {
    if (std::strcmp(argv[i], "--repeat") == 0 && i + 1 < argc) {
      repeat = uint32_t(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--json") == 0) {
      json = true;
    } else {
      collectFiles(argv[i], files);
    }
  }
  if (files.isEmpty() || repeat == 0) {
    std::fprintf(stderr, "usage: fastlint bench [--repeat N] [--json] <file|dir>...\n");
    return 2;
  }

  Vector<Source> sources;
  size_t bytes = 0;
  for (const std::filesystem::path &path : files) {
    Source source;
    source.path = path;
    if (!readFile(path, source.bytes)) {
      std::fprintf(stderr, "%s: cannot read\n", path.string().c_str());
      continue;
    }
    source.options = optionsFor(path);
    bytes += source.bytes.size();
    sources.append(std::move(source));
  }

  Pass best;
  double total = 0;
  for (uint32_t i = 0; i < repeat; ++i) {
    Pass pass = parseAll(sources);
    total += pass.seconds;
    if (i == 0 || pass.seconds < best.seconds) {
      best = pass;
    }
  }
  double megabytes = double(bytes) / (1024.0 * 1024.0);
  double throughput = megabytes / best.seconds;
  if (json) {
    std::printf("{\"files\":%zu,\"bytes\":%zu,\"tokens\":%zu,\"nodes\":%zu,"
                "\"diagnostics\":%zu,\"repeat\":%u,\"bestSeconds\":%.6f,"
                "\"meanSeconds\":%.6f,\"mbPerSecond\":%.2f}\n",
                sources.size(),
                bytes,
                best.tokens,
                best.nodes,
                best.diagnostics,
                repeat,
                best.seconds,
                total / repeat,
                throughput);
    return 0;
  }
  std::printf("%zu files, %.2f MB, %zu tokens, %zu nodes; best of %u: %.1f ms, %.1f MB/s "
              "(mean %.1f ms)\n",
              sources.size(),
              megabytes,
              best.tokens,
              best.nodes,
              repeat,
              best.seconds * 1000.0,
              throughput,
              total / repeat * 1000.0);
  return 0;
}

} // namespace fastlint::cli
