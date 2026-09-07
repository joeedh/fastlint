#pragma once

#include "util/map.h"
#include "util/span.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

namespace fastlint::types {

using litestl::util::Map;
using litestl::util::span;
using litestl::util::Vector;
using string = litestl::util::string;

/** Row indices into a `TypeGraph`; 0 is "none" for each. */
using TypeId = uint32_t;
using SymbolId = uint32_t;
using StringId = uint32_t;

/** What a type row's children range holds. */
enum class ChildKind : uint8_t { None, UnionMembers, IntersectionMembers, TypeArguments };

/** One interned type: its own flags plus one hop of structure. Deeper levels are separate
 * rows reached through `children`. */
struct TypeRow {
  /** Identity over every field below except `sessionId`; the interning key. */
  uint64_t hash = 0;
  uint32_t flags = 0;
  uint32_t objectFlags = 0;
  bool isTuple = false;
  SymbolId symbol = 0;
  SymbolId aliasSymbol = 0;
  /** Intrinsic name or literal value text; 0 when the type has neither. */
  StringId text = 0;
  /** Generic target of a type reference, else 0. */
  TypeId target = 0;
  ChildKind childKind = ChildKind::None;
  uint32_t childStart = 0;
  uint32_t childCount = 0;
  /** tsgo id in the live snapshot; 0 once the snapshot is released. */
  int sessionId = 0;
};

struct SymbolRow {
  uint64_t hash = 0;
  StringId name = 0;
  uint32_t flags = 0;
  uint32_t checkFlags = 0;
  /** Declaration node handles, as string ids. */
  uint32_t declStart = 0;
  uint32_t declCount = 0;
  int sessionId = 0;
};

/** 64-bit FNV-1a, the hash every interning key is built from. */
struct Hasher {
  uint64_t value = 0xcbf29ce484222325ull;

  Hasher &bytes(const void *data, size_t size);
  Hasher &u32(uint32_t v)
  {
    return bytes(&v, sizeof v);
  }
  Hasher &u64(uint64_t v)
  {
    return bytes(&v, sizeof v);
  }
  Hasher &text(std::string_view s)
  {
    u32(uint32_t(s.size()));
    return bytes(s.data(), s.size());
  }
};

/** Types, symbols and strings interned by structural hash. Rows are appended and never
 * removed; ids stay valid for the graph's lifetime. */
class TypeGraph {
public:
  TypeGraph();

  /** Adds `row` with `children` unless an identical row exists; `row.hash` is computed
   * here and `row.sessionId` is recorded on a new row or refreshed on an existing one. */
  TypeId intern(TypeRow row, span<const TypeId> children);
  SymbolId internSymbol(std::string_view name,
                        uint32_t flags,
                        uint32_t checkFlags,
                        span<const string> declarations,
                        int sessionId);
  StringId internString(std::string_view text);

  const TypeRow &type(TypeId id) const
  {
    return m_types[int(id)];
  }
  const SymbolRow &symbol(SymbolId id) const
  {
    return m_symbols[int(id)];
  }
  std::string_view text(StringId id) const;
  span<const TypeId> children(TypeId id) const;
  span<const StringId> declarations(SymbolId id) const;

  /** Row with this structural hash, or 0. */
  TypeId byHash(uint64_t hash) const;
  SymbolId symbolByHash(uint64_t hash) const;
  /** Row whose live tsgo id is `sessionId`, or 0. */
  TypeId bySessionId(int sessionId) const;
  SymbolId symbolBySessionId(int sessionId) const;
  /** Drops every live tsgo id; call when the snapshot that minted them is released. */
  void clearSessionIds();

  /** Counts exclude the reserved 0 row. */
  size_t typeCount() const
  {
    return m_types.size() - 1;
  }
  size_t symbolCount() const
  {
    return m_symbols.size() - 1;
  }
  size_t stringCount() const
  {
    return m_strings.size() - 1;
  }

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

  Vector<TypeRow> m_types;
  Vector<SymbolRow> m_symbols;
  Vector<string> m_strings;
  Vector<TypeId> m_children;
  Vector<StringId> m_declarations;
  Map<Key, TypeId> m_typeByHash;
  Map<Key, SymbolId> m_symbolByHash;
  Map<Key, StringId> m_stringByHash;
  Map<int, TypeId> m_typeBySession;
  Map<int, SymbolId> m_symbolBySession;
};

} // namespace fastlint::types
