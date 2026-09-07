#include "fastlint/types/type_graph.h"

namespace fastlint::types {

Hasher &Hasher::bytes(const void *data, size_t size)
{
  const uint8_t *p = static_cast<const uint8_t *>(data);
  for (size_t i = 0; i < size; i++) {
    value ^= p[i];
    value *= 0x100000001b3ull;
  }
  return *this;
}

TypeGraph::TypeGraph()
{
  m_types.append(TypeRow());
  m_symbols.append(SymbolRow());
  m_strings.append(string());
}

StringId TypeGraph::internString(std::string_view text)
{
  Key key{Hasher().text(text).value};
  if (StringId *found = m_stringByHash.lookup_ptr(key)) {
    return *found;
  }
  string copy;
  for (char c : text) {
    copy += c;
  }
  StringId id = StringId(m_strings.size());
  m_strings.append(std::move(copy));
  m_stringByHash.add(key, id);
  return id;
}

std::string_view TypeGraph::text(StringId id) const
{
  if (id == 0 || id >= m_strings.size()) {
    return {};
  }
  const string &s = m_strings[int(id)];
  return std::string_view(s.c_str(), s.size());
}

SymbolId TypeGraph::internSymbol(std::string_view name,
                                 uint32_t flags,
                                 uint32_t checkFlags,
                                 span<const string> declarations,
                                 int sessionId)
{
  Hasher h;
  h.text(name).u32(flags).u32(checkFlags).u32(uint32_t(declarations.size()));
  for (const string &decl : declarations) {
    h.text(std::string_view(decl.c_str(), decl.size()));
  }
  Key key{h.value};
  SymbolId id;
  if (SymbolId *found = m_symbolByHash.lookup_ptr(key)) {
    id = *found;
  } else {
    SymbolRow row;
    row.hash = h.value;
    row.name = internString(name);
    row.flags = flags;
    row.checkFlags = checkFlags;
    row.declStart = uint32_t(m_declarations.size());
    row.declCount = uint32_t(declarations.size());
    for (const string &decl : declarations) {
      m_declarations.append(internString(std::string_view(decl.c_str(), decl.size())));
    }
    id = SymbolId(m_symbols.size());
    m_symbols.append(row);
    m_symbolByHash.add(key, id);
  }
  if (sessionId) {
    m_symbols[int(id)].sessionId = sessionId;
    m_symbolBySession.add_overwrite(sessionId, id);
  }
  return id;
}

TypeId TypeGraph::intern(TypeRow row, span<const TypeId> children)
{
  Hasher h;
  h.u32(row.flags).u32(row.objectFlags).u32(row.isTuple ? 1 : 0);
  h.u64(row.symbol ? m_symbols[int(row.symbol)].hash : 0);
  h.u64(row.aliasSymbol ? m_symbols[int(row.aliasSymbol)].hash : 0);
  h.text(text(row.text));
  h.u32(uint32_t(row.childKind)).u32(uint32_t(children.size()));
  for (TypeId child : children) {
    h.u64(m_types[int(child)].hash);
  }
  row.hash = h.value;
  Key key{h.value};
  TypeId id;
  if (TypeId *found = m_typeByHash.lookup_ptr(key)) {
    id = *found;
  } else {
    row.childStart = uint32_t(m_children.size());
    row.childCount = uint32_t(children.size());
    for (TypeId child : children) {
      m_children.append(child);
    }
    id = TypeId(m_types.size());
    m_types.append(row);
    m_typeByHash.add(key, id);
  }
  if (row.sessionId) {
    m_types[int(id)].sessionId = row.sessionId;
    m_typeBySession.add_overwrite(row.sessionId, id);
  }
  return id;
}

span<const TypeId> TypeGraph::children(TypeId id) const
{
  if (id == 0 || id >= m_types.size()) {
    return {};
  }
  const TypeRow &row = m_types[int(id)];
  const TypeId *base = const_cast<Vector<TypeId> &>(m_children).data();
  return span<const TypeId>(base + row.childStart, row.childCount);
}

span<const StringId> TypeGraph::declarations(SymbolId id) const
{
  if (id == 0 || id >= m_symbols.size()) {
    return {};
  }
  const SymbolRow &row = m_symbols[int(id)];
  const StringId *base = const_cast<Vector<StringId> &>(m_declarations).data();
  return span<const StringId>(base + row.declStart, row.declCount);
}

TypeId TypeGraph::byHash(uint64_t hash) const
{
  const TypeId *found =
      const_cast<Map<Key, TypeId> &>(m_typeByHash).lookup_ptr(Key{hash});
  return found ? *found : 0;
}

SymbolId TypeGraph::symbolByHash(uint64_t hash) const
{
  const SymbolId *found =
      const_cast<Map<Key, SymbolId> &>(m_symbolByHash).lookup_ptr(Key{hash});
  return found ? *found : 0;
}

TypeId TypeGraph::bySessionId(int sessionId) const
{
  if (!sessionId) {
    return 0;
  }
  const TypeId *found =
      const_cast<Map<int, TypeId> &>(m_typeBySession).lookup_ptr(sessionId);
  return found ? *found : 0;
}

SymbolId TypeGraph::symbolBySessionId(int sessionId) const
{
  if (!sessionId) {
    return 0;
  }
  const SymbolId *found =
      const_cast<Map<int, SymbolId> &>(m_symbolBySession).lookup_ptr(sessionId);
  return found ? *found : 0;
}

void TypeGraph::clearSessionIds()
{
  for (TypeRow &row : m_types) {
    row.sessionId = 0;
  }
  for (SymbolRow &row : m_symbols) {
    row.sessionId = 0;
  }
  m_typeBySession = Map<int, TypeId>();
  m_symbolBySession = Map<int, SymbolId>();
}

} // namespace fastlint::types
