#include "cli/files.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "fastlint/syntax/tree.h"
#include "util/vector.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

// `fastlint fuzz`: token-level mutations of real files, each parsed and
// checked against the tree invariants. Memory errors are the sanitizer's job
// (the command is meant to run under the `asan` preset); the driver in
// tools/make/fuzz.ts reads the `# <file> <seed>` line printed before every
// parse to know which case crashed or hung, and `--replay <seed> --out <path>`
// regenerates that case from the seed alone.

namespace fastlint::cli {

using litestl::util::Vector;

namespace {

/** splitmix64; every mutation draws from one of these seeded per case. */
struct Rng {
  uint64_t state;

  uint64_t next()
  {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  uint32_t below(uint32_t bound)
  {
    return bound == 0 ? 0 : uint32_t(next() % bound);
  }
};

uint64_t hashPath(const std::string &path)
{
  uint64_t h = 0xcbf29ce484222325ull;
  for (char c : path) {
    h = (h ^ uint8_t(c)) * 0x100000001b3ull;
  }
  return h;
}

struct Span {
  uint32_t offset;
  uint32_t length;
};

/** Token spans of the unmutated file, from a first parse. */
void tokenSpans(std::string_view source,
                const syntax::Parser::Options &options,
                Vector<Span> &spans)
{
  syntax::Diagnostics diagnostics;
  syntax::GrammarTree tree;
  syntax::Parser parser(source, options, diagnostics);
  parser.parseFile(tree);
  for (const syntax::Token &token : tree.tokens()) {
    if (token.kind == syntax::TokenKind::EndOfFile) {
      break;
    }
    spans.append(Span{token.offset, token.length});
  }
}

constexpr const char *kInserts[] = {
    "{",         "}",
    "(",         ")",
    "[",         "]",
    "<",         ">",
    ";",         ",",
    ".",         "...",
    ":",         "?",
    "=",         "=>",
    "/",         "`",
    "${",        "\"",
    "'",         "\n",
    "@",         "#",
    "!",         "*",
    "&",         "|",
    "async",     "await",
    "yield",     "let",
    "const",     "class",
    "extends",   "new",
    "typeof",    "import",
    "export",    "default",
    "type",      "as",
    "satisfies", "in",
    "of",        "function",
    "static",    "declare",
    "abstract",  "get",
    "set",       "readonly",
    "enum",      "namespace",
    "</",        "/>",
    "<>",        "{...",
    "\\u{",      "0x",
    "1n",        "\xE2\x80\xA8",
};

/** Applies one to three random edits; token spans are from the original text. */
std::string mutate(std::string_view original, const Vector<Span> &spans, uint64_t seed)
{
  Rng rng{seed};
  std::string text(original);
  uint32_t edits = 1 + rng.below(3);
  for (uint32_t e = 0; e < edits; ++e) {
    // Offsets shift after the first edit; keep each edit inside the text.
    auto clamp = [&](uint32_t offset) {
      return offset > text.size() ? uint32_t(text.size()) : offset;
    };
    uint32_t n = uint32_t(spans.size());
    Span a = n ? spans[rng.below(n)] : Span{0, 0};
    Span b = n ? spans[rng.below(n)] : Span{0, 0};
    uint32_t start = clamp(a.offset);
    uint32_t length = std::min<uint32_t>(a.length, uint32_t(text.size()) - start);
    switch (rng.below(7)) {
    case 0: // delete a token
      text.erase(start, length);
      break;
    case 1: // duplicate a token
      text.insert(start + length, " " + std::string(original.substr(b.offset, b.length)));
      break;
    case 2: { // replace a token with another
      std::string other(original.substr(b.offset, b.length));
      text.replace(start, length, other);
      break;
    }
    case 3: // insert a random fragment at a token boundary
      text.insert(start, kInserts[rng.below(uint32_t(std::size(kInserts)))]);
      break;
    case 4: // truncate at a token boundary
      text.resize(start);
      break;
    case 5: { // flip one byte to a bracket, quote or separator
      if (text.empty()) {
        break;
      }
      const char pool[] = "{}()[]<>;,.:=/'\"`$\n\\";
      text[rng.below(uint32_t(text.size()))] =
          pool[rng.below(uint32_t(sizeof(pool) - 1))];
      break;
    }
    default: { // swap two tokens
      if (a.offset > b.offset) {
        std::swap(a, b);
      }
      if (b.offset + b.length > text.size() || a.offset + a.length > b.offset) {
        break;
      }
      std::string first(text.substr(a.offset, a.length));
      std::string second(text.substr(b.offset, b.length));
      text.replace(b.offset, b.length, first);
      text.replace(a.offset, a.length, second);
      break;
    }
    }
  }
  return text;
}

/** Structural invariants every tree must satisfy, valid input or not; the failure goes to
 * `detail`. */
const char *
checkTree(syntax::GrammarTree &tree, std::string_view source, std::string &detail)
{
  auto describe = [&](const char *what, size_t a, size_t b, size_t c) {
    detail = what;
    detail += " (" + std::to_string(a) + ", " + std::to_string(b) + ", " +
              std::to_string(c) + ")";
  };
  auto nodes = tree.nodes();
  auto tokens = tree.tokens();
  if (nodes.size() == 0 || tree.root() >= nodes.size()) {
    return "no root";
  }
  if (tokens.size() == 0 ||
      tokens[tokens.size() - 1].kind != syntax::TokenKind::EndOfFile)
  {
    return "token array does not end in EndOfFile";
  }
  uint32_t previousEnd = 0;
  for (const syntax::Token &token : tokens) {
    if (token.offset < previousEnd || size_t(token.offset) + token.length > source.size())
    {
      describe(
          "token offset, length, source size", token.offset, token.length, source.size());
      return "token outside the source or overlapping the previous token";
    }
    previousEnd = token.offset + token.length;
  }
  for (uint32_t id = 0; id < nodes.size(); ++id) {
    const syntax::Node &node = nodes[id];
    if (size_t(node.firstToken) + node.tokenCount > tokens.size()) {
      describe(syntax::nodeKindName(node.kind),
               node.firstToken,
               node.tokenCount,
               tokens.size());
      return "node token range outside the token array";
    }
    if (size_t(node.firstChild) + node.childCount > tree.childIds().size()) {
      return "node child range outside the child array";
    }
    for (syntax::NodeId child : tree.children(id)) {
      if (child >= nodes.size()) {
        return "child id outside the arena";
      }
      if (nodes[child].parent != id) {
        return "child's parent link does not point back";
      }
      if (child == id) {
        return "node is its own child";
      }
    }
    if (id != tree.root() && node.parent >= nodes.size()) {
      describe(syntax::nodeKindName(node.kind), id, node.firstToken, node.tokenCount);
      return "non-root node without a parent";
    }
  }
  return nullptr;
}

struct FuzzArgs {
  Vector<std::filesystem::path> files;
  uint32_t iterations = 100;
  uint64_t seed = 1;
  bool replay = false;
  uint64_t replaySeed = 0;
  std::filesystem::path out;
  /** Parse each file once, unmutated, with the invariant checks (the minimizer's probe).
   */
  bool checkOnly = false;
};

int usage()
{
  std::fprintf(stderr,
               "usage: fastlint fuzz [--iterations N] [--seed S] <file|dir>...\n"
               "       fastlint fuzz --replay <seed> --out <path> <file>\n"
               "       fastlint fuzz --check <file>...\n");
  return 2;
}

} // namespace

int fuzzCommand(int argc, char **argv)
{
  FuzzArgs args;
  for (int i = 2; i < argc; i++) {
    if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
      args.iterations = uint32_t(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      args.seed = std::strtoull(argv[++i], nullptr, 10);
    } else if (std::strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
      args.replay = true;
      args.replaySeed = std::strtoull(argv[++i], nullptr, 10);
    } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
      args.out = argv[++i];
    } else if (std::strcmp(argv[i], "--check") == 0) {
      args.checkOnly = true;
    } else {
      collectFiles(argv[i], args.files);
    }
  }
  if (args.files.isEmpty() ||
      (args.replay && (args.files.size() != 1 || args.out.empty())))
  {
    return usage();
  }

  for (const std::filesystem::path &path : args.files) {
    std::string bytes;
    if (!readFile(path, bytes)) {
      std::fprintf(stderr, "%s: cannot read\n", path.string().c_str());
      return 2;
    }
    std::string name = path.generic_string();
    syntax::Parser::Options options = optionsFor(path);

    if (args.checkOnly) {
      syntax::Diagnostics diagnostics;
      syntax::GrammarTree tree;
      syntax::Parser parser(std::string_view(bytes), options, diagnostics);
      parser.parseFile(tree);
      std::string detail;
      if (const char *problem = checkTree(tree, bytes, detail)) {
        std::fprintf(stderr,
                     "fuzz: %s: invariant failed: %s %s\n",
                     name.c_str(),
                     problem,
                     detail.c_str());
        return 1;
      }
      continue;
    }

    Vector<Span> spans;
    tokenSpans(bytes, options, spans);

    if (args.replay) {
      std::string mutated = mutate(bytes, spans, args.replaySeed);
      if (!writeFile(args.out, mutated)) {
        std::fprintf(stderr, "fuzz: cannot write %s\n", args.out.string().c_str());
        return 2;
      }
      return 0;
    }

    uint64_t fileSeed = hashPath(name) ^ (args.seed * 0x9E3779B97F4A7C15ull);
    for (uint32_t i = 0; i < args.iterations; ++i) {
      uint64_t caseSeed = Rng{fileSeed + i}.next();
      // The driver reads this line to identify a crash or hang.
      std::fprintf(
          stderr, "# %s %llu\n", name.c_str(), static_cast<unsigned long long>(caseSeed));
      std::string mutated = mutate(bytes, spans, caseSeed);
      syntax::Diagnostics diagnostics;
      syntax::GrammarTree tree;
      syntax::Parser parser(std::string_view(mutated), options, diagnostics);
      parser.parseFile(tree);
      std::string detail;
      if (const char *problem = checkTree(tree, mutated, detail)) {
        std::fprintf(stderr,
                     "fuzz: %s seed %llu: invariant failed: %s %s\n",
                     name.c_str(),
                     static_cast<unsigned long long>(caseSeed),
                     problem,
                     detail.c_str());
        return 1;
      }
    }
  }
  return 0;
}

} // namespace fastlint::cli
