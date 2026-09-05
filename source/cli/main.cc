#include "cli/files.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "fastlint/syntax/tree.h"
#include "fastlint/version.h"
#include "util/string.h"
#include "util/vector.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

using namespace fastlint;
using namespace fastlint::cli;
using litestl::util::string;
using litestl::util::Vector;

namespace {

struct Position {
  uint32_t line;
  uint32_t column;
};

Position positionOf(syntax::GrammarTree &tree, uint32_t offset)
{
  uint32_t line = tree.lineOf(offset);
  uint32_t lineStart = line > 0 ? tree.lineStarts()[line - 1] : 0;
  return {line, offset - lineStart + 1};
}

/**
 * Parses every file, printing each diagnostic as `path:line:col: TSnnnn
 * message`. With `--summary` only the counts print; `--limit N` caps the
 * diagnostics printed per file.
 */
int parseCommand(int argc, char **argv)
{
  Vector<std::filesystem::path> files;
  bool summaryOnly = false;
  uint32_t limit = 0xffffffffu;
  for (int i = 2; i < argc; i++) {
    if (std::strcmp(argv[i], "--summary") == 0) {
      summaryOnly = true;
    } else if (std::strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
      limit = uint32_t(std::atoi(argv[++i]));
    } else {
      collectFiles(argv[i], files);
    }
  }
  if (files.isEmpty()) {
    std::fprintf(stderr, "usage: fastlint parse [--summary] [--limit N] <file|dir>...\n");
    return 2;
  }

  size_t parsed = 0, clean = 0, unreadable = 0, totalDiagnostics = 0, totalBytes = 0;
  auto started = std::chrono::steady_clock::now();
  for (const std::filesystem::path &path : files) {
    std::string bytes;
    if (!readFile(path, bytes)) {
      std::fprintf(stderr, "%s: cannot read\n", path.string().c_str());
      unreadable++;
      continue;
    }
    parsed++;
    totalBytes += bytes.size();
    syntax::Diagnostics diagnostics;
    syntax::GrammarTree tree;
    syntax::Parser parser(std::string_view(bytes), optionsFor(path), diagnostics);
    parser.parseFile(tree);
    if (diagnostics.empty()) {
      clean++;
      continue;
    }
    totalDiagnostics += diagnostics.size();
    if (summaryOnly) {
      continue;
    }
    uint32_t printed = 0;
    for (const syntax::Diagnostic &d : diagnostics.items()) {
      if (printed++ == limit) {
        std::printf("%s: %zu more\n", path.string().c_str(), diagnostics.size() - limit);
        break;
      }
      Position at = positionOf(tree, d.offset);
      std::printf("%s:%u:%u: TS%u %s\n",
                  path.string().c_str(),
                  at.line,
                  at.column,
                  d.code,
                  d.message.c_str());
    }
  }
  auto elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started);
  std::printf("%zu files parsed, %zu clean, %zu with diagnostics (%zu diagnostics), "
              "%zu unreadable, %.1f MB in %.2fs\n",
              parsed,
              clean,
              parsed - clean,
              totalDiagnostics,
              unreadable,
              double(totalBytes) / (1024.0 * 1024.0),
              elapsed.count());
  return parsed == clean ? 0 : 1;
}

/** Parses one file and prints its dump; `--errors` appends diagnostics. */
int dumpOne(const std::filesystem::path &file, bool spans, bool errors)
{
  std::string bytes;
  if (!readFile(file, bytes)) {
    std::fprintf(stderr, "%s: cannot read\n", file.string().c_str());
    return 2;
  }
  syntax::Diagnostics diagnostics;
  syntax::GrammarTree tree;
  syntax::Parser parser(std::string_view(bytes), optionsFor(file), diagnostics);
  parser.parseFile(tree);
  string dump;
  syntax::dumpTree(tree, dump, spans);
  std::fputs(dump.c_str(), stdout);
  if (errors) {
    for (const syntax::Diagnostic &d : diagnostics.items()) {
      Position at = positionOf(tree, d.offset);
      std::printf("%u:%u: TS%u %s\n", at.line, at.column, d.code, d.message.c_str());
    }
  }
  return diagnostics.empty() ? 0 : 1;
}

int dumpTreeCommand(int argc, char **argv)
{
  const char *file = nullptr;
  const char *batch = nullptr;
  bool errors = false;
  bool spans = false;
  for (int i = 2; i < argc; i++) {
    if (std::strcmp(argv[i], "--errors") == 0) {
      errors = true;
    } else if (std::strcmp(argv[i], "--spans") == 0) {
      spans = true;
    } else if (std::strcmp(argv[i], "--batch") == 0 && i + 1 < argc) {
      batch = argv[++i];
    } else {
      file = argv[i];
    }
  }
  if (!file && !batch) {
    std::fprintf(
        stderr,
        "usage: fastlint dump-tree [--errors] [--spans] (<file> | --batch <list>)\n");
    return 2;
  }
  if (!batch) {
    return dumpOne(file, spans, errors);
  }
  // Batch mode reads one path per line and prints `#file <path>` before each
  // dump so a consumer can stream thousands of files from one process.
  std::string list;
  if (!readFile(batch, list)) {
    std::fprintf(stderr, "%s: cannot read\n", batch);
    return 2;
  }
  int worst = 0;
  size_t at = 0;
  while (at < list.size()) {
    size_t end = list.find('\n', at);
    if (end == std::string::npos) {
      end = list.size();
    }
    std::string line = list.substr(at, end - at);
    at = end + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    std::printf("#file %s\n", line.c_str());
    int code = dumpOne(line, spans, errors);
    if (code == 2) {
      std::printf("#error cannot read\n");
    }
    worst = std::max(worst, code);
  }
  std::fflush(stdout);
  return worst;
}

} // namespace

int main(int argc, char **argv)
{
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--version") == 0) {
      std::printf("%s\n", fastlint::version());
      return 0;
    }
  }
  if (argc > 1 && std::strcmp(argv[1], "parse") == 0) {
    return parseCommand(argc, argv);
  }
  if (argc > 1 && std::strcmp(argv[1], "dump-tree") == 0) {
    return dumpTreeCommand(argc, argv);
  }
  if (argc > 1 && std::strcmp(argv[1], "fuzz") == 0) {
    return fuzzCommand(argc, argv);
  }
  if (argc > 1 && std::strcmp(argv[1], "bench") == 0) {
    return benchCommand(argc, argv);
  }
  std::printf("%s\n", fastlint::buildBanner().c_str());
  std::printf("commands: parse [--summary] [--limit N] <file|dir>..., "
              "dump-tree [--errors] [--spans] <file>, "
              "fuzz [--iterations N] [--seed S] <file|dir>..., "
              "bench [--repeat N] [--json] <file|dir>...\n");
  std::printf("no rules yet; see docs/tasklists/MASTER.md\n");
  return 0;
}
