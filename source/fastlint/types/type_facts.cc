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

/** How many levels of members and type arguments a row is interned with. A generic
 * instantiation is only told apart by its arguments, so leaves below this depth may
 * share a row. */
constexpr int kChildDepth = 8;

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
  m_contextual = Map<const ast::Node *, TypeId>();
  m_error = string();
}

bool Signature::hasRest() const
{
  return (flags & tsgo::SignatureFlags::HasRestParameter) != 0;
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

TypeId TypeFacts::contextualTypeOf(const ast::Node *node)
{
  if (!node) {
    return 0;
  }
  if (TypeId *cached = m_contextual.lookup_ptr(node)) {
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
    m_contextual.add_overwrite(node, 0);
    return 0;
  }
  tsgo::TypeResponse response;
  m_stats.typeFetches++;
  if (!m_session.contextualType(view(handle), response, m_error)) {
    return 0;
  }
  TypeId id = internResponse(response, true);
  m_contextual.add_overwrite(node, id);
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

TypeId
TypeFacts::internResponse(const tsgo::TypeResponse &type, bool withChildren, int depth)
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
    fetchChildren(type.id, type.flags, type.objectFlags, depth, row.childKind, children);
  }
  return m_graph.intern(row, span<const TypeId>(children.data(), children.size()));
}

bool TypeFacts::fetchChildren(int sessionId,
                              uint32_t flags,
                              uint32_t objectFlags,
                              int depth,
                              ChildKind &kind,
                              Vector<TypeId> &children)
{
  bool isUnion = (flags & tsgo::TypeFlags::Union) != 0;
  bool isIntersection = (flags & tsgo::TypeFlags::Intersection) != 0;
  bool isReference = (objectFlags & tsgo::ObjectFlags::Reference) != 0;
  kind = ChildKind::None;
  children.clear();
  if (!sessionId || !(isUnion || isIntersection || isReference)) {
    return false;
  }
  Vector<tsgo::TypeResponse> parts;
  m_stats.childFetches++;
  bool ok;
  if (isUnion || isIntersection) {
    ok = m_session.typesOfType(sessionId, parts, m_error);
    kind = isUnion ? ChildKind::UnionMembers : ChildKind::IntersectionMembers;
  } else {
    ok = m_session.typeArguments(sessionId, parts, m_error);
    kind = ChildKind::TypeArguments;
  }
  if (!ok) {
    kind = ChildKind::None;
    return false;
  }
  for (const tsgo::TypeResponse &part : parts) {
    children.append(internResponse(part, depth < kChildDepth, depth + 1));
  }
  return true;
}

TypeId TypeFacts::deepen(TypeId type)
{
  if (!type) {
    return 0;
  }
  TypeRow row = m_graph.type(type);
  bool compound =
      (row.flags & (tsgo::TypeFlags::Union | tsgo::TypeFlags::Intersection)) ||
      (row.objectFlags & tsgo::ObjectFlags::Reference);
  if (!compound || row.childKind != ChildKind::None || !row.sessionId) {
    return type;
  }
  Vector<TypeId> children;
  if (!fetchChildren(
          row.sessionId, row.flags, row.objectFlags, 0, row.childKind, children))
  {
    return type;
  }
  row.hash = 0;
  row.childStart = row.childCount = 0;
  return m_graph.intern(row, span<const TypeId>(children.data(), children.size()));
}

TypeId TypeFacts::typeOfSymbolSession(int symbolSessionId)
{
  if (!symbolSessionId) {
    return 0;
  }
  tsgo::TypeResponse response;
  m_stats.typeFetches++;
  if (!m_session.typeOfSymbol(symbolSessionId, response, m_error)) {
    return 0;
  }
  return internResponse(response, true);
}

// ---------------------------------------------------------------- questions

uint32_t TypeFacts::flags(TypeId type) const
{
  return type ? m_graph.type(type).flags : 0;
}

std::string_view TypeFacts::literalText(TypeId type) const
{
  if (!type) {
    return {};
  }
  return m_graph.text(m_graph.type(type).text);
}

uint32_t TypeFacts::objectFlags(TypeId type) const
{
  return type ? m_graph.type(type).objectFlags : 0;
}

bool TypeFacts::isAnyLike(TypeId type) const
{
  return (flags(type) & (tsgo::TypeFlags::Any | tsgo::TypeFlags::Unknown)) != 0;
}

bool TypeFacts::isAny(TypeId type) const
{
  return (flags(type) & tsgo::TypeFlags::Any) != 0;
}

bool TypeFacts::isUnknown(TypeId type) const
{
  return (flags(type) & tsgo::TypeFlags::Unknown) != 0;
}

bool TypeFacts::isErrorType(TypeId type) const
{
  return isAny(type) && m_graph.text(m_graph.type(type).text) == "error";
}

string TypeFacts::typeToString(TypeId type)
{
  if (!type) {
    return string();
  }
  if (string *cached = m_text.lookup_ptr(int(type))) {
    return *cached;
  }
  string text;
  const TypeRow &row = m_graph.type(type);
  if (row.sessionId) {
    m_stats.typeFetches++;
    m_session.typeToString(row.sessionId, text, m_error);
  } else if (row.text) {
    text = copy(m_graph.text(row.text));
  }
  m_text.add_overwrite(int(type), text);
  return text;
}

bool TypeFacts::strictOption(std::string_view name) const
{
  if (!m_compilerOptions) {
    return true;
  }
  // A `Tristate` reaches us as a JSON boolean, or `null`/absent when unset; an unset
  // strict-family option follows `strict`, which defaults to off.
  const tsgo::JsonValue *own = m_compilerOptions->get(name);
  if (own && own->kind == tsgo::JsonKind::Bool) {
    return own->asBool();
  }
  const tsgo::JsonValue *strict = m_compilerOptions->get("strict");
  return strict && strict->kind == tsgo::JsonKind::Bool && strict->asBool();
}

TypeId TypeFacts::targetOf(TypeId type)
{
  if (!type) {
    return 0;
  }
  const TypeRow &row = m_graph.type(type);
  // `Target` panics in the server on anything but an object type, and the
  // object flags of other kinds mean other things.
  if (!(row.flags & tsgo::TypeFlags::Object) ||
      !(row.objectFlags & tsgo::ObjectFlags::Reference) || !row.sessionId)
  {
    return 0;
  }
  if (TypeId *cached = m_target.lookup_ptr(int(type))) {
    return *cached;
  }
  tsgo::TypeResponse target;
  m_stats.typeFetches++;
  TypeId result = 0;
  if (m_session.targetOfType(row.sessionId, target, m_error) && target.present) {
    result = internResponse(target, false);
  }
  m_target.add_overwrite(int(type), result);
  return result;
}

bool TypeFacts::isTypeParameter(TypeId type) const
{
  return type && (m_graph.type(type).flags & tsgo::TypeFlags::TypeParameter) != 0;
}

bool TypeFacts::isTuple(TypeId type)
{
  if (!type) {
    return false;
  }
  const TypeRow &row = m_graph.type(type);
  if (row.isTuple) {
    return true;
  }
  if (!(row.flags & tsgo::TypeFlags::Object) ||
      !(row.objectFlags & tsgo::ObjectFlags::Reference) || !row.sessionId)
  {
    return false;
  }
  if (bool *cached = m_tuple.lookup_ptr(int(type))) {
    return *cached;
  }
  tsgo::TypeResponse target;
  m_stats.typeFetches++;
  bool result = m_session.targetOfType(row.sessionId, target, m_error) &&
                target.present && (target.objectFlags & tsgo::ObjectFlags::Tuple) != 0;
  m_tuple.add_overwrite(int(type), result);
  return result;
}

uint32_t TypeFacts::symbolFlags(SymbolId symbol) const
{
  return symbol ? m_graph.symbol(symbol).flags : 0;
}

bool TypeFacts::isDefaultLibrary(SymbolId symbol) const
{
  for (StringId handle : declarationsOf(symbol)) {
    std::string_view file = declarationFile(m_graph.text(handle));
    size_t slash = file.find_last_of('/');
    std::string_view name =
        slash == std::string_view::npos ? file : file.substr(slash + 1);
    if (name.size() > 9 && name.substr(0, 4) == "lib." &&
        name.substr(name.size() - 5) == ".d.ts")
    {
      return true;
    }
  }
  return false;
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
  // Copy the fields and members out before the queries below: `isPromiseLike`
  // and `thenable` query the type server, which can reallocate the graph's row
  // and child storage and dangle a held `TypeRow &` or a `children` span.
  const SymbolId symbol = m_graph.type(type).symbol;
  const SymbolId aliasSymbol = m_graph.type(type).aliasSymbol;
  const ChildKind childKind = m_graph.type(type).childKind;
  bool result = false;
  if (nameIs(symbol, "Promise", "PromiseLike") ||
      nameIs(aliasSymbol, "Promise", "PromiseLike"))
  {
    result = true;
  } else if (childKind == ChildKind::UnionMembers) {
    Vector<TypeId, 4> members;
    for (TypeId member : m_graph.children(type)) {
      members.append(member);
    }
    for (TypeId member : members) {
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

bool TypeFacts::isArray(TypeId type)
{
  if (!type) {
    return false;
  }
  if (bool *cached = m_array.lookup_ptr(int(type))) {
    return *cached;
  }
  const TypeRow &row = m_graph.type(type);
  bool result = false;
  if (row.sessionId) {
    m_session.isArrayType(row.sessionId, result, m_error);
  }
  m_array.add_overwrite(int(type), result);
  return result;
}

span<const TypeId> TypeFacts::typeArguments(TypeId type)
{
  type = deepen(type);
  if (!type || m_graph.type(type).childKind != ChildKind::TypeArguments) {
    return {};
  }
  return m_graph.children(type);
}

TypeId TypeFacts::apparentType(TypeId type)
{
  if (!type) {
    return 0;
  }
  if (TypeId *cached = m_apparent.lookup_ptr(int(type))) {
    return *cached;
  }
  const TypeRow &row = m_graph.type(type);
  TypeId result = type;
  // Only primitives and type parameters have an apparent type other than themselves.
  constexpr uint32_t kSelf =
      tsgo::TypeFlags::Object | tsgo::TypeFlags::Union | tsgo::TypeFlags::Intersection |
      tsgo::TypeFlags::Any | tsgo::TypeFlags::Unknown | tsgo::TypeFlags::Never |
      tsgo::TypeFlags::Void | tsgo::TypeFlags::Undefined | tsgo::TypeFlags::Null;
  if (row.sessionId && !(row.flags & kSelf)) {
    tsgo::TypeResponse response;
    m_stats.typeFetches++;
    if (m_session.apparentType(row.sessionId, response, m_error) && response.present) {
      result = internResponse(response, true);
    }
  }
  m_apparent.add_overwrite(int(type), result);
  return result;
}

TypeId TypeFacts::constraintOf(TypeId type)
{
  if (!isTypeParameter(type)) {
    return 0;
  }
  if (TypeId *cached = m_constraint.lookup_ptr(int(type))) {
    return *cached;
  }
  const TypeRow &row = m_graph.type(type);
  TypeId result = 0;
  if (row.sessionId) {
    tsgo::TypeResponse response;
    m_stats.typeFetches++;
    if (m_session.baseConstraintOfType(row.sessionId, response, m_error) &&
        response.present)
    {
      result = internResponse(response, true);
    }
  }
  m_constraint.add_overwrite(int(type), result);
  return result;
}

TypeId TypeFacts::numberIndexType(TypeId type)
{
  if (!type) {
    return 0;
  }
  const TypeRow &row = m_graph.type(type);
  if (!row.sessionId || !(row.flags & tsgo::TypeFlags::Object)) {
    return 0;
  }
  Vector<tsgo::IndexInfoResponse> infos;
  m_stats.typeFetches++;
  if (!m_session.indexInfosOfType(row.sessionId, infos, m_error)) {
    return 0;
  }
  for (const tsgo::IndexInfoResponse &info : infos) {
    if (info.keyType.present && (info.keyType.flags & tsgo::TypeFlags::Number)) {
      return internResponse(info.valueType, true);
    }
  }
  return 0;
}

TypeId TypeFacts::propertyType(TypeId type, std::string_view name)
{
  if (!type) {
    return 0;
  }
  const TypeRow &row = m_graph.type(type);
  if (!row.sessionId || !(row.flags & (tsgo::TypeFlags::Object | tsgo::TypeFlags::Union |
                                       tsgo::TypeFlags::Intersection)))
  {
    return 0;
  }
  tsgo::SymbolResponse property;
  m_stats.symbolFetches++;
  if (!m_session.propertyOfType(row.sessionId, name, property, m_error) ||
      !property.present)
  {
    return 0;
  }
  return typeOfSymbolSession(property.id);
}

bool TypeFacts::hasWellKnownSymbolProperty(TypeId type, std::string_view name)
{
  if (!type) {
    return false;
  }
  const TypeRow &row = m_graph.type(type);
  if (!row.sessionId || !(row.flags & tsgo::TypeFlags::Object)) {
    return false;
  }
  Vector<tsgo::SymbolResponse> properties;
  m_stats.symbolFetches++;
  if (!m_session.propertiesOfType(row.sessionId, properties, m_error)) {
    return false;
  }
  // The checker names a `[Symbol.name]` member `__@name@<id>`.
  for (const tsgo::SymbolResponse &property : properties) {
    std::string_view text = view(property.name);
    if (text.size() > name.size() + 4 && text.substr(0, 3) == "__@" &&
        text.substr(3, name.size()) == name && text[3 + name.size()] == '@')
    {
      return true;
    }
  }
  return false;
}

bool TypeFacts::isCallable(TypeId type)
{
  if (!type) {
    return false;
  }
  if (bool *cached = m_callable.lookup_ptr(int(type))) {
    return *cached;
  }
  bool result = false;
  TypeId apparent = apparentType(type);
  span<const TypeId> members = unionMembers(apparent);
  Vector<TypeId, 4> parts;
  if (members.size() > 0) {
    for (TypeId member : members) {
      parts.append(member);
    }
  } else {
    parts.append(apparent);
  }
  for (TypeId part : parts) {
    const TypeRow &row = m_graph.type(part);
    if (!row.sessionId) {
      continue;
    }
    Vector<tsgo::SignatureResponse> signatures;
    if (m_session.signaturesOfType(
            row.sessionId, tsgo::SignatureKind::Call, signatures, m_error) &&
        !signatures.isEmpty())
    {
      result = true;
      break;
    }
  }
  m_callable.add_overwrite(int(type), result);
  return result;
}

bool TypeFacts::isThenable(TypeId type, int callbacks)
{
  TypeId apparent = apparentType(type);
  span<const TypeId> members = unionMembers(apparent);
  Vector<TypeId, 4> parts;
  if (members.size() > 0) {
    for (TypeId member : members) {
      parts.append(member);
    }
  } else {
    parts.append(apparent);
  }
  for (TypeId part : parts) {
    TypeId then = propertyType(part, "then");
    if (!then) {
      continue;
    }
    span<const TypeId> thenMembers = unionMembers(then);
    Vector<TypeId, 4> thenParts;
    if (thenMembers.size() > 0) {
      for (TypeId member : thenMembers) {
        thenParts.append(member);
      }
    } else {
      thenParts.append(then);
    }
    for (TypeId thenPart : thenParts) {
      Vector<Signature> signatures;
      if (!callSignatures(thenPart, signatures)) {
        continue;
      }
      for (const Signature &signature : signatures) {
        if (int(signature.parameters.size()) < callbacks) {
          continue;
        }
        bool ok = true;
        for (int i = 0; i < callbacks && ok; i++) {
          ok = isCallable(typeOfSymbol(signature.parameters[i]));
        }
        if (ok) {
          return true;
        }
      }
    }
  }
  return false;
}

bool TypeFacts::baseTypesOf(TypeId type, Vector<TypeId> &bases)
{
  bases.clear();
  SymbolId symbol = symbolOf(type);
  if (!symbol) {
    return false;
  }
  const SymbolRow &row = m_graph.symbol(symbol);
  if (!row.sessionId ||
      !(row.flags & (tsgo::SymbolFlags::Class | tsgo::SymbolFlags::Interface)))
  {
    return false;
  }
  tsgo::TypeResponse declared;
  m_stats.typeFetches++;
  if (!m_session.declaredTypeOfSymbol(row.sessionId, declared, m_error) ||
      !declared.present)
  {
    return false;
  }
  Vector<tsgo::TypeResponse> responses;
  m_stats.typeFetches++;
  if (!m_session.baseTypes(declared.id, responses, m_error)) {
    return false;
  }
  for (const tsgo::TypeResponse &response : responses) {
    bases.append(internResponse(response, false));
  }
  return true;
}

bool TypeFacts::isBuiltin(TypeId type, std::string_view name)
{
  return isBuiltinDeep(type, name, 0);
}

bool TypeFacts::isBuiltinDeep(TypeId type, std::string_view name, int depth)
{
  if (!type || depth > 8) {
    return false;
  }
  type = deepen(type);
  // Copy the fields and members out before recursing: `isBuiltinDeep` queries
  // the type server, which can reallocate the graph's row and child storage and
  // dangle a held `TypeRow &` or a `children` span.
  const uint32_t flags = m_graph.type(type).flags;
  const ChildKind childKind = m_graph.type(type).childKind;
  const SymbolId symbol = m_graph.type(type).symbol;
  if (childKind == ChildKind::IntersectionMembers || childKind == ChildKind::UnionMembers)
  {
    Vector<TypeId, 4> members;
    for (TypeId member : m_graph.children(type)) {
      members.append(member);
    }
    if (childKind == ChildKind::IntersectionMembers) {
      for (TypeId member : members) {
        if (isBuiltinDeep(member, name, depth + 1)) {
          return true;
        }
      }
      return false;
    }
    for (TypeId member : members) {
      if (!isBuiltinDeep(member, name, depth + 1)) {
        return false;
      }
    }
    return !members.isEmpty();
  }
  if (flags & tsgo::TypeFlags::TypeParameter) {
    TypeId constraint = constraintOf(type);
    return constraint && isBuiltinDeep(constraint, name, depth + 1);
  }
  if (symbol && m_graph.text(m_graph.symbol(symbol).name) == name &&
      isDefaultLibrary(symbol))
  {
    return true;
  }
  Vector<TypeId> bases;
  if (baseTypesOf(type, bases)) {
    for (TypeId base : bases) {
      if (isBuiltinDeep(base, name, depth + 1)) {
        return true;
      }
    }
  }
  return false;
}

span<const TypeId> TypeFacts::unionMembers(TypeId type)
{
  type = deepen(type);
  if (!type) {
    return {};
  }
  ChildKind kind = m_graph.type(type).childKind;
  if (kind != ChildKind::UnionMembers && kind != ChildKind::IntersectionMembers) {
    return {};
  }
  return m_graph.children(type);
}

void TypeFacts::fillSignature(const tsgo::SignatureResponse &response, Signature &out)
{
  out.sessionId = response.id;
  out.flags = response.flags;
  tsgo::TypeResponse returned;
  m_stats.typeFetches++;
  if (m_session.returnTypeOfSignature(response.id, returned, m_error)) {
    out.returnType = internResponse(returned, true);
  }
  Vector<tsgo::SymbolResponse> parameters;
  m_stats.symbolFetches++;
  if (m_session.parametersOfSignature(response.id, parameters, m_error)) {
    for (const tsgo::SymbolResponse &parameter : parameters) {
      out.parameters.append(internSymbol(parameter));
    }
  }
}

bool TypeFacts::signatures(TypeId type, tsgo::SignatureKind kind, Vector<Signature> &out)
{
  out.clear();
  if (!type) {
    return false;
  }
  // A union or intersection carries a call signature only through its members, so gather
  // signatures from each apparent member the way `getCallSignaturesOfType` does.
  TypeId apparent = apparentType(type);
  span<const TypeId> members = unionMembers(apparent);
  Vector<TypeId, 4> parts;
  if (members.size() > 0) {
    for (TypeId member : members) {
      parts.append(member);
    }
  } else {
    parts.append(apparent);
  }
  bool found = false;
  for (TypeId part : parts) {
    const TypeRow &row = m_graph.type(part);
    if (!row.sessionId) {
      continue;
    }
    Vector<tsgo::SignatureResponse> responses;
    if (!m_session.signaturesOfType(row.sessionId, kind, responses, m_error)) {
      continue;
    }
    for (const tsgo::SignatureResponse &response : responses) {
      Signature signature;
      fillSignature(response, signature);
      out.append(std::move(signature));
      found = true;
    }
  }
  return found;
}

bool TypeFacts::callSignatures(TypeId type, Vector<Signature> &signatures)
{
  return this->signatures(type, tsgo::SignatureKind::Call, signatures);
}

bool TypeFacts::constructSignatures(TypeId type, Vector<Signature> &signatures)
{
  return this->signatures(type, tsgo::SignatureKind::Construct, signatures);
}

bool TypeFacts::resolvedSignature(const ast::Node *call, Signature &signature)
{
  signature = Signature();
  if (!call || !ensureTable()) {
    return false;
  }
  string handle = m_table.handle(call, view(m_canonical));
  if (handle.size() == 0) {
    m_stats.unmappedNodes++;
    return false;
  }
  tsgo::SignatureResponse response;
  if (!m_session.resolvedSignature(view(handle), response, m_error) || !response.present)
  {
    return false;
  }
  fillSignature(response, signature);
  return true;
}

TypeId TypeFacts::thenValueType(TypeId type)
{
  TypeId then = propertyType(type, "then");
  Vector<Signature> thenSignatures;
  if (!then || !callSignatures(then, thenSignatures)) {
    return 0;
  }
  for (const Signature &signature : thenSignatures) {
    if (signature.parameters.isEmpty()) {
      continue;
    }
    // `onfulfilled?: ((value: T) => ...) | null | undefined`
    TypeId callback = typeOfSymbol(signature.parameters[0]);
    Vector<TypeId, 4> parts;
    span<const TypeId> members = unionMembers(callback);
    if (members.size() > 0) {
      for (TypeId member : members) {
        parts.append(member);
      }
    } else {
      parts.append(callback);
    }
    for (TypeId part : parts) {
      Vector<Signature> callbackSignatures;
      if (!callSignatures(part, callbackSignatures)) {
        continue;
      }
      for (const Signature &cb : callbackSignatures) {
        if (!cb.parameters.isEmpty()) {
          return typeOfSymbol(cb.parameters[0]);
        }
      }
    }
  }
  return 0;
}

TypeId TypeFacts::awaitedDeep(TypeId type, int depth)
{
  // `Promise<Promise<...>>` nests a bounded number of times in practice.
  if (!type || depth > 8) {
    return 0;
  }
  // The type's flags and symbol are copied out before any type-server query
  // below: a query appends rows to the graph and can reallocate its storage,
  // which would dangle a `TypeRow &` held across the call.
  const uint32_t flags = m_graph.type(type).flags;
  const SymbolId symbol = m_graph.type(type).symbol;
  if (flags & (tsgo::TypeFlags::Any | tsgo::TypeFlags::Unknown)) {
    return type;
  }
  if (flags & tsgo::TypeFlags::Union) {
    // Copy the members out before recursing: `awaitedDeep` queries the type
    // server, which can reallocate the graph's child storage and dangle the
    // span `unionMembers` returns into it.
    Vector<TypeId, 4> members;
    for (TypeId member : unionMembers(type)) {
      members.append(member);
    }
    TypeId same = 0;
    for (TypeId member : members) {
      TypeId awaited = awaitedDeep(member, depth + 1);
      if (!awaited || (same && awaited != same)) {
        return 0;
      }
      same = awaited;
    }
    return same ? same : type;
  }
  if (!isThenable(type, 1)) {
    return type;
  }
  // The default library's `Promise<T>` resolves to `T`; other thenables are read through
  // the callback `then` hands their value to.
  TypeId value = 0;
  if (nameIs(symbol, "Promise", "PromiseLike") && isDefaultLibrary(symbol)) {
    span<const TypeId> args = typeArguments(type);
    value = args.size() > 0 ? args[0] : 0;
  }
  if (!value) {
    TypeId apparent = apparentType(type);
    span<const TypeId> members = unionMembers(apparent);
    if (members.size() > 0) {
      for (TypeId member : members) {
        value = thenValueType(member);
        if (value) {
          break;
        }
      }
    } else {
      value = thenValueType(apparent);
    }
  }
  return value ? awaitedDeep(value, depth + 1) : 0;
}

TypeId TypeFacts::awaitedType(TypeId type)
{
  if (!type) {
    return 0;
  }
  if (TypeId *cached = m_awaited.lookup_ptr(int(type))) {
    return *cached;
  }
  TypeId result = awaitedDeep(type, 0);
  m_awaited.add_overwrite(int(type), result);
  return result;
}

TypeId TypeFacts::typeOfSymbol(SymbolId symbol)
{
  if (!symbol) {
    return 0;
  }
  return typeOfSymbolSession(m_graph.symbol(symbol).sessionId);
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
