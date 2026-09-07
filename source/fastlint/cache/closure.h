#pragma once

#include "fastlint/cache/store.h"
#include "util/map.h"
#include "util/span.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

namespace fastlint::cache {

using litestl::util::Map;

inline string toString(std::string_view text)
{
  string out;
  for (char c : text) {
    out += c;
  }
  return out;
}

inline std::string_view view(const string &s)
{
  return std::string_view(s.c_str(), s.size());
}

/** What the closure builder needs from the disk; tests substitute an in-memory tree. */
class FileSystem {
public:
  virtual ~FileSystem() = default;
  virtual bool isFile(std::string_view path) = 0;
  virtual bool readFile(std::string_view path, string &out) = 0;
};

/** Reads the real disk; the one place this module touches the OS. */
class DiskFileSystem : public FileSystem {
public:
  bool isFile(std::string_view path) override;
  bool readFile(std::string_view path, string &out) override;
};

/** FNV-1a over the bytes, the hash every `content_hash` column holds. */
uint64_t hashContent(std::string_view bytes);

/** `dir/rel` with `.` and `..` folded and every separator turned into `/`; an absolute
 * `rel` replaces `dir`. */
string joinPath(std::string_view dir, std::string_view rel);
string dirOf(std::string_view path);
/** True for `./x`, `../x` and `/x`; everything else is a bare package specifier. */
bool isRelativeSpecifier(std::string_view specifier);

/** Resolves a relative specifier the way tsc does under `moduleResolution: bundler`: the
 * exact path, then a `.js`-family extension rewritten to its TS source, then the TS
 * extensions appended, then a directory index. Returns an empty string for a bare
 * specifier or when nothing exists. */
string
resolveImport(std::string_view fromFile, std::string_view specifier, FileSystem &fs);

struct FileEntry {
  string path;
  uint64_t contentHash = 0;
  /** Resolved paths of the files this one imports. */
  Vector<string> imports;
  /** Specifiers that resolved to nothing, kept so adding or dropping one changes the
   * hash. */
  Vector<string> unresolved;
  /** Hash of the path, content hash and unresolved specifiers; a closure hash folds
   * these. */
  uint64_t selfHash = 0;
};

/** The import graph of the files a run has seen, and the closure hash each one derives
 * from it: the order-independent hash of every reachable file's `selfHash`, the file
 * itself included. Imports pointing at files not in the graph contribute nothing, so
 * callers load a file's whole closure before asking. */
class ImportGraph {
public:
  void setFile(std::string_view path,
               uint64_t contentHash,
               span<const string> imports,
               span<const string> unresolved);
  const FileEntry *file(std::string_view path) const;
  uint64_t closureHash(std::string_view path) const;
  size_t fileCount() const
  {
    return m_files.size();
  }
  void clear();

private:
  struct Key {
    uint64_t value;
    uint64_t computeHash() const
    {
      return value;
    }
    bool operator==(const Key &b) const
    {
      return value == b.value;
    }
  };
  int indexOf(std::string_view path) const;

  Vector<FileEntry> m_files;
  Map<Key, int> m_byPath;
  mutable Map<Key, uint64_t> m_closureByPath;
};

/** Reads, parses and adds `path` and, transitively, every relative import that resolves;
 * a file already in the graph with the same content hash is not reparsed. */
bool loadClosure(ImportGraph &graph,
                 std::string_view path,
                 FileSystem &fs,
                 string &error);

enum class Freshness {
  /** The store has no record of the file. */
  Missing,
  /** The record exists but its content, closure or tsconfig hash changed. */
  Stale,
  /** The stored record matches; node types and rule results can be replayed. */
  Fresh,
};

/** v1 file-closure invalidation over a store: a file is fresh when its content hash,
 * closure hash and tsconfig hash all match the stored record. */
class FileCache {
public:
  FileCache(Store &store, const ImportGraph &graph, uint64_t tsconfigHash)
      : m_store(store), m_graph(graph), m_tsconfigHash(tsconfigHash)
  {
  }

  /** Fills `record` with the current hashes from the graph and compares it with the
   * store. A stale file has its stored node types and rule results dropped here so the
   * caller can lint and refill it. Fails when the graph has not seen `path`. */
  bool
  lookup(std::string_view path, FileRecord &record, Freshness &freshness, string &error);
  /** Writes the record after a lint so the next lookup finds it fresh. */
  bool commitFile(const FileRecord &record, string &error);
  /** Replays one rule's stored payload for a fresh record; `found` is false on a miss. */
  bool ruleResult(const FileRecord &record,
                  std::string_view rule,
                  string &payload,
                  bool &found,
                  string &error);
  bool saveRuleResult(const FileRecord &record,
                      std::string_view rule,
                      std::string_view payload,
                      string &error);

private:
  Store &m_store;
  const ImportGraph &m_graph;
  uint64_t m_tsconfigHash;
};

} // namespace fastlint::cache
