#pragma once

#include "fastlint/tsgo/client.h"
#include "fastlint/tsgo/source_file.h"

#include <cstdint>
#include <string_view>

namespace fastlint::tsgo {

/** `TypeResponse` from tsc/internal/api/proto.go. Union and intersection members are not
 * included; fetch them with `Session::typesOfType`. */
struct TypeResponse {
  /** False for a null response (no type at the position or location). */
  bool present = false;
  int id = 0;
  uint32_t flags = 0;
  uint32_t objectFlags = 0;
  bool isTupleType = false;
  int target = 0;
  int symbol = 0;
  int aliasSymbol = 0;
  int freshType = 0;
  int regularType = 0;
  int objectType = 0;
  int indexType = 0;
  int checkType = 0;
  int extendsType = 0;
  int baseType = 0;
  int fixedLength = 0;
  string intrinsicName;
  /** Literal value: `JsonKind::Number` in `numberValue`, `JsonKind::String` in
   * `stringValue`, `JsonKind::Null` when the type has none. */
  JsonKind valueKind = JsonKind::Null;
  double numberValue = 0;
  string stringValue;
  Vector<int, 2> typeParameters;
  Vector<int, 2> aliasTypeArguments;
  Vector<uint32_t, 2> elementFlags;
  Vector<string, 1> texts;

  static TypeResponse from(const JsonValue *value);
};

struct SymbolResponse {
  bool present = false;
  int id = 0;
  string project;
  string name;
  uint32_t flags = 0;
  uint32_t checkFlags = 0;
  /** Node handles; the third field of each is the declaring file's canonical path. */
  Vector<string, 1> declarations;
  string valueDeclaration;
  int parent = 0;
  int exportSymbol = 0;

  static SymbolResponse from(const JsonValue *value);
};

struct SignatureResponse {
  bool present = false;
  int id = 0;
  uint32_t flags = 0;
  string declaration;
  Vector<int, 2> typeParameters;
  Vector<int, 4> parameters;
  int thisParameter = 0;
  int target = 0;

  static SignatureResponse from(const JsonValue *value);
};

/** One index signature of a type. */
struct IndexInfoResponse {
  TypeResponse keyType;
  TypeResponse valueType;
  bool isReadonly = false;

  static IndexInfoResponse from(const JsonValue *value);
};

enum class SignatureKind : int { Call = 0, Construct = 1 };

/** Spelling of the type-id parameter `method` reads in the pinned version, from the
 * probed table in generated/compat.h; "type" for endpoints the probe did not cover. */
const char *typeIdParam(std::string_view method);

/** Type queries against one snapshot and project. Every call returns false with `error`
 * on a transport or server failure; a type or symbol the server has no answer for comes
 * back with `present == false`. */
class Session {
public:
  Session(Client &client, int snapshot, std::string_view project);

  int snapshot() const
  {
    return m_snapshot;
  }
  const string &project() const
  {
    return m_project;
  }
  Client &client()
  {
    return m_client;
  }

  /** `position` is a UTF-16 offset and lands on a token, not an expression. */
  bool typeAtPosition(std::string_view file,
                      uint32_t position,
                      TypeResponse &type,
                      string &error);
  bool typesAtPositions(std::string_view file,
                        span<const uint32_t> positions,
                        Vector<TypeResponse> &types,
                        string &error);
  bool typeAtLocation(std::string_view handle, TypeResponse &type, string &error);
  bool typesAtLocations(span<const string> handles,
                        Vector<TypeResponse> &types,
                        string &error);
  bool typesOfType(int typeId, Vector<TypeResponse> &members, string &error);
  bool nonNullableType(int typeId, TypeResponse &type, string &error);
  /** Only valid on type references (`ObjectFlags::Reference`); the server panics
   * otherwise. */
  bool typeArguments(int typeId, Vector<TypeResponse> &arguments, string &error);
  bool signaturesOfType(int typeId,
                        SignatureKind kind,
                        Vector<SignatureResponse> &signatures,
                        string &error);
  bool returnTypeOfSignature(int signatureId, TypeResponse &type, string &error);
  bool parametersOfSignature(int signatureId,
                             Vector<SymbolResponse> &parameters,
                             string &error);
  bool symbolOfType(int typeId, SymbolResponse &symbol, string &error);
  bool aliasSymbolOfType(int typeId, SymbolResponse &symbol, string &error);
  /** Properties of an object type (`TypeFlags::Object`); the server panics on other
   * kinds. */
  bool propertiesOfType(int typeId, Vector<SymbolResponse> &properties, string &error);
  bool typeOfSymbol(int symbolId, TypeResponse &type, string &error);
  /** The type a symbol declares (a class or interface's instance type). */
  bool declaredTypeOfSymbol(int symbolId, TypeResponse &type, string &error);
  /** `name` is the property's escaped name; a null response when the type lacks it. */
  bool propertyOfType(int typeId,
                      std::string_view name,
                      SymbolResponse &symbol,
                      string &error);
  bool apparentType(int typeId, TypeResponse &type, string &error);
  /** A null response for an unconstrained type parameter. */
  bool baseConstraintOfType(int typeId, TypeResponse &type, string &error);
  /** Base types of a class or interface instance type; the server panics on other
   * kinds. */
  bool baseTypes(int typeId, Vector<TypeResponse> &types, string &error);
  bool isArrayType(int typeId, bool &result, string &error);
  /** The generic target of a type reference; a null response for other types. */
  bool targetOfType(int typeId, TypeResponse &type, string &error);
  bool indexInfosOfType(int typeId, Vector<IndexInfoResponse> &infos, string &error);
  bool symbolAtPosition(std::string_view file,
                        uint32_t position,
                        SymbolResponse &symbol,
                        string &error);
  bool symbolAtLocation(std::string_view handle, SymbolResponse &symbol, string &error);
  bool isArrayLikeType(int typeId, bool &result, string &error);
  bool isTypeAssignableTo(int sourceId, int targetId, bool &result, string &error);
  bool typeToString(int typeId, string &text, string &error);
  /** Fetches and decodes the server's parse tree of `file`. */
  bool sourceFile(std::string_view file, EncodedSourceFile &encoded, string &error);
  /** Every source file in the project's program, lib and package files included, by the
   * name the server knows it under. */
  bool sourceFileNames(Vector<string> &names, string &error);
  bool release(string &error);

private:
  void begin(JsonWriter &w) const;
  bool callType(const char *method, JsonWriter &w, TypeResponse &type, string &error);
  bool callTypes(const char *method,
                 JsonWriter &w,
                 Vector<TypeResponse> &types,
                 string &error);
  bool
  callSymbol(const char *method, JsonWriter &w, SymbolResponse &symbol, string &error);
  bool callBool(const char *method, JsonWriter &w, bool &result, string &error);

  Client &m_client;
  int m_snapshot;
  string m_project;
};

} // namespace fastlint::tsgo
