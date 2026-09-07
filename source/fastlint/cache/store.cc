#include "fastlint/cache/store.h"

#include "sqlite3.h"

#include <cstdio>
#include <string>

namespace fastlint::cache {

namespace {

using types::SymbolId;
using types::SymbolRow;
using types::TypeGraph;
using types::TypeId;
using types::TypeRow;

/** A prepared statement finalized on scope exit. */
struct Statement {
  sqlite3_stmt *stmt = nullptr;
  ~Statement()
  {
    sqlite3_finalize(stmt);
  }
};

int64_t signedHash(uint64_t hash)
{
  return int64_t(hash);
}

uint64_t unsignedHash(int64_t value)
{
  return uint64_t(value);
}

// An empty view may carry a null pointer, which sqlite would bind as NULL and the NOT
// NULL columns would then reject.
void bindText(sqlite3_stmt *stmt, int index, std::string_view text)
{
  const char *data = text.data() ? text.data() : "";
  sqlite3_bind_text(stmt, index, data, int(text.size()), SQLITE_TRANSIENT);
}

std::string_view columnText(sqlite3_stmt *stmt, int index)
{
  const unsigned char *text = sqlite3_column_text(stmt, index);
  if (!text) {
    return {};
  }
  return std::string_view(reinterpret_cast<const char *>(text),
                          size_t(sqlite3_column_bytes(stmt, index)));
}

string toString(std::string_view text)
{
  string out;
  for (char c : text) {
    out += c;
  }
  return out;
}

std::string_view view(const string &s)
{
  return std::string_view(s.c_str(), s.size());
}

const char *kSchema[] = {
    "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL)",
    "CREATE TABLE IF NOT EXISTS files(path TEXT PRIMARY KEY, content_hash INTEGER NOT "
    "NULL,"
    " closure_hash INTEGER NOT NULL, tsconfig_hash INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS types(hash INTEGER PRIMARY KEY, seq INTEGER NOT NULL,"
    " flags INTEGER NOT NULL, object_flags INTEGER NOT NULL, is_tuple INTEGER NOT NULL,"
    " symbol_hash INTEGER NOT NULL, alias_hash INTEGER NOT NULL, text TEXT NOT NULL,"
    " child_kind INTEGER NOT NULL)",
    "CREATE INDEX IF NOT EXISTS types_seq ON types(seq)",
    "CREATE TABLE IF NOT EXISTS type_children(parent_hash INTEGER NOT NULL,"
    " ordinal INTEGER NOT NULL, child_hash INTEGER NOT NULL, PRIMARY KEY(parent_hash, "
    "ordinal))",
    "CREATE TABLE IF NOT EXISTS symbols(hash INTEGER PRIMARY KEY, seq INTEGER NOT NULL,"
    " name TEXT NOT NULL, flags INTEGER NOT NULL, check_flags INTEGER NOT NULL)",
    "CREATE INDEX IF NOT EXISTS symbols_seq ON symbols(seq)",
    "CREATE TABLE IF NOT EXISTS symbol_declarations(symbol_hash INTEGER NOT NULL,"
    " ordinal INTEGER NOT NULL, handle TEXT NOT NULL, PRIMARY KEY(symbol_hash, ordinal))",
    "CREATE TABLE IF NOT EXISTS node_types(file_hash INTEGER NOT NULL, start INTEGER NOT "
    "NULL,"
    " end INTEGER NOT NULL, kind INTEGER NOT NULL, type_hash INTEGER NOT NULL,"
    " PRIMARY KEY(file_hash, start, end, kind))",
    "CREATE TABLE IF NOT EXISTS rule_results(file_hash INTEGER NOT NULL,"
    " closure_hash INTEGER NOT NULL, rule TEXT NOT NULL, payload BLOB NOT NULL,"
    " PRIMARY KEY(file_hash, closure_hash, rule))",
};

const char *kTables[] = {
    "meta",
    "files",
    "types",
    "type_children",
    "symbols",
    "symbol_declarations",
    "node_types",
    "rule_results",
};

} // namespace

// ---------------------------------------------------------------- lifecycle

Store::~Store()
{
  close();
}

bool Store::fail(string &error, const char *what)
{
  error = string(what);
  if (m_db) {
    error += ": ";
    error += sqlite3_errmsg(m_db);
  }
  return false;
}

bool Store::exec(const char *sql, string &error)
{
  char *message = nullptr;
  if (sqlite3_exec(m_db, sql, nullptr, nullptr, &message) != SQLITE_OK) {
    error = string("sqlite: ");
    error += message ? message : "unknown error";
    error += " in ";
    error += sql;
    sqlite3_free(message);
    return false;
  }
  return true;
}

bool Store::prepare(sqlite3_stmt *&stmt, const char *sql, string &error)
{
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    error = string("sqlite prepare: ");
    error += sqlite3_errmsg(m_db);
    error += " in ";
    error += sql;
    return false;
  }
  return true;
}

bool Store::createSchema(string &error)
{
  for (const char *sql : kSchema) {
    if (!exec(sql, error)) {
      return false;
    }
  }
  return true;
}

bool Store::dropSchema(string &error)
{
  for (const char *table : kTables) {
    std::string sql = std::string("DROP TABLE IF EXISTS ") + table;
    if (!exec(sql.c_str(), error)) {
      return false;
    }
  }
  return true;
}

bool Store::writeMeta(const StoreOptions &options, string &error)
{
  Statement s;
  if (!prepare(s.stmt, "INSERT OR REPLACE INTO meta(key, value) VALUES(?, ?)", error)) {
    return false;
  }
  char buf[32];
  snprintf(buf, sizeof buf, "%d", kSchemaVersion);
  const char *keys[] = {"schema_version", "tsgo_version", "lib_hash"};
  char libHash[32];
  snprintf(
      libHash, sizeof libHash, "%llu", static_cast<unsigned long long>(options.libHash));
  const char *values[] = {buf, options.tsgoVersion.c_str(), libHash};
  for (int i = 0; i < 3; i++) {
    sqlite3_reset(s.stmt);
    bindText(s.stmt, 1, keys[i]);
    bindText(s.stmt, 2, values[i]);
    if (sqlite3_step(s.stmt) != SQLITE_DONE) {
      return fail(error, "writing meta");
    }
  }
  return true;
}

bool Store::readMeta(StoreMeta &meta, string &error)
{
  meta = StoreMeta();
  Statement s;
  if (!prepare(s.stmt, "SELECT key, value FROM meta", error)) {
    return false;
  }
  int rc;
  while ((rc = sqlite3_step(s.stmt)) == SQLITE_ROW) {
    std::string_view key = columnText(s.stmt, 0);
    std::string_view value = columnText(s.stmt, 1);
    std::string text(value);
    if (key == "schema_version") {
      meta.schemaVersion = atoi(text.c_str());
    } else if (key == "tsgo_version") {
      meta.tsgoVersion = toString(value);
    } else if (key == "lib_hash") {
      meta.libHash = strtoull(text.c_str(), nullptr, 10);
    }
  }
  return rc == SQLITE_DONE || fail(error, "reading meta");
}

bool Store::open(const StoreOptions &options, string &error)
{
  close();
  m_rebuilt = false;
  int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
  if (sqlite3_open_v2(options.path.c_str(), &m_db, flags, nullptr) != SQLITE_OK) {
    fail(error, "opening the cache");
    close();
    return false;
  }
  bool memory = view(options.path) == ":memory:" ||
                options.path.starts_with(string("file::memory:"));
  if (!memory && !exec("PRAGMA journal_mode=WAL", error)) {
    close();
    return false;
  }
  if (!exec("PRAGMA synchronous=NORMAL", error) ||
      !exec("PRAGMA foreign_keys=OFF", error))
  {
    close();
    return false;
  }
  if (!createSchema(error)) {
    close();
    return false;
  }
  StoreMeta meta;
  if (!readMeta(meta, error)) {
    close();
    return false;
  }
  bool fresh = meta.schemaVersion == 0;
  bool mismatch = meta.schemaVersion != kSchemaVersion ||
                  meta.tsgoVersion != options.tsgoVersion ||
                  meta.libHash != options.libHash;
  if (!fresh && mismatch) {
    if (!dropSchema(error) || !createSchema(error)) {
      close();
      return false;
    }
    m_rebuilt = true;
  }
  if ((fresh || mismatch) && !writeMeta(options, error)) {
    close();
    return false;
  }
  return true;
}

void Store::close()
{
  if (m_db) {
    sqlite3_close(m_db);
    m_db = nullptr;
  }
}

bool Store::begin(string &error)
{
  return exec("BEGIN IMMEDIATE", error);
}

bool Store::commit(string &error)
{
  return exec("COMMIT", error);
}

void Store::rollback()
{
  string ignored;
  exec("ROLLBACK", ignored);
}

// ---------------------------------------------------------------- files

bool Store::putFile(const FileRecord &file, string &error)
{
  Statement s;
  if (!prepare(
          s.stmt,
          "INSERT OR REPLACE INTO files(path, content_hash, closure_hash, tsconfig_hash)"
          " VALUES(?, ?, ?, ?)",
          error))
  {
    return false;
  }
  bindText(s.stmt, 1, view(file.path));
  sqlite3_bind_int64(s.stmt, 2, signedHash(file.contentHash));
  sqlite3_bind_int64(s.stmt, 3, signedHash(file.closureHash));
  sqlite3_bind_int64(s.stmt, 4, signedHash(file.tsconfigHash));
  return sqlite3_step(s.stmt) == SQLITE_DONE || fail(error, "writing a file record");
}

bool Store::getFile(std::string_view path, FileRecord &file, bool &found, string &error)
{
  found = false;
  Statement s;
  if (!prepare(
          s.stmt,
          "SELECT content_hash, closure_hash, tsconfig_hash FROM files WHERE path = ?",
          error))
  {
    return false;
  }
  bindText(s.stmt, 1, path);
  int rc = sqlite3_step(s.stmt);
  if (rc == SQLITE_ROW) {
    found = true;
    file.path = toString(path);
    file.contentHash = unsignedHash(sqlite3_column_int64(s.stmt, 0));
    file.closureHash = unsignedHash(sqlite3_column_int64(s.stmt, 1));
    file.tsconfigHash = unsignedHash(sqlite3_column_int64(s.stmt, 2));
    return true;
  }
  return rc == SQLITE_DONE || fail(error, "reading a file record");
}

bool Store::removeFile(std::string_view path, string &error)
{
  Statement s;
  if (!prepare(s.stmt, "DELETE FROM files WHERE path = ?", error)) {
    return false;
  }
  bindText(s.stmt, 1, path);
  return sqlite3_step(s.stmt) == SQLITE_DONE || fail(error, "removing a file record");
}

// ---------------------------------------------------------------- graph

bool Store::saveGraph(const TypeGraph &graph, GraphCursor &cursor, string &error)
{
  Statement symbolStmt, declStmt, typeStmt, childStmt;
  if (!prepare(symbolStmt.stmt,
               "INSERT OR IGNORE INTO symbols(hash, seq, name, flags, check_flags)"
               " VALUES(?, ?, ?, ?, ?)",
               error) ||
      !prepare(declStmt.stmt,
               "INSERT OR IGNORE INTO symbol_declarations(symbol_hash, ordinal, handle)"
               " VALUES(?, ?, ?)",
               error) ||
      !prepare(
          typeStmt.stmt,
          "INSERT OR IGNORE INTO types(hash, seq, flags, object_flags, is_tuple,"
          " symbol_hash, alias_hash, text, child_kind) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)",
          error) ||
      !prepare(childStmt.stmt,
               "INSERT OR IGNORE INTO type_children(parent_hash, ordinal, child_hash)"
               " VALUES(?, ?, ?)",
               error))
  {
    return false;
  }

  for (size_t i = cursor.symbols + 1; i <= graph.symbolCount(); i++) {
    const SymbolRow &row = graph.symbol(SymbolId(i));
    sqlite3_reset(symbolStmt.stmt);
    sqlite3_bind_int64(symbolStmt.stmt, 1, signedHash(row.hash));
    sqlite3_bind_int64(symbolStmt.stmt, 2, int64_t(i));
    bindText(symbolStmt.stmt, 3, graph.text(row.name));
    sqlite3_bind_int64(symbolStmt.stmt, 4, row.flags);
    sqlite3_bind_int64(symbolStmt.stmt, 5, row.checkFlags);
    if (sqlite3_step(symbolStmt.stmt) != SQLITE_DONE) {
      return fail(error, "writing a symbol row");
    }
    int ordinal = 0;
    for (types::StringId handle : graph.declarations(SymbolId(i))) {
      sqlite3_reset(declStmt.stmt);
      sqlite3_bind_int64(declStmt.stmt, 1, signedHash(row.hash));
      sqlite3_bind_int(declStmt.stmt, 2, ordinal++);
      bindText(declStmt.stmt, 3, graph.text(handle));
      if (sqlite3_step(declStmt.stmt) != SQLITE_DONE) {
        return fail(error, "writing a declaration");
      }
    }
  }
  cursor.symbols = graph.symbolCount();

  for (size_t i = cursor.types + 1; i <= graph.typeCount(); i++) {
    const TypeRow &row = graph.type(TypeId(i));
    sqlite3_reset(typeStmt.stmt);
    sqlite3_bind_int64(typeStmt.stmt, 1, signedHash(row.hash));
    sqlite3_bind_int64(typeStmt.stmt, 2, int64_t(i));
    sqlite3_bind_int64(typeStmt.stmt, 3, row.flags);
    sqlite3_bind_int64(typeStmt.stmt, 4, row.objectFlags);
    sqlite3_bind_int(typeStmt.stmt, 5, row.isTuple ? 1 : 0);
    sqlite3_bind_int64(
        typeStmt.stmt, 6, row.symbol ? signedHash(graph.symbol(row.symbol).hash) : 0);
    sqlite3_bind_int64(typeStmt.stmt,
                       7,
                       row.aliasSymbol ? signedHash(graph.symbol(row.aliasSymbol).hash)
                                       : 0);
    bindText(typeStmt.stmt, 8, graph.text(row.text));
    sqlite3_bind_int(typeStmt.stmt, 9, int(row.childKind));
    if (sqlite3_step(typeStmt.stmt) != SQLITE_DONE) {
      return fail(error, "writing a type row");
    }
    int ordinal = 0;
    for (TypeId child : graph.children(TypeId(i))) {
      sqlite3_reset(childStmt.stmt);
      sqlite3_bind_int64(childStmt.stmt, 1, signedHash(row.hash));
      sqlite3_bind_int(childStmt.stmt, 2, ordinal++);
      sqlite3_bind_int64(childStmt.stmt, 3, signedHash(graph.type(child).hash));
      if (sqlite3_step(childStmt.stmt) != SQLITE_DONE) {
        return fail(error, "writing a type child");
      }
    }
  }
  cursor.types = graph.typeCount();
  return true;
}

bool Store::loadGraph(TypeGraph &graph, string &error)
{
  // Symbols first, since type rows refer to them; both in the order they were interned so
  // every child precedes its parent.
  Statement symbols, decls, typesStmt, children;
  if (!prepare(symbols.stmt,
               "SELECT hash, name, flags, check_flags FROM symbols ORDER BY seq",
               error) ||
      !prepare(
          decls.stmt,
          "SELECT handle FROM symbol_declarations WHERE symbol_hash = ? ORDER BY ordinal",
          error) ||
      !prepare(
          typesStmt.stmt,
          "SELECT hash, flags, object_flags, is_tuple, symbol_hash, alias_hash, text,"
          " child_kind FROM types ORDER BY seq",
          error) ||
      !prepare(
          children.stmt,
          "SELECT child_hash FROM type_children WHERE parent_hash = ? ORDER BY ordinal",
          error))
  {
    return false;
  }

  int rc;
  while ((rc = sqlite3_step(symbols.stmt)) == SQLITE_ROW) {
    int64_t hash = sqlite3_column_int64(symbols.stmt, 0);
    Vector<string> handles;
    sqlite3_reset(decls.stmt);
    sqlite3_bind_int64(decls.stmt, 1, hash);
    int drc;
    while ((drc = sqlite3_step(decls.stmt)) == SQLITE_ROW) {
      handles.append(toString(columnText(decls.stmt, 0)));
    }
    if (drc != SQLITE_DONE) {
      return fail(error, "reading declarations");
    }
    SymbolId id = graph.internSymbol(columnText(symbols.stmt, 1),
                                     uint32_t(sqlite3_column_int64(symbols.stmt, 2)),
                                     uint32_t(sqlite3_column_int64(symbols.stmt, 3)),
                                     span<const string>(handles.data(), handles.size()),
                                     0);
    if (graph.symbol(id).hash != unsignedHash(hash)) {
      char buf[96];
      snprintf(buf,
               sizeof buf,
               "stored symbol hash %llx does not match its contents",
               static_cast<unsigned long long>(hash));
      error = string(buf);
      return false;
    }
  }
  if (rc != SQLITE_DONE) {
    return fail(error, "reading symbols");
  }

  while ((rc = sqlite3_step(typesStmt.stmt)) == SQLITE_ROW) {
    int64_t hash = sqlite3_column_int64(typesStmt.stmt, 0);
    TypeRow row;
    row.flags = uint32_t(sqlite3_column_int64(typesStmt.stmt, 1));
    row.objectFlags = uint32_t(sqlite3_column_int64(typesStmt.stmt, 2));
    row.isTuple = sqlite3_column_int(typesStmt.stmt, 3) != 0;
    int64_t symbolHash = sqlite3_column_int64(typesStmt.stmt, 4);
    int64_t aliasHash = sqlite3_column_int64(typesStmt.stmt, 5);
    row.symbol = symbolHash ? graph.symbolByHash(unsignedHash(symbolHash)) : 0;
    row.aliasSymbol = aliasHash ? graph.symbolByHash(unsignedHash(aliasHash)) : 0;
    if ((symbolHash && !row.symbol) || (aliasHash && !row.aliasSymbol)) {
      error = string("stored type refers to a symbol that is not in the store");
      return false;
    }
    std::string_view text = columnText(typesStmt.stmt, 6);
    row.text = text.empty() ? 0 : graph.internString(text);
    row.childKind = types::ChildKind(sqlite3_column_int(typesStmt.stmt, 7));
    Vector<TypeId> kids;
    sqlite3_reset(children.stmt);
    sqlite3_bind_int64(children.stmt, 1, hash);
    int crc;
    while ((crc = sqlite3_step(children.stmt)) == SQLITE_ROW) {
      TypeId child = graph.byHash(unsignedHash(sqlite3_column_int64(children.stmt, 0)));
      if (!child) {
        error = string("stored type child precedes its own row");
        return false;
      }
      kids.append(child);
    }
    if (crc != SQLITE_DONE) {
      return fail(error, "reading type children");
    }
    TypeId id = graph.intern(row, span<const TypeId>(kids.data(), kids.size()));
    if (graph.type(id).hash != unsignedHash(hash)) {
      char buf[96];
      snprintf(buf,
               sizeof buf,
               "stored type hash %llx does not match its contents",
               static_cast<unsigned long long>(hash));
      error = string(buf);
      return false;
    }
  }
  return rc == SQLITE_DONE || fail(error, "reading types");
}

bool Store::countTypes(size_t &typeCount, size_t &symbolCount, string &error)
{
  Statement s;
  if (!prepare(s.stmt,
               "SELECT (SELECT COUNT(*) FROM types), (SELECT COUNT(*) FROM symbols)",
               error))
  {
    return false;
  }
  if (sqlite3_step(s.stmt) != SQLITE_ROW) {
    return fail(error, "counting rows");
  }
  typeCount = size_t(sqlite3_column_int64(s.stmt, 0));
  symbolCount = size_t(sqlite3_column_int64(s.stmt, 1));
  return true;
}

// ---------------------------------------------------------------- node types

bool Store::putNodeTypes(uint64_t fileHash, span<const NodeType> nodes, string &error)
{
  Statement s;
  if (!prepare(s.stmt,
               "INSERT OR REPLACE INTO node_types(file_hash, start, end, kind, type_hash)"
               " VALUES(?, ?, ?, ?, ?)",
               error))
  {
    return false;
  }
  for (const NodeType &node : nodes) {
    sqlite3_reset(s.stmt);
    sqlite3_bind_int64(s.stmt, 1, signedHash(fileHash));
    sqlite3_bind_int64(s.stmt, 2, node.start);
    sqlite3_bind_int64(s.stmt, 3, node.end);
    sqlite3_bind_int64(s.stmt, 4, node.kind);
    sqlite3_bind_int64(s.stmt, 5, signedHash(node.typeHash));
    if (sqlite3_step(s.stmt) != SQLITE_DONE) {
      return fail(error, "writing a node type");
    }
  }
  return true;
}

bool Store::getNodeTypes(uint64_t fileHash, Vector<NodeType> &nodes, string &error)
{
  nodes.clear();
  Statement s;
  if (!prepare(s.stmt,
               "SELECT start, end, kind, type_hash FROM node_types WHERE file_hash = ?"
               " ORDER BY start, end, kind",
               error))
  {
    return false;
  }
  sqlite3_bind_int64(s.stmt, 1, signedHash(fileHash));
  int rc;
  while ((rc = sqlite3_step(s.stmt)) == SQLITE_ROW) {
    NodeType node;
    node.start = uint32_t(sqlite3_column_int64(s.stmt, 0));
    node.end = uint32_t(sqlite3_column_int64(s.stmt, 1));
    node.kind = uint32_t(sqlite3_column_int64(s.stmt, 2));
    node.typeHash = unsignedHash(sqlite3_column_int64(s.stmt, 3));
    nodes.append(node);
  }
  return rc == SQLITE_DONE || fail(error, "reading node types");
}

bool Store::dropNodeTypes(uint64_t fileHash, string &error)
{
  Statement s;
  if (!prepare(s.stmt, "DELETE FROM node_types WHERE file_hash = ?", error)) {
    return false;
  }
  sqlite3_bind_int64(s.stmt, 1, signedHash(fileHash));
  return sqlite3_step(s.stmt) == SQLITE_DONE || fail(error, "dropping node types");
}

// ---------------------------------------------------------------- rule results

bool Store::putRuleResult(uint64_t fileHash,
                          uint64_t closureHash,
                          std::string_view rule,
                          std::string_view payload,
                          string &error)
{
  Statement s;
  if (!prepare(
          s.stmt,
          "INSERT OR REPLACE INTO rule_results(file_hash, closure_hash, rule, payload)"
          " VALUES(?, ?, ?, ?)",
          error))
  {
    return false;
  }
  sqlite3_bind_int64(s.stmt, 1, signedHash(fileHash));
  sqlite3_bind_int64(s.stmt, 2, signedHash(closureHash));
  bindText(s.stmt, 3, rule);
  sqlite3_bind_blob(s.stmt, 4, payload.data(), int(payload.size()), SQLITE_TRANSIENT);
  return sqlite3_step(s.stmt) == SQLITE_DONE || fail(error, "writing a rule result");
}

bool Store::getRuleResult(uint64_t fileHash,
                          uint64_t closureHash,
                          std::string_view rule,
                          string &payload,
                          bool &found,
                          string &error)
{
  found = false;
  payload = string();
  Statement s;
  if (!prepare(s.stmt,
               "SELECT payload FROM rule_results WHERE file_hash = ? AND closure_hash = ?"
               " AND rule = ?",
               error))
  {
    return false;
  }
  sqlite3_bind_int64(s.stmt, 1, signedHash(fileHash));
  sqlite3_bind_int64(s.stmt, 2, signedHash(closureHash));
  bindText(s.stmt, 3, rule);
  int rc = sqlite3_step(s.stmt);
  if (rc == SQLITE_ROW) {
    found = true;
    const char *bytes = static_cast<const char *>(sqlite3_column_blob(s.stmt, 0));
    int size = sqlite3_column_bytes(s.stmt, 0);
    for (int i = 0; i < size; i++) {
      payload += bytes[i];
    }
    return true;
  }
  return rc == SQLITE_DONE || fail(error, "reading a rule result");
}

bool Store::dropRuleResults(uint64_t fileHash, string &error)
{
  Statement s;
  if (!prepare(s.stmt, "DELETE FROM rule_results WHERE file_hash = ?", error)) {
    return false;
  }
  sqlite3_bind_int64(s.stmt, 1, signedHash(fileHash));
  return sqlite3_step(s.stmt) == SQLITE_DONE || fail(error, "dropping rule results");
}

// ---------------------------------------------------------------- verify

bool Store::verify(Vector<string> &problems, string &error)
{
  problems.clear();
  Statement integrity;
  if (!prepare(integrity.stmt, "PRAGMA integrity_check", error)) {
    return false;
  }
  int rc;
  while ((rc = sqlite3_step(integrity.stmt)) == SQLITE_ROW) {
    std::string_view line = columnText(integrity.stmt, 0);
    if (line != "ok") {
      problems.append(toString(line));
    }
  }
  if (rc != SQLITE_DONE) {
    return fail(error, "integrity check");
  }
  const char *checks[] = {
      "SELECT COUNT(*) FROM type_children c LEFT JOIN types t ON t.hash = c.child_hash"
      " WHERE t.hash IS NULL",
      "SELECT COUNT(*) FROM types WHERE symbol_hash != 0 AND symbol_hash NOT IN"
      " (SELECT hash FROM symbols)",
      "SELECT COUNT(*) FROM types WHERE alias_hash != 0 AND alias_hash NOT IN"
      " (SELECT hash FROM symbols)",
      "SELECT COUNT(*) FROM node_types WHERE type_hash NOT IN (SELECT hash FROM types)",
  };
  const char *names[] = {
      "type children without a row",
      "types whose symbol is missing",
      "types whose alias symbol is missing",
      "node types pointing at a missing type",
  };
  for (int i = 0; i < 4; i++) {
    Statement s;
    if (!prepare(s.stmt, checks[i], error)) {
      return false;
    }
    if (sqlite3_step(s.stmt) != SQLITE_ROW) {
      return fail(error, "verify query");
    }
    int64_t count = sqlite3_column_int64(s.stmt, 0);
    if (count) {
      char buf[128];
      snprintf(buf, sizeof buf, "%lld %s", static_cast<long long>(count), names[i]);
      problems.append(string(buf));
    }
  }
  return true;
}

} // namespace fastlint::cache
