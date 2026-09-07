#include "fastlint/cache/closure.h"

#include "fastlint/ast/file.h"
#include "fastlint/ast/lower.h"
#include "fastlint/cache/imports.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "fastlint/syntax/tree.h"
#include "fastlint/types/type_graph.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fastlint::cache {

namespace {

using types::Hasher;

bool endsWith(std::string_view text, std::string_view suffix)
{
  return text.size() >= suffix.size() &&
         text.substr(text.size() - suffix.size()) == suffix;
}

/** Extensions tried in tsc's order; `.d.ts` last so a source file wins over its
 * declaration. */
const char *const kSourceExtensions[] = {
    ".ts", ".tsx", ".mts", ".cts", ".d.ts", ".js", ".jsx"};

/** `.js` in a specifier means the emitted file; the source it came from is what we hash.
 */
struct Rewrite {
  const char *from;
  const char *to[2];
};
const Rewrite kRewrites[] = {
    {".js", {".ts", ".tsx"}},
    {".jsx", {".tsx", ".ts"}},
    {".mjs", {".mts", nullptr}},
    {".cjs", {".cts", nullptr}},
};

string withSuffix(std::string_view base, const char *suffix)
{
  string out = toString(base);
  out += suffix;
  return out;
}

} // namespace

// ---------------------------------------------------------------- disk

bool DiskFileSystem::isFile(std::string_view path)
{
  std::error_code ec;
  return std::filesystem::is_regular_file(std::filesystem::path(std::string(path)), ec);
}

bool DiskFileSystem::readFile(std::string_view path, string &out)
{
  out = string();
  FILE *file = fopen(std::string(path).c_str(), "rb");
  if (!file) {
    return false;
  }
  // Gathered in a std::string and converted once at the OS boundary.
  std::string bytes;
  char buffer[65536];
  for (;;) {
    size_t read = fread(buffer, 1, sizeof buffer, file);
    if (read == 0) {
      break;
    }
    bytes.append(buffer, read);
  }
  fclose(file);
  out += bytes;
  return true;
}

// ---------------------------------------------------------------- paths

uint64_t hashContent(std::string_view bytes)
{
  return Hasher().bytes(bytes.data(), bytes.size()).value;
}

bool isRelativeSpecifier(std::string_view specifier)
{
  return specifier.starts_with("./") || specifier.starts_with("../") ||
         specifier == "." || specifier == ".." || specifier.starts_with("/");
}

string joinPath(std::string_view dir, std::string_view rel)
{
  // Absolute `rel` (a root slash or a drive letter) starts over.
  bool absolute =
      rel.starts_with("/") || rel.starts_with("\\") || (rel.size() >= 2 && rel[1] == ':');
  std::string_view base = absolute ? std::string_view() : dir;

  Vector<std::string_view> parts;
  bool rooted = false;
  std::string_view prefix;
  auto push = [&](std::string_view path, bool first) {
    size_t i = 0;
    if (first) {
      if (path.size() >= 2 && path[1] == ':') {
        prefix = path.substr(0, 2);
        i = 2;
      }
      if (i < path.size() && (path[i] == '/' || path[i] == '\\')) {
        rooted = true;
      }
    }
    while (i <= path.size()) {
      size_t j = i;
      while (j < path.size() && path[j] != '/' && path[j] != '\\') {
        j++;
      }
      std::string_view part = path.substr(i, j - i);
      if (part == "..") {
        if (parts.size() > 0 && parts[int(parts.size()) - 1] != "..") {
          parts.pop_back();
        } else if (!rooted) {
          parts.append(part);
        }
      } else if (!part.empty() && part != ".") {
        parts.append(part);
      }
      i = j + 1;
    }
  };
  if (!base.empty()) {
    push(base, true);
  }
  push(rel, base.empty());

  string out = toString(prefix);
  if (rooted) {
    out += '/';
  }
  for (int i = 0; i < int(parts.size()); i++) {
    if (i > 0) {
      out += '/';
    }
    for (char c : parts[i]) {
      out += c;
    }
  }
  if (out.size() == 0) {
    out += '.';
  }
  return out;
}

string dirOf(std::string_view path)
{
  size_t slash = path.find_last_of("/\\");
  if (slash == std::string_view::npos) {
    return string(".");
  }
  return joinPath(path.substr(0, slash), "");
}

string
resolveImport(std::string_view fromFile, std::string_view specifier, FileSystem &fs)
{
  if (!isRelativeSpecifier(specifier)) {
    return string();
  }
  string base = joinPath(view(dirOf(fromFile)), specifier);
  std::string_view basePath = view(base);

  if (fs.isFile(basePath)) {
    return base;
  }
  for (const Rewrite &rewrite : kRewrites) {
    if (!endsWith(basePath, rewrite.from)) {
      continue;
    }
    std::string_view stem =
        basePath.substr(0, basePath.size() - std::string_view(rewrite.from).size());
    for (const char *to : rewrite.to) {
      if (!to) {
        break;
      }
      string candidate = withSuffix(stem, to);
      if (fs.isFile(view(candidate))) {
        return candidate;
      }
    }
  }
  for (const char *ext : kSourceExtensions) {
    string candidate = withSuffix(basePath, ext);
    if (fs.isFile(view(candidate))) {
      return candidate;
    }
  }
  for (const char *ext : kSourceExtensions) {
    string candidate = withSuffix(basePath, "/index");
    candidate += ext;
    if (fs.isFile(view(candidate))) {
      return candidate;
    }
  }
  return string();
}

// ---------------------------------------------------------------- graph

int ImportGraph::indexOf(std::string_view path) const
{
  Key key{Hasher().text(path).value};
  const int *found = const_cast<Map<Key, int> &>(m_byPath).lookup_ptr(key);
  if (!found || view(m_files[*found].path) != path) {
    return -1;
  }
  return *found;
}

void ImportGraph::setFile(std::string_view path,
                          uint64_t contentHash,
                          span<const string> imports,
                          span<const string> unresolved)
{
  int index = indexOf(path);
  if (index < 0) {
    index = int(m_files.size());
    FileEntry entry;
    entry.path = toString(path);
    m_files.append(std::move(entry));
    m_byPath.add(Key{Hasher().text(path).value}, index);
  }
  FileEntry &entry = m_files[index];
  entry.contentHash = contentHash;
  entry.imports.clear();
  for (const string &import : imports) {
    entry.imports.append(import);
  }
  entry.unresolved.clear();
  for (const string &specifier : unresolved) {
    entry.unresolved.append(specifier);
  }
  Hasher h;
  h.text(path).u64(contentHash).u32(uint32_t(entry.unresolved.size()));
  for (const string &specifier : entry.unresolved) {
    h.text(view(specifier));
  }
  entry.selfHash = h.value;
  m_closureByPath.clear();
}

const FileEntry *ImportGraph::file(std::string_view path) const
{
  int index = indexOf(path);
  return index < 0 ? nullptr : &m_files[index];
}

uint64_t ImportGraph::closureHash(std::string_view path) const
{
  int root = indexOf(path);
  if (root < 0) {
    return 0;
  }
  Key key{Hasher().text(path).value};
  if (const uint64_t *cached = m_closureByPath.lookup_ptr(key)) {
    return *cached;
  }

  Vector<uint8_t> seen;
  seen.resize(m_files.size());
  for (int i = 0; i < int(seen.size()); i++) {
    seen[i] = 0;
  }
  Vector<int> stack;
  Vector<uint64_t> hashes;
  stack.append(root);
  seen[root] = 1;
  while (stack.size() > 0) {
    int index = stack[int(stack.size()) - 1];
    stack.pop_back();
    const FileEntry &entry = m_files[index];
    hashes.append(entry.selfHash);
    for (const string &import : entry.imports) {
      int next = indexOf(view(import));
      if (next >= 0 && !seen[next]) {
        seen[next] = 1;
        stack.append(next);
      }
    }
  }
  hashes.sort(
      [](const uint64_t &a, const uint64_t &b) { return a < b ? -1 : (a > b ? 1 : 0); });
  Hasher h;
  h.u32(uint32_t(hashes.size()));
  for (uint64_t value : hashes) {
    h.u64(value);
  }
  m_closureByPath.add(key, h.value);
  return h.value;
}

void ImportGraph::clear()
{
  m_files.clear();
  m_byPath.clear();
  m_closureByPath.clear();
}

// ---------------------------------------------------------------- loading

bool loadClosure(ImportGraph &graph, std::string_view path, FileSystem &fs, string &error)
{
  Vector<string> pending;
  pending.append(joinPath(path, ""));
  while (pending.size() > 0) {
    string current = pending[int(pending.size()) - 1];
    pending.pop_back();
    std::string_view currentPath = view(current);

    string content;
    if (!fs.readFile(currentPath, content)) {
      error = string("cannot read ");
      error += current;
      return false;
    }
    uint64_t contentHash = hashContent(view(content));
    const FileEntry *existing = graph.file(currentPath);
    if (existing && existing->contentHash == contentHash) {
      continue;
    }

    syntax::Parser::Options options;
    options.jsx = endsWith(currentPath, ".tsx") || endsWith(currentPath, ".jsx");
    options.javaScript = endsWith(currentPath, ".js") || endsWith(currentPath, ".jsx") ||
                         endsWith(currentPath, ".mjs") || endsWith(currentPath, ".cjs");
    syntax::Diagnostics diagnostics;
    syntax::GrammarTree tree;
    ast::AstFile file(&tree);
    syntax::Parser parser(view(content), options, diagnostics);
    parser.parseFile(tree);
    ast::lower(tree, file);

    Vector<string> specifiers;
    collectImports(file, specifiers);
    Vector<string> imports;
    Vector<string> unresolved;
    for (const string &specifier : specifiers) {
      string resolved = resolveImport(currentPath, view(specifier), fs);
      if (resolved.size() == 0) {
        unresolved.append(specifier);
        continue;
      }
      if (!graph.file(view(resolved))) {
        pending.append(resolved);
      }
      imports.append(std::move(resolved));
    }
    graph.setFile(currentPath,
                  contentHash,
                  span<const string>(imports.data(), imports.size()),
                  span<const string>(unresolved.data(), unresolved.size()));
  }
  return true;
}

// ---------------------------------------------------------------- freshness

bool FileCache::lookup(std::string_view path,
                       FileRecord &record,
                       Freshness &freshness,
                       string &error)
{
  const FileEntry *entry = m_graph.file(path);
  if (!entry) {
    error = string("file is not in the import graph: ");
    error += toString(path);
    return false;
  }
  record.path = entry->path;
  record.contentHash = entry->contentHash;
  record.closureHash = m_graph.closureHash(path);
  record.tsconfigHash = m_tsconfigHash;

  FileRecord stored;
  bool found = false;
  if (!m_store.getFile(path, stored, found, error)) {
    return false;
  }
  if (!found) {
    freshness = Freshness::Missing;
    return true;
  }
  if (stored.contentHash == record.contentHash &&
      stored.closureHash == record.closureHash &&
      stored.tsconfigHash == record.tsconfigHash)
  {
    freshness = Freshness::Fresh;
    return true;
  }
  freshness = Freshness::Stale;
  return m_store.dropNodeTypes(stored.contentHash, error) &&
         m_store.dropRuleResults(stored.contentHash, error);
}

bool FileCache::commitFile(const FileRecord &record, string &error)
{
  return m_store.putFile(record, error);
}

bool FileCache::ruleResult(const FileRecord &record,
                           std::string_view rule,
                           string &payload,
                           bool &found,
                           string &error)
{
  return m_store.getRuleResult(
      record.contentHash, record.closureHash, rule, payload, found, error);
}

bool FileCache::saveRuleResult(const FileRecord &record,
                               std::string_view rule,
                               std::string_view payload,
                               string &error)
{
  return m_store.putRuleResult(
      record.contentHash, record.closureHash, rule, payload, error);
}

} // namespace fastlint::cache
