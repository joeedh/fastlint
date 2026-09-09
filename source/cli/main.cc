#include "cli/files.h"
#include "fastlint/ast/binder.h"
#include "fastlint/ast/dump.h"
#include "fastlint/ast/lower.h"
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
    if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      std::printf("usage: fastlint parse [--summary] [--limit N] <file|dir>...\n");
      return 0;
    } else if (std::strcmp(argv[i], "--summary") == 0) {
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
    if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      std::printf(
          "usage: fastlint dump-tree [--errors] [--spans] (<file> | --batch <list>)\n");
      return 0;
    } else if (std::strcmp(argv[i], "--errors") == 0) {
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

/** Parses and lowers one file, then prints the AST dump and, on request, the bindings. */
int dumpAstCommand(int argc, char **argv)
{
  const char *file = nullptr;
  bool errors = false;
  bool bindings = false;
  for (int i = 2; i < argc; i++) {
    if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      std::printf("usage: fastlint dump-ast [--errors] [--bindings] <file>\n");
      return 0;
    } else if (std::strcmp(argv[i], "--errors") == 0) {
      errors = true;
    } else if (std::strcmp(argv[i], "--bindings") == 0) {
      bindings = true;
    } else {
      file = argv[i];
    }
  }
  if (!file) {
    std::fprintf(stderr, "usage: fastlint dump-ast [--errors] [--bindings] <file>\n");
    return 2;
  }
  std::string bytes;
  if (!readFile(file, bytes)) {
    std::fprintf(stderr, "%s: cannot read\n", file);
    return 2;
  }
  syntax::Diagnostics diagnostics;
  syntax::GrammarTree tree;
  syntax::Parser parser(std::string_view(bytes), optionsFor(file), diagnostics);
  parser.parseFile(tree);
  ast::AstFile astFile(&tree);
  ast::lower(tree, astFile);
  string dump;
  ast::dumpAst(astFile, dump);
  std::fputs(dump.c_str(), stdout);
  if (bindings) {
    ast::Bindings bound;
    ast::bind(astFile, bound);
    string scopes;
    ast::dumpBindings(bound, scopes);
    std::fputs(scopes.c_str(), stdout);
  }
  if (errors) {
    for (const syntax::Diagnostic &d : diagnostics.items()) {
      Position at = positionOf(tree, d.offset);
      std::printf("%u:%u: TS%u %s\n", at.line, at.column, d.code, d.message.c_str());
    }
  }
  return diagnostics.empty() ? 0 : 1;
}

/** Prints the top-level help: the banner, the command list with a one-line
 * description each, and the global options. Per-command options come from
 * `fastlint <command> --help`. */
void printHelp()
{
  std::printf("%s\n", fastlint::buildBanner().c_str());
  std::printf("\n"
              "A fast TypeScript/JavaScript linter.\n"
              "\n"
              "usage: fastlint <command> [options] <file|dir>...\n"
              "\n"
              "commands:\n"
              "  lint         lint files and report problems\n"
              "  parse        report only syntax diagnostics, running no rules\n"
              "  dump-tree    print a file's grammar tree\n"
              "  dump-ast     print a file's lowered AST\n"
              "\n"
              "development commands:\n"
              "  fuzz         mutate a corpus and parse it to find crashes\n"
              "  bench        measure parse throughput in MB/s\n"
              "  cache-bench  measure the type-fact cache\n"
              "  cache        inspect or verify the type-fact cache\n"
              "\n"
              "global options:\n"
              "  --init       write a starter fastlint.config.json and exit\n"
              "  --version    print the version and exit\n"
              "  -h, --help   show this help and exit\n"
              "\n"
              "Run `fastlint <command> --help` for a command's own options.\n");
}

/** Writes a starter `fastlint.config.json` in the current directory, extending
 * the recommended preset. Refuses to overwrite an existing config. */
int initCommand()
{
  const char *name = "fastlint.config.json";
  std::error_code ec;
  if (std::filesystem::exists(name, ec)) {
    std::fprintf(stderr, "%s already exists\n", name);
    return 1;
  }
  std::string body = "{\n"
                     "  \"extends\": \"fastlint:recommended\",\n"
                     "  \"rules\": {},\n"
                     "  \"ignores\": [\"**/node_modules/**\", \"**/dist/**\"]\n"
                     "}\n";
  if (!writeFile(name, body)) {
    std::fprintf(stderr, "cannot write %s\n", name);
    return 2;
  }
  std::printf("wrote %s\n", name);
  std::printf("extends fastlint:recommended; set rule severities under \"rules\".\n");
  return 0;
}

} // namespace

int main(int argc, char **argv)
{
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--version") == 0) {
      std::printf("%s\n", fastlint::version());
      return 0;
    }
    if (std::strcmp(argv[i], "--init") == 0) {
      return initCommand();
    }
  }
  // A bare invocation or a leading help flag prints the top-level help; a help
  // flag after a command is left to that command.
  if (argc == 1 || std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0)
  {
    printHelp();
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "lint") == 0) {
    return lintCommand(argc, argv);
  }
  if (argc > 1 && std::strcmp(argv[1], "parse") == 0) {
    return parseCommand(argc, argv);
  }
  if (argc > 1 && std::strcmp(argv[1], "dump-tree") == 0) {
    return dumpTreeCommand(argc, argv);
  }
  if (argc > 1 && std::strcmp(argv[1], "dump-ast") == 0) {
    return dumpAstCommand(argc, argv);
  }
  if (argc > 1 && std::strcmp(argv[1], "fuzz") == 0) {
    return fuzzCommand(argc, argv);
  }
  if (argc > 1 && std::strcmp(argv[1], "bench") == 0) {
    return benchCommand(argc, argv);
  }
  if (argc > 1 && std::strcmp(argv[1], "cache-bench") == 0) {
    return cacheBenchCommand(argc, argv);
  }
  if (argc > 1 && std::strcmp(argv[1], "cache") == 0) {
    return cacheCommand(argc, argv);
  }
  std::fprintf(stderr, "unknown command '%s'\n\n", argv[1]);
  printHelp();
  return 2;
}
