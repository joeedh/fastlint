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
  uint32_t flags = 0;
  TypeId returnType = 0;
  Vector<SymbolId, 4> parameters;

  /** The last parameter is a `...rest` parameter. */
  bool hasRest() const;
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

  /** The type the position of `node` expects of it (a parameter type for an argument,
   * the annotated type for an initializer), or 0 when the position gives none. */
  TypeId contextualTypeOf(const ast::Node *node);

  uint32_t flags(TypeId type) const;
  uint32_t objectFlags(TypeId type) const;
  /** `any` or `unknown`. */
  bool isAnyLike(TypeId type) const;
  bool isAny(TypeId type) const;
  bool isUnknown(TypeId type) const;
  /** The `any` the checker gives an unresolved name. */
  bool isErrorType(TypeId type) const;
  bool isTypeParameter(TypeId type) const;
  /** The checker's spelling of the type (`Set<string>`, `any[]`); cached per row. */
  string typeToString(TypeId type);
  /** A literal type's value text or an intrinsic type's name (`5`, `foo`, `true`,
   * `undefined`), empty when the type carries neither. */
  std::string_view literalText(TypeId type) const;
  /** `undefined`, `null`, `void`, or a union with such a member. */
  bool isNullable(TypeId type) const;
  /** `Promise`, `PromiseLike`, an object with a callable `then`, or a union with such a
   * member. */
  bool isPromiseLike(TypeId type);
  bool isArrayLike(TypeId type);
  /** `T[]` or `readonly T[]`, not tuples; cached per row. */
  bool isArray(TypeId type);
  /** A tuple type; the pinned server omits the flag, so a reference's target is asked
   * once and cached. */
  bool isTuple(TypeId type);
  /** Members of a union or intersection, fetched when the row was interned without
   * them; empty for other types. */
  span<const TypeId> unionMembers(TypeId type);
  /** Type arguments of a generic reference (array element type, tuple elements, `T` of
   * `Promise<T>`); fetched when the row was interned without them. */
  span<const TypeId> typeArguments(TypeId type);
  /** The apparent type: a primitive's interface, a type parameter's constraint. */
  TypeId apparentType(TypeId type);
  /** The base constraint of a type parameter, or 0 when it has none. */
  TypeId constraintOf(TypeId type);
  /** Value type of the type's `number` index signature, or 0 when it has none. */
  TypeId numberIndexType(TypeId type);
  /** Type of the property `name`, or 0 when the type has no such property. */
  TypeId propertyType(TypeId type, std::string_view name);
  /** Whether the type has a property keyed by the well-known symbol `Symbol.<name>`. */
  bool hasWellKnownSymbolProperty(TypeId type, std::string_view name);
  /** Some union member of the apparent type has a call signature. */
  bool isCallable(TypeId type);
  /** A `then` method whose first `callbacks` parameters are callable, on some member of
   * the apparent type; `then(cb)` for 1, `then(ok, fail)` for 2. */
  bool isThenable(TypeId type, int callbacks);
  /** The type is the default library's `name` (`Promise`, `PromiseConstructor`), a class
   * or interface deriving from it, an intersection containing it, a union of such types,
   * or a type parameter constrained to one. */
  bool isBuiltin(TypeId type, std::string_view name);
  /** Some declaration of the symbol is in a `lib.*.d.ts` file. */
  bool isDefaultLibrary(SymbolId symbol) const;
  bool callSignatures(TypeId type, Vector<Signature> &signatures);
  bool constructSignatures(TypeId type, Vector<Signature> &signatures);
  /** The signature a call, `new` or tagged template resolves to; false when the checker
   * has none for the node. */
  bool resolvedSignature(const ast::Node *call, Signature &signature);
  /** The generic a type reference instantiates (`Set<T>` for `Set<string>`), or 0;
   * cached per row. Two instantiations of one generic share the target. */
  TypeId targetOf(TypeId type);
  /** What `await` yields: a promise's resolved value, unwrapped through nested promises;
   * a non-thenable type is its own awaited type. 0 for a union mixing promises and
   * other members, whose awaited type the graph cannot spell. */
  TypeId awaitedType(TypeId type);
  /** Declared type of a symbol (a parameter, a property), interned with one hop. */
  TypeId typeOfSymbol(SymbolId symbol);
  bool assignableTo(TypeId from, TypeId to, bool &result);
  SymbolId symbolOf(TypeId type) const;
  uint32_t symbolFlags(SymbolId symbol) const;
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

  /** The project's compiler options as `parseConfigFile` reports them; null when
   * unknown. */
  void setCompilerOptions(const tsgo::JsonValue *options)
  {
    m_compilerOptions = options;
  }
  /** Whether a strict-family option (`noImplicitThis`, `strictNullChecks`) is on: its
   * own setting when given, else `strict`. Unknown options count as on. */
  bool strictOption(std::string_view name) const;

private:
  bool ensureTable();
  /** Interns a response; `withChildren` fetches union members and type arguments, and
   * their own down to `kChildDepth`, so a row's hash carries its whole shape. */
  TypeId internResponse(const tsgo::TypeResponse &type, bool withChildren, int depth = 0);
  SymbolId internSymbol(const tsgo::SymbolResponse &symbol);
  SymbolId symbolOfSession(int typeSessionId, bool alias);
  bool thenable(TypeId type);
  bool nameIs(SymbolId symbol, std::string_view a, std::string_view b) const;
  /** Interns a fresh row for a compound type whose children were not fetched. */
  TypeId deepen(TypeId type);
  bool fetchChildren(int sessionId,
                     uint32_t flags,
                     uint32_t objectFlags,
                     int depth,
                     ChildKind &kind,
                     Vector<TypeId> &children);
  TypeId typeOfSymbolSession(int symbolSessionId);
  bool baseTypesOf(TypeId type, Vector<TypeId> &bases);
  bool isBuiltinDeep(TypeId type, std::string_view name, int depth);
  bool signatures(TypeId type, tsgo::SignatureKind kind, Vector<Signature> &out);
  void fillSignature(const tsgo::SignatureResponse &response, Signature &out);
  /** The type the `then` method's first callback receives, or 0. */
  TypeId thenValueType(TypeId type);
  TypeId awaitedDeep(TypeId type, int depth);

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
  Map<const ast::Node *, TypeId> m_contextual;
  const tsgo::JsonValue *m_compilerOptions = nullptr;
  Map<int, string> m_text;
  Map<int, TypeId> m_target;
  Map<int, TypeId> m_awaited;
  Map<int, bool> m_arrayLike;
  Map<int, bool> m_array;
  Map<int, bool> m_tuple;
  Map<int, bool> m_promiseLike;
  Map<int, bool> m_callable;
  Map<int, TypeId> m_apparent;
  Map<int, TypeId> m_constraint;
  string m_error;
  FactsStats m_stats;
};

} // namespace fastlint::types
