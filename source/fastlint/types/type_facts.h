#pragma once

#include "fastlint/tsgo/queries.h"
#include "fastlint/tsgo/source_file.h"
#include "fastlint/types/type_graph.h"

#include <string_view>

namespace fastlint::ast {
class AstFile;
struct Node;
} // namespace fastlint::ast

namespace fastlint::types {

/** One call or construct signature with its return type interned. */
struct Signature {
  int sessionId = 0;
  TypeId returnType = 0;
  Vector<SymbolId, 4> parameters;
};

struct FactsStats {
  int nodeHits = 0;
  int nodeMisses = 0;
  int unmappedNodes = 0;
  int typeFetches = 0;
  int childFetches = 0;
  int symbolFetches = 0;
};

/** The type questions rules ask, answered from the interned graph and fetched lazily from
 * one tsgo session. Node queries refer to the file selected with `beginFile`; a `TypeId`
 * stays valid for the graph's lifetime, while server-backed questions need the snapshot
 * that minted the type to be alive. */
class TypeFacts {
public:
  TypeFacts(tsgo::Session &session, TypeGraph &graph);

  /** Selects the file later node queries refer to. `path` is the path the server knows
   * the file by; the node side table is fetched on the first query. */
  bool beginFile(const ast::AstFile &file, std::string_view path, string &error);
  /** Drops the per-file working set (node types and the side table); the graph keeps
   * every row. */
  void endFile();

  /** Type of an expression node, or 0 when the server has none or the node has no tsgo
   * counterpart. Fetched once per node per file. */
  TypeId typeOf(const ast::Node *node);
  /** Fetches the types of `nodes` in one request so later `typeOf` calls hit the cache.
   */
  bool prefetch(span<const ast::Node *const> nodes);

  bool isAnyLike(TypeId type) const;
  /** `undefined`, `null`, `void`, or a union with such a member. */
  bool isNullable(TypeId type) const;
  /** `Promise`, `PromiseLike`, an object with a callable `then`, or a union with such a
   * member. */
  bool isPromiseLike(TypeId type);
  bool isArrayLike(TypeId type);
  /** Members of a union or intersection; empty for other types. */
  span<const TypeId> unionMembers(TypeId type) const;
  bool callSignatures(TypeId type, Vector<Signature> &signatures);
  bool assignableTo(TypeId from, TypeId to, bool &result);
  SymbolId symbolOf(TypeId type) const;
  /** Declaration node handles of a symbol, as string ids. */
  span<const StringId> declarationsOf(SymbolId symbol) const;
  /** The canonical file path inside a declaration handle (`index.kind.path`). */
  static std::string_view declarationFile(std::string_view handle);
  std::string_view text(StringId id) const
  {
    return m_graph.text(id);
  }

  const TypeGraph &graph() const
  {
    return m_graph;
  }
  /** The last server or transport failure; node queries return 0 rather than failing. */
  const string &lastError() const
  {
    return m_error;
  }
  const FactsStats &stats() const
  {
    return m_stats;
  }

private:
  bool ensureTable();
  /** Interns a response; `withChildren` fetches one hop (union members, type arguments).
   */
  TypeId internResponse(const tsgo::TypeResponse &type, bool withChildren);
  SymbolId internSymbol(const tsgo::SymbolResponse &symbol);
  SymbolId symbolOfSession(int typeSessionId, bool alias);
  bool thenable(TypeId type);
  bool nameIs(SymbolId symbol, std::string_view a, std::string_view b) const;

  tsgo::Session &m_session;
  TypeGraph &m_graph;
  const ast::AstFile *m_file = nullptr;
  string m_path;
  string m_canonical;
  tsgo::EncodedSourceFile m_encoded;
  tsgo::NodeIndexTable m_table;
  bool m_tableReady = false;
  bool m_tableFailed = false;
  Map<const ast::Node *, TypeId> m_nodeTypes;
  Map<int, bool> m_arrayLike;
  Map<int, bool> m_promiseLike;
  string m_error;
  FactsStats m_stats;
};

} // namespace fastlint::types
