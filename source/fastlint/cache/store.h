#pragma once

#include "fastlint/types/type_graph.h"
#include "util/span.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace fastlint::cache {

using litestl::util::span;
using litestl::util::Vector;
using string = litestl::util::string;

/** Bumped whenever a table changes shape; a store written by another version is rebuilt.
 */
constexpr int kSchemaVersion = 1;

struct StoreOptions {
  /** Database file, or `:memory:` for a private in-process store. */
  string path;
  /** The pinned `tsc --version`; a store written against another is rebuilt. */
  string tsgoVersion;
  /** Hash of the lib `.d.ts` set; a change invalidates every type row. */
  uint64_t libHash = 0;
};

struct FileRecord {
  string path;
  uint64_t contentHash = 0;
  uint64_t closureHash = 0;
  uint64_t tsconfigHash = 0;
};

/** The type of one node, keyed by span and kind within a file version. */
struct NodeType {
  uint32_t start = 0;
  uint32_t end = 0;
  uint32_t kind = 0;
  uint64_t typeHash = 0;
};

struct StoreMeta {
  int schemaVersion = 0;
  string tsgoVersion;
  uint64_t libHash = 0;
};

/** How much of a graph a store has already been given; lets `saveGraph` write only the
 * rows appended since the last call. */
struct GraphCursor {
  size_t types = 0;
  size_t symbols = 0;
};

/** The SQLite type cache: interned type rows, per-file node types and rule results. One
 * connection, single writer; wrap a file's writes in `begin`/`commit`. Every method
 * returns false with `error` set on a database failure. */
class Store {
public:
  Store() = default;
  ~Store();
  Store(const Store &) = delete;
  Store &operator=(const Store &) = delete;

  /** Opens or creates the database in WAL mode and checks `meta`; a schema, tsgo version
   * or lib hash mismatch drops every table and starts over (`rebuilt()` reports it). */
  bool open(const StoreOptions &options, string &error);
  void close();
  bool isOpen() const
  {
    return m_db != nullptr;
  }
  bool rebuilt() const
  {
    return m_rebuilt;
  }

  bool begin(string &error);
  bool commit(string &error);
  void rollback();

  bool readMeta(StoreMeta &meta, string &error);

  bool putFile(const FileRecord &file, string &error);
  /** `found` is false when `path` has no record; that is not an error. */
  bool getFile(std::string_view path, FileRecord &file, bool &found, string &error);
  bool removeFile(std::string_view path, string &error);

  /** Upserts the graph rows past `cursor` (types, children, symbols, declarations) and
   * advances it. */
  bool saveGraph(const types::TypeGraph &graph, GraphCursor &cursor, string &error);
  /** Interns every stored row into `graph`, children before parents, so ids and hashes
   * come out as they were saved. */
  bool loadGraph(types::TypeGraph &graph, string &error);
  bool countTypes(size_t &types, size_t &symbols, string &error);

  bool putNodeTypes(uint64_t fileHash, span<const NodeType> nodes, string &error);
  bool getNodeTypes(uint64_t fileHash, Vector<NodeType> &nodes, string &error);
  bool dropNodeTypes(uint64_t fileHash, string &error);

  bool putRuleResult(uint64_t fileHash,
                     uint64_t closureHash,
                     std::string_view rule,
                     std::string_view payload,
                     string &error);
  bool getRuleResult(uint64_t fileHash,
                     uint64_t closureHash,
                     std::string_view rule,
                     string &payload,
                     bool &found,
                     string &error);
  bool dropRuleResults(uint64_t fileHash, string &error);

  /** Runs `PRAGMA integrity_check` and confirms every child and symbol reference
   * resolves; `problems` lists what does not. */
  bool verify(Vector<string> &problems, string &error);

private:
  bool exec(const char *sql, string &error);
  bool prepare(sqlite3_stmt *&stmt, const char *sql, string &error);
  bool fail(string &error, const char *what);
  bool createSchema(string &error);
  bool dropSchema(string &error);
  bool writeMeta(const StoreOptions &options, string &error);

  sqlite3 *m_db = nullptr;
  bool m_rebuilt = false;
};

} // namespace fastlint::cache
