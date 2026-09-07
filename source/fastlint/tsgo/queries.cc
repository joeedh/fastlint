#include "fastlint/tsgo/queries.h"

#include "fastlint/tsgo/generated/compat.h"

namespace fastlint::tsgo {

namespace {

string toString(std::string_view text)
{
  string out;
  for (char c : text) {
    out += c;
  }
  return out;
}

void readInts(const JsonValue *list, Vector<int, 2> &out)
{
  for (int i = 0; list && i < list->size(); i++) {
    out.append(list->at(i)->asInt());
  }
}

void readStrings(const JsonValue *list, Vector<string, 1> &out)
{
  for (int i = 0; list && i < list->size(); i++) {
    out.append(toString(list->at(i)->asString()));
  }
}

} // namespace

const char *typeIdParam(std::string_view method)
{
  for (const compat::TypeIdParam &entry : compat::kTypeIdParams) {
    if (method == entry.method) {
      return entry.param;
    }
  }
  return "type";
}

// ---------------------------------------------------------------- responses

TypeResponse TypeResponse::from(const JsonValue *v)
{
  TypeResponse t;
  if (!v || !v->isObject()) {
    return t;
  }
  t.present = true;
  t.id = v->getInt("id");
  t.flags = v->getUint("flags");
  t.objectFlags = v->getUint("objectFlags");
  t.isTupleType = v->getBool("isTupleType");
  t.target = v->getInt("target");
  t.symbol = v->getInt("symbol");
  t.aliasSymbol = v->getInt("aliasSymbol");
  t.freshType = v->getInt("freshType");
  t.regularType = v->getInt("regularType");
  t.objectType = v->getInt("objectType");
  t.indexType = v->getInt("indexType");
  t.checkType = v->getInt("checkType");
  t.extendsType = v->getInt("extendsType");
  t.baseType = v->getInt("baseType");
  t.fixedLength = v->getInt("fixedLength");
  t.intrinsicName = toString(v->getString("intrinsicName"));
  if (const JsonValue *value = v->get("value")) {
    if (value->isNumber()) {
      t.valueKind = JsonKind::Number;
      t.numberValue = value->asDouble();
    } else if (value->isString()) {
      t.valueKind = JsonKind::String;
      t.stringValue = toString(value->asString());
    } else if (value->kind == JsonKind::Bool) {
      // A boolean literal (`true`/`false`) arrives as a JSON boolean.
      t.valueKind = JsonKind::String;
      t.stringValue = value->asBool() ? "true" : "false";
    } else if (value->isObject()) {
      // A bigint literal arrives as {base10Value, negative}.
      t.valueKind = JsonKind::String;
      if (value->getBool("negative")) {
        t.stringValue += '-';
      }
      t.stringValue += toString(value->getString("base10Value"));
    }
  }
  readInts(v->get("typeParameters"), t.typeParameters);
  readInts(v->get("aliasTypeArguments"), t.aliasTypeArguments);
  if (const JsonValue *list = v->get("elementFlags")) {
    for (int i = 0; i < list->size(); i++) {
      t.elementFlags.append(list->at(i)->asUint());
    }
  }
  readStrings(v->get("texts"), t.texts);
  return t;
}

SymbolResponse SymbolResponse::from(const JsonValue *v)
{
  SymbolResponse s;
  if (!v || !v->isObject()) {
    return s;
  }
  s.present = true;
  s.id = v->getInt("id");
  s.project = toString(v->getString("project"));
  s.name = toString(v->getString("name"));
  s.flags = v->getUint("flags");
  s.checkFlags = v->getUint("checkFlags");
  readStrings(v->get("declarations"), s.declarations);
  s.valueDeclaration = toString(v->getString("valueDeclaration"));
  s.parent = v->getInt("parent");
  s.exportSymbol = v->getInt("exportSymbol");
  return s;
}

SignatureResponse SignatureResponse::from(const JsonValue *v)
{
  SignatureResponse s;
  if (!v || !v->isObject()) {
    return s;
  }
  s.present = true;
  s.id = v->getInt("id");
  s.flags = v->getUint("flags");
  s.declaration = toString(v->getString("declaration"));
  readInts(v->get("typeParameters"), s.typeParameters);
  if (const JsonValue *list = v->get("parameters")) {
    for (int i = 0; i < list->size(); i++) {
      s.parameters.append(list->at(i)->asInt());
    }
  }
  s.thisParameter = v->getInt("thisParameter");
  s.target = v->getInt("target");
  return s;
}

// ---------------------------------------------------------------- Session

Session::Session(Client &client, int snapshot, std::string_view project)
    : m_client(client), m_snapshot(snapshot), m_project(toString(project))
{
}

void Session::begin(JsonWriter &w) const
{
  w.beginObject();
  w.member("snapshot", m_snapshot);
  w.member("project", std::string_view(m_project.c_str(), m_project.size()));
}

bool Session::callType(const char *method,
                       JsonWriter &w,
                       TypeResponse &type,
                       string &error)
{
  w.endObject();
  JsonDocument result;
  if (!m_client.call(method, w.text(), result, error)) {
    return false;
  }
  type = TypeResponse::from(result.root());
  return true;
}

bool Session::callTypes(const char *method,
                        JsonWriter &w,
                        Vector<TypeResponse> &types,
                        string &error)
{
  w.endObject();
  JsonDocument result;
  if (!m_client.call(method, w.text(), result, error)) {
    return false;
  }
  types.clear();
  const JsonValue *list = result.root();
  for (int i = 0; list && i < list->size(); i++) {
    types.append(TypeResponse::from(list->at(i)));
  }
  return true;
}

bool Session::callSymbol(const char *method,
                         JsonWriter &w,
                         SymbolResponse &symbol,
                         string &error)
{
  w.endObject();
  JsonDocument result;
  if (!m_client.call(method, w.text(), result, error)) {
    return false;
  }
  symbol = SymbolResponse::from(result.root());
  return true;
}

bool Session::callBool(const char *method, JsonWriter &w, bool &value, string &error)
{
  w.endObject();
  JsonDocument result;
  if (!m_client.call(method, w.text(), result, error)) {
    return false;
  }
  value = result.root() && result.root()->asBool();
  return true;
}

bool Session::typeAtPosition(std::string_view file,
                             uint32_t position,
                             TypeResponse &type,
                             string &error)
{
  JsonWriter w;
  begin(w);
  w.member("file", file);
  w.member("position", position);
  return callType("getTypeAtPosition", w, type, error);
}

bool Session::typesAtPositions(std::string_view file,
                               span<const uint32_t> positions,
                               Vector<TypeResponse> &types,
                               string &error)
{
  JsonWriter w;
  begin(w);
  w.member("file", file);
  w.key("positions");
  w.beginArray();
  for (uint32_t p : positions) {
    w.value(p);
  }
  w.endArray();
  return callTypes("getTypesAtPositions", w, types, error);
}

bool Session::typeAtLocation(std::string_view handle, TypeResponse &type, string &error)
{
  JsonWriter w;
  begin(w);
  w.member("location", handle);
  return callType("getTypeAtLocation", w, type, error);
}

bool Session::typesAtLocations(span<const string> handles,
                               Vector<TypeResponse> &types,
                               string &error)
{
  JsonWriter w;
  begin(w);
  w.key("locations");
  w.beginArray();
  for (const string &h : handles) {
    w.value(std::string_view(h.c_str(), h.size()));
  }
  w.endArray();
  return callTypes("getTypeAtLocations", w, types, error);
}

bool Session::typesOfType(int typeId, Vector<TypeResponse> &members, string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("getTypesOfType"), typeId);
  return callTypes("getTypesOfType", w, members, error);
}

bool Session::nonNullableType(int typeId, TypeResponse &type, string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("getNonNullableType"), typeId);
  return callType("getNonNullableType", w, type, error);
}

bool Session::typeArguments(int typeId, Vector<TypeResponse> &arguments, string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("getTypeArguments"), typeId);
  return callTypes("getTypeArguments", w, arguments, error);
}

bool Session::signaturesOfType(int typeId,
                               SignatureKind kind,
                               Vector<SignatureResponse> &signatures,
                               string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("getSignaturesOfType"), typeId);
  w.member("kind", int(kind));
  w.endObject();
  JsonDocument result;
  if (!m_client.call("getSignaturesOfType", w.text(), result, error)) {
    return false;
  }
  signatures.clear();
  const JsonValue *list = result.root();
  for (int i = 0; list && i < list->size(); i++) {
    signatures.append(SignatureResponse::from(list->at(i)));
  }
  return true;
}

bool Session::returnTypeOfSignature(int signatureId, TypeResponse &type, string &error)
{
  JsonWriter w;
  begin(w);
  w.member("signature", signatureId);
  return callType("getReturnTypeOfSignature", w, type, error);
}

bool Session::parametersOfSignature(int signatureId,
                                    Vector<SymbolResponse> &parameters,
                                    string &error)
{
  JsonWriter w;
  begin(w);
  w.member("objectId", signatureId);
  w.endObject();
  JsonDocument result;
  if (!m_client.call("getParametersOfSignature", w.text(), result, error)) {
    return false;
  }
  parameters.clear();
  const JsonValue *list = result.root();
  for (int i = 0; list && i < list->size(); i++) {
    parameters.append(SymbolResponse::from(list->at(i)));
  }
  return true;
}

bool Session::symbolOfType(int typeId, SymbolResponse &symbol, string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("getSymbolOfType"), typeId);
  return callSymbol("getSymbolOfType", w, symbol, error);
}

bool Session::aliasSymbolOfType(int typeId, SymbolResponse &symbol, string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("getAliasSymbolOfType"), typeId);
  return callSymbol("getAliasSymbolOfType", w, symbol, error);
}

bool Session::propertiesOfType(int typeId,
                               Vector<SymbolResponse> &properties,
                               string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("getPropertiesOfType"), typeId);
  w.endObject();
  JsonDocument result;
  if (!m_client.call("getPropertiesOfType", w.text(), result, error)) {
    return false;
  }
  properties.clear();
  const JsonValue *list = result.root();
  for (int i = 0; list && i < list->size(); i++) {
    properties.append(SymbolResponse::from(list->at(i)));
  }
  return true;
}

bool Session::typeOfSymbol(int symbolId, TypeResponse &type, string &error)
{
  JsonWriter w;
  begin(w);
  w.member("symbol", symbolId);
  return callType("getTypeOfSymbol", w, type, error);
}

bool Session::declaredTypeOfSymbol(int symbolId, TypeResponse &type, string &error)
{
  JsonWriter w;
  begin(w);
  w.member("symbol", symbolId);
  return callType("getDeclaredTypeOfSymbol", w, type, error);
}

bool Session::propertyOfType(int typeId,
                             std::string_view name,
                             SymbolResponse &symbol,
                             string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("getPropertyOfType"), typeId);
  w.member("name", name);
  w.endObject();
  JsonDocument result;
  if (!m_client.call("getPropertyOfType", w.text(), result, error)) {
    return false;
  }
  symbol = SymbolResponse::from(result.root());
  return true;
}

bool Session::apparentType(int typeId, TypeResponse &type, string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("getApparentType"), typeId);
  return callType("getApparentType", w, type, error);
}

bool Session::baseConstraintOfType(int typeId, TypeResponse &type, string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("getBaseConstraintOfType"), typeId);
  return callType("getBaseConstraintOfType", w, type, error);
}

bool Session::baseTypes(int typeId, Vector<TypeResponse> &types, string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("getBaseTypes"), typeId);
  return callTypes("getBaseTypes", w, types, error);
}

bool Session::targetOfType(int typeId, TypeResponse &type, string &error)
{
  JsonWriter w;
  begin(w);
  w.member("objectId", typeId);
  return callType("getTargetOfType", w, type, error);
}

bool Session::isArrayType(int typeId, bool &result, string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("isArrayType"), typeId);
  return callBool("isArrayType", w, result, error);
}

IndexInfoResponse IndexInfoResponse::from(const JsonValue *v)
{
  IndexInfoResponse info;
  if (!v || !v->isObject()) {
    return info;
  }
  info.keyType = TypeResponse::from(v->get("keyType"));
  info.valueType = TypeResponse::from(v->get("valueType"));
  info.isReadonly = v->getBool("isReadonly", false);
  return info;
}

bool Session::indexInfosOfType(int typeId,
                               Vector<IndexInfoResponse> &infos,
                               string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("getIndexInfosOfType"), typeId);
  w.endObject();
  JsonDocument result;
  if (!m_client.call("getIndexInfosOfType", w.text(), result, error)) {
    return false;
  }
  infos.clear();
  const JsonValue *list = result.root();
  for (int i = 0; list && i < list->size(); i++) {
    infos.append(IndexInfoResponse::from(list->at(i)));
  }
  return true;
}

bool Session::symbolAtPosition(std::string_view file,
                               uint32_t position,
                               SymbolResponse &symbol,
                               string &error)
{
  JsonWriter w;
  begin(w);
  w.member("file", file);
  w.member("position", position);
  return callSymbol("getSymbolAtPosition", w, symbol, error);
}

bool Session::symbolAtLocation(std::string_view handle,
                               SymbolResponse &symbol,
                               string &error)
{
  JsonWriter w;
  begin(w);
  w.member("location", handle);
  return callSymbol("getSymbolAtLocation", w, symbol, error);
}

bool Session::isArrayLikeType(int typeId, bool &result, string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("isArrayLikeType"), typeId);
  return callBool("isArrayLikeType", w, result, error);
}

bool Session::isTypeAssignableTo(int sourceId, int targetId, bool &result, string &error)
{
  JsonWriter w;
  begin(w);
  w.member("source", sourceId);
  w.member("target", targetId);
  return callBool("isTypeAssignableTo", w, result, error);
}

bool Session::typeToString(int typeId, string &text, string &error)
{
  JsonWriter w;
  begin(w);
  w.member(typeIdParam("typeToString"), typeId);
  w.endObject();
  JsonDocument result;
  if (!m_client.call("typeToString", w.text(), result, error)) {
    return false;
  }
  text = toString(result.root() ? result.root()->asString() : std::string_view());
  return true;
}

bool Session::contextualType(std::string_view handle, TypeResponse &type, string &error)
{
  JsonWriter w;
  begin(w);
  w.member("location", handle);
  return callType("getContextualType", w, type, error);
}

bool Session::resolvedSignature(std::string_view handle,
                                SignatureResponse &signature,
                                string &error)
{
  JsonWriter w;
  begin(w);
  w.member("location", handle);
  w.endObject();
  JsonDocument result;
  if (!m_client.call("getResolvedSignature", w.text(), result, error)) {
    return false;
  }
  signature = SignatureResponse::from(result.root());
  return true;
}

bool Session::sourceFile(std::string_view file, EncodedSourceFile &encoded, string &error)
{
  JsonWriter w;
  begin(w);
  w.member("file", file);
  w.endObject();
  Vector<uint8_t, 4> payload;
  if (!m_client.callRaw("getSourceFile", w.text(), payload, error)) {
    return false;
  }
  return encoded.decode(span<const uint8_t>(payload.data(), payload.size()), error);
}

bool Session::sourceFileNames(Vector<string> &names, string &error)
{
  JsonWriter w;
  begin(w);
  w.endObject();
  JsonDocument result;
  if (!m_client.call("getSourceFileNames", w.text(), result, error)) {
    return false;
  }
  names.clear();
  const JsonValue *list = result.root();
  for (int i = 0; list && i < list->size(); i++) {
    std::string_view name = list->at(i)->asString();
    string copy;
    for (char c : name) {
      copy += c;
    }
    names.append(std::move(copy));
  }
  return true;
}

bool Session::release(string &error)
{
  return m_client.release(m_snapshot, error);
}

} // namespace fastlint::tsgo
