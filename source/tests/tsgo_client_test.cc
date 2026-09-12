#include "fastlint/ast/file.h"
#include "fastlint/ast/lower.h"
#include "fastlint/ast/node.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "fastlint/tsgo/client.h"
#include "fastlint/tsgo/generated/enums.h"
#include "fastlint/tsgo/queries.h"
#include "fastlint/tsgo/source_file.h"
#include "testing/test.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace fastlint;
using namespace fastlint::tsgo;

namespace {

namespace fs = std::filesystem;

std::string repoRoot()
{
  return fs::path(FASTLINT_TESTS_DIR).parent_path().generic_string();
}

std::string projectDir()
{
  return repoRoot() + "/tests/fixtures/projects/basic";
}

std::string readText(const std::string &path)
{
  std::ifstream in(path, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::string str(const string &s)
{
  return std::string(s.c_str(), s.size());
}

/** A started client with the basic fixture project open; `error` explains a false `ok`.
 */
struct Server {
  Client client;
  SnapshotInfo snapshot;
  string error;
  bool ok = false;
  std::string main = projectDir() + "/src/main.ts";
  std::string mainText = readText(main);

  explicit Server(FileProvider *files = nullptr)
  {
    ClientOptions options;
    options.cwd = string(projectDir().c_str());
    options.files = files;
    ok = client.start(options, error) &&
         client.openProject((projectDir() + "/tsconfig.json").c_str(), snapshot, error);
  }

  Session session()
  {
    return Session(
        client, snapshot.id, std::string_view(snapshot.projects[0].id.c_str()));
  }

  string canonicalMain()
  {
    return canonicalPath(main, client.caseSensitiveFileNames());
  }

  uint32_t offsetOf(const char *needle, uint32_t skip = 0)
  {
    return uint32_t(mainText.find(needle)) + skip;
  }
};

bool haveTsgo()
{
  string exe;
  return resolveTsgoExe(std::string_view(repoRoot().c_str()), exe);
}

} // namespace

TEST(tsgo_client, version_gate)
{
  string version;
  CHECK(parseVersionOutput("Version 7.0.2\n", version));
  CHECK_EQ(str(version), std::string("7.0.2"));
  CHECK(parseVersionOutput("Version 7.1.0-dev.20260901", version));
  CHECK_EQ(str(version), std::string("7.1.0-dev.20260901"));
  CHECK(!parseVersionOutput("tsc: command not found", version));
  CHECK(!parseVersionOutput("", version));
  CHECK(versionSupported("7.0.2"));
  CHECK(!versionSupported("7.0.1"));
  CHECK(!versionSupported("7.0.2-dev"));
}

TEST(tsgo_client, a_server_panic_loses_its_stack_and_names_the_fix)
{
  string error;
  formatServerError(
      "panic: interface conversion: checker.TypeData is *checker.TypeReference, "
      "not *checker.TupleType\ngoroutine 7 [running]:\nruntime/debug.Stack()\n",
      "7.0.2",
      error);
  CHECK_EQ(
      str(error),
      std::string("panic: interface conversion: checker.TypeData is "
                  "*checker.TypeReference, not *checker.TupleType (a crash inside tsc "
                  "7.0.2; a newer tsc named by FASTLINT_TSGO may have the fix)"));
  formatServerError("type handle 13 not found in project registry", "7.0.2", error);
  CHECK_EQ(str(error), std::string("type handle 13 not found in project registry"));
}

TEST(tsgo_client, parameter_spellings_come_from_the_probe_table)
{
  CHECK_EQ(std::string(typeIdParam("getTypesOfType")), std::string("objectId"));
  CHECK_EQ(std::string(typeIdParam("getSymbolOfType")), std::string("objectId"));
  CHECK_EQ(std::string(typeIdParam("getNonNullableType")), std::string("type"));
  CHECK_EQ(std::string(typeIdParam("isArrayLikeType")), std::string("type"));
  CHECK_EQ(std::string(typeIdParam("neverProbed")), std::string("type"));
}

TEST_TAGGED(tsgo_client, opens_a_project_and_answers_queries, "integration")
{
  if (!haveTsgo()) {
    SKIP("no native tsc found");
  }
  Server server;
  CHECK_EQ(str(server.error), std::string());
  if (!server.ok) {
    return;
  }
  CHECK_EQ(str(server.client.version()), std::string("7.0.2"));
  CHECK(server.client.alive());
  CHECK(server.snapshot.id > 0);
  CHECK_EQ(server.snapshot.projects.size(), size_t(1));
  CHECK(server.snapshot.projects[0].configFileName.ends_with(string("tsconfig.json")));

  Session session = server.session();
  string error;

  TypeResponse id;
  CHECK(session.typeAtPosition(
      server.main.c_str(), server.offsetOf("id: string"), id, error));
  CHECK_EQ(str(error), std::string());
  CHECK(id.present);
  CHECK((id.flags & TypeFlags::String) != 0u);
  CHECK_EQ(str(id.intrinsicName), std::string("string"));

  // Union members are fetched separately; the non-nullable form drops `undefined`.
  TypeResponse maybe;
  CHECK(session.typeAtPosition(
      server.main.c_str(), server.offsetOf("maybe !== undefined"), maybe, error));
  CHECK(maybe.present);
  CHECK((maybe.flags & TypeFlags::Union) != 0u);
  Vector<TypeResponse> members;
  CHECK(session.typesOfType(maybe.id, members, error));
  CHECK_EQ(members.size(), size_t(2));
  TypeResponse nonNull;
  CHECK(session.nonNullableType(maybe.id, nonNull, error));
  CHECK((nonNull.flags & TypeFlags::String) != 0u);
  string text;
  CHECK(session.typeToString(nonNull.id, text, error));
  CHECK_EQ(str(text), std::string("string"));

  bool assignable = false;
  CHECK(session.isTypeAssignableTo(id.id, maybe.id, assignable, error));
  CHECK(assignable);
  CHECK(session.isTypeAssignableTo(maybe.id, id.id, assignable, error));
  CHECK(!assignable);

  TypeResponse tags;
  CHECK(session.typeAtPosition(
      server.main.c_str(), server.offsetOf("tags: readonly"), tags, error));
  bool arrayLike = false;
  CHECK(session.isArrayLikeType(tags.id, arrayLike, error));
  CHECK(arrayLike);

  // A position lands on the callee token; the call's own type needs a handle.
  uint32_t callStart = server.offsetOf("  fetchUser(id);", 2);
  TypeResponse callee;
  CHECK(session.typeAtPosition(server.main.c_str(), callStart, callee, error));
  CHECK(callee.present);
  CHECK((callee.flags & TypeFlags::Object) != 0u);
  Vector<SignatureResponse> signatures;
  CHECK(session.signaturesOfType(callee.id, SignatureKind::Call, signatures, error));
  CHECK_EQ(signatures.size(), size_t(1));
  if (signatures.size() == 1) {
    TypeResponse returned;
    CHECK(session.returnTypeOfSignature(signatures[0].id, returned, error));
    CHECK(returned.present);
    CHECK((returned.objectFlags & ObjectFlags::Reference) != 0u);
    CHECK(session.typeToString(returned.id, text, error));
    CHECK_EQ(str(text), std::string("Promise<User>"));
    Vector<SymbolResponse> params;
    CHECK(session.parametersOfSignature(signatures[0].id, params, error));
    CHECK_EQ(params.size(), size_t(1));
    if (params.size() == 1) {
      CHECK_EQ(str(params[0].name), std::string("id"));
    }
  }

  SymbolResponse symbol;
  CHECK(session.symbolAtPosition(server.main.c_str(), callStart, symbol, error));
  CHECK(symbol.present);
  CHECK_EQ(str(symbol.name), std::string("fetchUser"));
  // The callee resolves to the import alias, whose declaration is the specifier in
  // main.ts.
  CHECK((symbol.flags & SymbolFlags::Alias) != 0u);
  CHECK_EQ(symbol.declarations.size(), size_t(1));
  if (symbol.declarations.size() == 1) {
    CHECK(str(symbol.declarations[0]).find("src/main.ts") != std::string::npos);
  }

  Vector<uint32_t> positions;
  positions.append(server.offsetOf("id: string"));
  positions.append(callStart);
  positions.append(0);
  Vector<TypeResponse> types;
  CHECK(session.typesAtPositions(server.main.c_str(),
                                 span<const uint32_t>(positions.data(), positions.size()),
                                 types,
                                 error));
  CHECK_EQ(types.size(), size_t(3));
  if (types.size() == 3) {
    CHECK_EQ(types[0].id, id.id);
    CHECK_EQ(types[1].id, callee.id);
  }

  JsonDocument doc;
  CHECK(!server.client.call("noSuchMethod", "{}", doc, error));
  CHECK(str(error).find("unknown API method") != std::string::npos);
  CHECK(server.client.alive());

  CHECK(session.release(error));
}

TEST_TAGGED(tsgo_client, handles_reach_expression_types, "integration")
{
  if (!haveTsgo()) {
    SKIP("no native tsc found");
  }
  Server server;
  CHECK_EQ(str(server.error), std::string());
  if (!server.ok) {
    return;
  }
  Session session = server.session();
  string error;

  EncodedSourceFile encoded;
  CHECK(session.sourceFile(server.main.c_str(), encoded, error));
  CHECK_EQ(str(error), std::string());
  CHECK_EQ(encoded.protocolVersion, 5u);
  CHECK(encoded.nodes.size() > 100);

  uint32_t callStart = server.offsetOf("  fetchUser(id);", 2);
  uint32_t callEnd = callStart + uint32_t(strlen("fetchUser(id)"));
  int index = encoded.findNode(callStart, callEnd, SyntaxKind::CallExpression);
  CHECK(index >= 0);
  string canonical = server.canonicalMain();
  string handle =
      nodeHandle(index, SyntaxKind::CallExpression, std::string_view(canonical.c_str()));
  TypeResponse callType;
  CHECK(session.typeAtLocation(std::string_view(handle.c_str()), callType, error));
  CHECK_EQ(str(error), std::string());
  CHECK(callType.present);
  CHECK((callType.flags & TypeFlags::Object) != 0u);
  SymbolResponse promise;
  CHECK(session.symbolOfType(callType.id, promise, error));
  CHECK_EQ(str(promise.name), std::string("Promise"));

  // The side table gets our own CallExpression node to the same handle.
  syntax::Diagnostics diagnostics;
  syntax::GrammarTree tree;
  ast::AstFile file(&tree);
  syntax::Parser parser(server.mainText, {}, diagnostics);
  parser.parseFile(tree);
  ast::lower(tree, file);
  NodeIndexTable table;
  table.build(file, encoded);
  CHECK(table.mappedCount() > 100);
  const ast::Node *ourCall = nullptr;
  for (const ast::PreorderEntry &entry : file.preorder()) {
    if (entry.node->kind == ast::NodeKind::CallExpression &&
        entry.node->start == callStart)
    {
      ourCall = entry.node;
      break;
    }
  }
  CHECK(ourCall != nullptr);
  if (ourCall) {
    CHECK_EQ(table.lookup(ourCall), index);
    CHECK_EQ(str(table.handle(ourCall, std::string_view(canonical.c_str()))),
             str(handle));
  }

  Vector<string> handles;
  handles.append(handle);
  handles.append(handle);
  Vector<TypeResponse> types;
  CHECK(session.typesAtLocations(
      span<const string>(handles.data(), handles.size()), types, error));
  CHECK_EQ(types.size(), size_t(2));

  // Handles survive a snapshot that leaves the file untouched.
  SnapshotUpdate update;
  update.changed.append(string(server.main.c_str()));
  SnapshotInfo next;
  CHECK(server.client.updateSnapshot(update, next, error));
  CHECK(next.id > server.snapshot.id);
  Session later(
      server.client, next.id, std::string_view(server.snapshot.projects[0].id.c_str()));
  TypeResponse again;
  CHECK(later.typeAtLocation(std::string_view(handle.c_str()), again, error));
  CHECK(again.present);
  CHECK(later.release(error));
  CHECK(session.release(error));
}

namespace {

/** Serves a main.ts that differs from the one on disk. */
class OverrideProvider : public FileProvider {
public:
  explicit OverrideProvider(string mainPath) : m_main(std::move(mainPath))
  {
  }

  bool readFile(std::string_view path, string &content) override
  {
    reads++;
    if (canonicalPath(path, false) != m_main) {
      return false;
    }
    served++;
    content = string("export const fromCallback: 42 = 42;\n");
    return true;
  }

  int fileExists(std::string_view path) override
  {
    return canonicalPath(path, false) == m_main ? 1 : -1;
  }

  int reads = 0;
  int served = 0;

private:
  string m_main;
};

} // namespace

TEST_TAGGED(tsgo_client, serves_file_contents_over_callbacks, "integration")
{
  if (!haveTsgo()) {
    SKIP("no native tsc found");
  }
  OverrideProvider provider(canonicalPath(projectDir() + "/src/main.ts", false));
  Server server(&provider);
  CHECK_EQ(str(server.error), std::string());
  if (!server.ok) {
    return;
  }
  Session session = server.session();
  string error;
  TypeResponse type;
  CHECK(session.typeAtPosition(
      server.main.c_str(), uint32_t(strlen("export const ")), type, error));
  CHECK_EQ(str(error), std::string());
  CHECK(type.present);
  CHECK((type.flags & TypeFlags::NumberLiteral) != 0u);
  CHECK(type.valueKind == JsonKind::Number);
  CHECK_EQ(type.numberValue, 42.0);
  CHECK(provider.reads > 0);
  CHECK_EQ(provider.served, 1);
  CHECK(server.client.stats().callbacks >= provider.reads);
  CHECK(session.release(error));
  server.client.stop();
  CHECK(!server.client.alive());
}
