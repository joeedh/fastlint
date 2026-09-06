#include "fastlint/types/type_facts.h"

#include "fastlint/ast/file.h"
#include "fastlint/ast/node.h"
#include "fastlint/tsgo/generated/enums.h"

#include <cstdio>

namespace fastlint::types {

namespace {

string copy(std::string_view text)
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

constexpr uint32_t kNullish =
    tsgo::TypeFlags::Undefined | tsgo::TypeFlags::Null | tsgo::TypeFlags::Void;

} // namespace

TypeFacts::TypeFacts(tsgo::Session &session, TypeGraph &graph)
    : m_session(session), m_graph(graph)
{
}

// ---------------------------------------------------------------- files and nodes

bool TypeFacts::beginFile(const ast::AstFile &file, std::string_view path, string &error)
{
  endFile();
  m_file = &file;
  m_path = copy(path);
  m_canonical = tsgo::canonicalPath(path, m_session.client().caseSensitiveFileNames());
  error = string();
  return true;
}

void TypeFacts::endFile()
{
  m_file = nullptr;
  m_path = string();
  m_canonical = string();
  m_encoded.nodes.clear();
  m_table.clear();
  m_tableReady = false;
  m_tableFailed = false;
  m_nodeTypes = Map<const ast::Node *, TypeId>();
}

bool TypeFacts::ensureTable()
{
  if (m_tableReady) {
    return true;
  }
  if (m_tableFailed || !m_file) {
    return false;
  }
  if (!m_session.sourceFile(view(m_path), m_encoded, m_error)) {
    m_tableFailed = true;
    return false;
  }
  m_table.build(*m_file, m_encoded);
  m_tableReady = true;
  return true;
}

TypeId TypeFacts::typeOf(const ast::Node *node)
{
  if (!node) {
    return 0;
  }
  if (TypeId *cached = m_nodeTypes.lookup_ptr(node)) {
    m_stats.nodeHits++;
    return *cached;
  }
  m_stats.nodeMisses++;
  if (!ensureTable()) {
    return 0;
  }
  string handle = m_table.handle(node, view(m_canonical));
  if (handle.size() == 0) {
    m_stats.unmappedNodes++;
    m_nodeTypes.add_overwrite(node, 0);
    return 0;
  }
  tsgo::TypeResponse response;
  m_stats.typeFetches++;
  if (!m_session.typeAtLocation(view(handle), response, m_error)) {
    return 0;
  }
  TypeId id = internResponse(response, true);
  m_nodeTypes.add_overwrite(node, id);
  return id;
}

bool TypeFacts::prefetch(span<const ast::Node *const> nodes)
{
  if (!ensureTable()) {
    return false;
  }
  Vector<string> handles;
  Vector<const ast::Node *> pending;
  for (const ast::Node *node : nodes) {
    if (!node || m_nodeTypes.contains(node)) {
      continue;
    }
    string handle = m_table.handle(node, view(m_canonical));
    if (handle.size() == 0) {
      m_stats.unmappedNodes++;
      m_nodeTypes.add_overwrite(node, 0);
      continue;
    }
    handles.append(std::move(handle));
    pending.append(node);
  }
  if (pending.isEmpty()) {
    return true;
  }
  Vector<tsgo::TypeResponse> responses;
  m_stats.typeFetches++;
  if (!m_session.typesAtLocations(
          span<const string>(handles.data(), handles.size()), responses, m_error))
  {
    return false;
  }
  for (int i = 0; i < int(pending.size()); i++) {
    TypeId id = i < int(responses.size()) ? internResponse(responses[i], true) : 0;
    m_nodeTypes.add_overwrite(pending[i], id);
  }
  return true;
}

// ---------------------------------------------------------------- interning

SymbolId TypeFacts::internSymbol(const tsgo::SymbolResponse &symbol)
{
  if (!symbol.present) {
    return 0;
  }
  if (SymbolId existing = m_graph.symbolBySessionId(symbol.id)) {
    return existing;
  }
  return m_graph.internSymbol(
      view(symbol.name),
      symbol.flags,
      symbol.checkFlags,
      span<const string>(const_cast<Vector<string, 1> &>(symbol.declarations).data(),
                         symbol.declarations.size()),
      symbol.id);
}

SymbolId TypeFacts::symbolOfSession(int typeSessionId, bool alias)
{
  tsgo::SymbolResponse symbol;
  m_stats.symbolFetches++;
  bool ok = alias ? m_session.aliasSymbolOfType(typeSessionId, symbol, m_error)
                  : m_session.symbolOfType(typeSessionId, symbol, m_error);
  if (!ok) {
    return 0;
  }
  return internSymbol(symbol);
}

TypeId TypeFacts::internResponse(const tsgo::TypeResponse &type, bool withChildren)
{
  if (!type.present) {
    return 0;
  }
  bool isUnion = (type.flags & tsgo::TypeFlags::Union) != 0;
  bool isIntersection = (type.flags & tsgo::TypeFlags::Intersection) != 0;
  bool isReference = (type.objectFlags & tsgo::ObjectFlags::Reference) != 0;
  bool compound = isUnion || isIntersection || isReference;
  if (TypeId existing = m_graph.bySessionId(type.id)) {
    if (!withChildren || !compound || m_graph.type(existing).childKind != ChildKind::None)
    {
      return existing;
    }
  }

  TypeRow row;
  row.flags = type.flags;
  row.objectFlags = type.objectFlags;
  row.isTuple = type.isTupleType;
  row.sessionId = type.id;
  if (type.intrinsicName.size() > 0) {
    row.text = m_graph.internString(view(type.intrinsicName));
  } else if (type.valueKind == tsgo::JsonKind::String) {
    row.text = m_graph.internString(view(type.stringValue));
  } else if (type.valueKind == tsgo::JsonKind::Number) {
    char buf[40];
    snprintf(buf, sizeof buf, "%.17g", type.numberValue);
    row.text = m_graph.internString(buf);
  }
  if (type.symbol) {
    row.symbol = m_graph.symbolBySessionId(type.symbol);
    if (!row.symbol) {
      row.symbol = symbolOfSession(type.id, false);
    }
  }
  if (type.aliasSymbol) {
    row.aliasSymbol = m_graph.symbolBySessionId(type.aliasSymbol);
    if (!row.aliasSymbol) {
      row.aliasSymbol = symbolOfSession(type.id, true);
    }
  }

  Vector<TypeId> children;
  if (withChildren && compound) {
    Vector<tsgo::TypeResponse> parts;
    m_stats.childFetches++;
    bool ok;
    if (isUnion || isIntersection) {
      ok = m_session.typesOfType(type.id, parts, m_error);
      row.childKind = isUnion ? ChildKind::UnionMembers : ChildKind::IntersectionMembers;
    } else {
      ok = m_session.typeArguments(type.id, parts, m_error);
      row.childKind = ChildKind::TypeArguments;
    }
    if (ok) {
      for (const tsgo::TypeResponse &part : parts) {
        children.append(internResponse(part, false));
      }
    } else {
      row.childKind = ChildKind::None;
    }
  }
  return m_graph.intern(row, span<const TypeId>(children.data(), children.size()));
}

// ---------------------------------------------------------------- questions

bool TypeFacts::isAnyLike(TypeId type) const
{
  if (!type) {
    return false;
  }
  return (m_graph.type(type).flags & (tsgo::TypeFlags::Any | tsgo::TypeFlags::Unknown)) !=
         0;
}

bool TypeFacts::isNullable(TypeId type) const
{
  if (!type) {
    return false;
  }
  const TypeRow &row = m_graph.type(type);
  if (row.flags & kNullish) {
    return true;
  }
  if (row.childKind == ChildKind::UnionMembers) {
    for (TypeId member : m_graph.children(type)) {
      if (m_graph.type(member).flags & kNullish) {
        return true;
      }
    }
  }
  return false;
}

bool TypeFacts::nameIs(SymbolId symbol, std::string_view a, std::string_view b) const
{
  if (!symbol) {
    return false;
  }
  std::string_view name = m_graph.text(m_graph.symbol(symbol).name);
  return name == a || name == b;
}

bool TypeFacts::thenable(TypeId type)
{
  const TypeRow &row = m_graph.type(type);
  if (!(row.flags & tsgo::TypeFlags::Object) || !row.sessionId) {
    return false;
  }
  Vector<tsgo::SymbolResponse> properties;
  if (!m_session.propertiesOfType(row.sessionId, properties, m_error)) {
    return false;
  }
  for (const tsgo::SymbolResponse &property : properties) {
    if (view(property.name) != "then") {
      continue;
    }
    tsgo::TypeResponse thenType;
    if (!m_session.typeOfSymbol(property.id, thenType, m_error) || !thenType.present) {
      return false;
    }
    Vector<tsgo::SignatureResponse> signatures;
    if (!m_session.signaturesOfType(
            thenType.id, tsgo::SignatureKind::Call, signatures, m_error))
    {
      return false;
    }
    return !signatures.isEmpty();
  }
  return false;
}

bool TypeFacts::isPromiseLike(TypeId type)
{
  if (!type) {
    return false;
  }
  if (bool *cached = m_promiseLike.lookup_ptr(int(type))) {
    return *cached;
  }
  const TypeRow &row = m_graph.type(type);
  bool result = false;
  if (nameIs(row.symbol, "Promise", "PromiseLike") ||
      nameIs(row.aliasSymbol, "Promise", "PromiseLike"))
  {
    result = true;
  } else if (row.childKind == ChildKind::UnionMembers) {
    for (TypeId member : m_graph.children(type)) {
      if (isPromiseLike(member)) {
        result = true;
        break;
      }
    }
  } else {
    result = thenable(type);
  }
  m_promiseLike.add_overwrite(int(type), result);
  return result;
}

bool TypeFacts::isArrayLike(TypeId type)
{
  if (!type) {
    return false;
  }
  if (bool *cached = m_arrayLike.lookup_ptr(int(type))) {
    return *cached;
  }
  const TypeRow &row = m_graph.type(type);
  bool result = false;
  if (row.sessionId) {
    m_session.isArrayLikeType(row.sessionId, result, m_error);
  }
  m_arrayLike.add_overwrite(int(type), result);
  return result;
}

span<const TypeId> TypeFacts::unionMembers(TypeId type) const
{
  if (!type) {
    return {};
  }
  ChildKind kind = m_graph.type(type).childKind;
  if (kind != ChildKind::UnionMembers && kind != ChildKind::IntersectionMembers) {
    return {};
  }
  return m_graph.children(type);
}

bool TypeFacts::callSignatures(TypeId type, Vector<Signature> &signatures)
{
  signatures.clear();
  if (!type) {
    return false;
  }
  const TypeRow &row = m_graph.type(type);
  if (!row.sessionId) {
    m_error = string("type has no live session id");
    return false;
  }
  Vector<tsgo::SignatureResponse> responses;
  if (!m_session.signaturesOfType(
          row.sessionId, tsgo::SignatureKind::Call, responses, m_error))
  {
    return false;
  }
  for (const tsgo::SignatureResponse &response : responses) {
    Signature signature;
    signature.sessionId = response.id;
    tsgo::TypeResponse returned;
    if (m_session.returnTypeOfSignature(response.id, returned, m_error)) {
      signature.returnType = internResponse(returned, true);
    }
    Vector<tsgo::SymbolResponse> parameters;
    if (m_session.parametersOfSignature(response.id, parameters, m_error)) {
      for (const tsgo::SymbolResponse &parameter : parameters) {
        signature.parameters.append(internSymbol(parameter));
      }
    }
    signatures.append(std::move(signature));
  }
  return true;
}

bool TypeFacts::assignableTo(TypeId from, TypeId to, bool &result)
{
  result = false;
  if (!from || !to) {
    return false;
  }
  int fromId = m_graph.type(from).sessionId;
  int toId = m_graph.type(to).sessionId;
  if (!fromId || !toId) {
    m_error = string("type has no live session id");
    return false;
  }
  return m_session.isTypeAssignableTo(fromId, toId, result, m_error);
}

SymbolId TypeFacts::symbolOf(TypeId type) const
{
  return type ? m_graph.type(type).symbol : 0;
}

span<const StringId> TypeFacts::declarationsOf(SymbolId symbol) const
{
  return m_graph.declarations(symbol);
}

std::string_view TypeFacts::declarationFile(std::string_view handle)
{
  size_t first = handle.find('.');
  if (first == std::string_view::npos) {
    return {};
  }
  size_t second = handle.find('.', first + 1);
  if (second == std::string_view::npos) {
    return {};
  }
  return handle.substr(second + 1);
}

} // namespace fastlint::types
