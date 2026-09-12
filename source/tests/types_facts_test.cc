#include "fastlint/ast/file.h"
#include "fastlint/ast/lower.h"
#include "fastlint/ast/node.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "fastlint/tsgo/client.h"
#include "fastlint/tsgo/generated/enums.h"
#include "fastlint/tsgo/queries.h"
#include "fastlint/types/type_facts.h"
#include "fastlint/types/type_source.h"
#include "testing/test.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace fastlint;
using namespace fastlint::types;

namespace {

namespace fs = std::filesystem;

std::string projectDir()
{
  return fs::path(FASTLINT_TESTS_DIR).parent_path().generic_string() +
         "/tests/fixtures/projects/basic";
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

/** The fixture project opened in a server, with main.ts parsed by our own front end. */
struct Fixture {
  tsgo::Client client;
  tsgo::SnapshotInfo snapshot;
  string error;
  bool ok = false;
  std::string main = projectDir() + "/src/main.ts";
  std::string mainText = readText(main);
  syntax::Diagnostics diagnostics;
  syntax::GrammarTree tree;
  ast::AstFile file;

  Fixture() : file(&tree)
  {
    tsgo::ClientOptions options;
    options.cwd = string(projectDir().c_str());
    ok = client.start(options, error) &&
         client.openProject((projectDir() + "/tsconfig.json").c_str(), snapshot, error);
    syntax::Parser parser(mainText, {}, diagnostics);
    parser.parseFile(tree);
    ast::lower(tree, file);
  }

  tsgo::Session session()
  {
    return tsgo::Session(
        client, snapshot.id, std::string_view(snapshot.projects[0].id.c_str()));
  }

  uint32_t offsetOf(const char *needle, uint32_t skip = 0)
  {
    return uint32_t(mainText.find(needle)) + skip;
  }

  const ast::Node *nodeAt(ast::NodeKind kind, uint32_t start)
  {
    for (const ast::PreorderEntry &entry : file.preorder()) {
      if (entry.node->kind == kind && entry.node->start == start) {
        return entry.node;
      }
    }
    return nullptr;
  }
};

bool haveTsgo()
{
  string exe;
  return tsgo::resolveTsgoExe(std::string_view(projectDir().c_str()), exe);
}

} // namespace

TEST(types_facts, declaration_file_is_the_handle_tail)
{
  CHECK_EQ(std::string(TypeFacts::declarationFile("43.214.c:/dev/x/main.ts")),
           std::string("c:/dev/x/main.ts"));
  CHECK_EQ(std::string(TypeFacts::declarationFile("43.214.c:/dev/lib.es5.d.ts")),
           std::string("c:/dev/lib.es5.d.ts"));
  CHECK_EQ(std::string(TypeFacts::declarationFile("garbage")), std::string());
}

TEST_TAGGED(types_facts, answers_rule_questions_from_nodes, "integration")
{
  if (!haveTsgo()) {
    SKIP("no native tsc found");
  }
  Fixture fx;
  CHECK_EQ(str(fx.error), std::string());
  if (!fx.ok) {
    return;
  }
  tsgo::Session session = fx.session();
  TypeGraph graph;
  TypeFacts facts(session, graph);
  string error;
  CHECK(facts.beginFile(fx.file, fx.main.c_str(), error));

  uint32_t callStart = fx.offsetOf("  fetchUser(id);", 2);
  const ast::Node *call = fx.nodeAt(ast::NodeKind::CallExpression, callStart);
  CHECK(call != nullptr);
  TypeId promise = facts.typeOf(call);
  CHECK_EQ(str(facts.lastError()), std::string());
  CHECK_NE(promise, 0u);
  CHECK(facts.isPromiseLike(promise));
  CHECK(!facts.isNullable(promise));
  CHECK(!facts.isAnyLike(promise));
  CHECK_EQ(facts.unionMembers(promise).size(), size_t(0));
  SymbolId promiseSymbol = facts.symbolOf(promise);
  CHECK_EQ(std::string(facts.text(graph.symbol(promiseSymbol).name)),
           std::string("Promise"));
  CHECK(facts.declarationsOf(promiseSymbol).size() > 0);
  if (facts.declarationsOf(promiseSymbol).size() > 0) {
    std::string_view handle = facts.text(facts.declarationsOf(promiseSymbol)[0]);
    CHECK(TypeFacts::declarationFile(handle).ends_with(".d.ts"));
  }
  // One hop: the reference carries its type argument, which is our `User`.
  CHECK(graph.type(promise).childKind == ChildKind::TypeArguments);
  CHECK_EQ(graph.children(promise).size(), size_t(1));
  if (graph.children(promise).size() == 1) {
    SymbolId user = facts.symbolOf(graph.children(promise)[0]);
    CHECK_EQ(std::string(facts.text(graph.symbol(user).name)), std::string("User"));
  }

  const ast::Node *maybe =
      fx.nodeAt(ast::NodeKind::Identifier, fx.offsetOf("maybe !== undefined"));
  CHECK(maybe != nullptr);
  TypeId maybeType = facts.typeOf(maybe);
  CHECK_NE(maybeType, 0u);
  CHECK(facts.isNullable(maybeType));
  CHECK(!facts.isPromiseLike(maybeType));
  CHECK_EQ(facts.unionMembers(maybeType).size(), size_t(2));

  const ast::Node *anything =
      fx.nodeAt(ast::NodeKind::Identifier, fx.offsetOf("anything.whatever"));
  CHECK(anything != nullptr);
  CHECK(facts.isAnyLike(facts.typeOf(anything)));

  const ast::Node *tags =
      fx.nodeAt(ast::NodeKind::Identifier, fx.offsetOf("of tags)", 3));
  CHECK(tags != nullptr);
  CHECK(facts.isArrayLike(facts.typeOf(tags)));
  CHECK(!facts.isArrayLike(maybeType));

  // `id` in the call and the narrowed `maybe` are both `string`: one interned row.
  const ast::Node *idArg = fx.nodeAt(ast::NodeKind::Identifier, callStart + 10);
  const ast::Node *narrowed =
      fx.nodeAt(ast::NodeKind::Identifier, fx.offsetOf("maybe.toUpperCase"));
  CHECK(idArg != nullptr);
  CHECK(narrowed != nullptr);
  TypeId stringType = facts.typeOf(idArg);
  CHECK_NE(stringType, 0u);
  CHECK_EQ(stringType, facts.typeOf(narrowed));
  CHECK_NE(stringType, maybeType);

  bool assignable = false;
  CHECK(facts.assignableTo(stringType, maybeType, assignable));
  CHECK(assignable);
  CHECK(facts.assignableTo(maybeType, stringType, assignable));
  CHECK(!assignable);

  const ast::Node *callee = fx.nodeAt(ast::NodeKind::Identifier, callStart);
  CHECK(callee != nullptr);
  Vector<Signature> signatures;
  CHECK(facts.callSignatures(facts.typeOf(callee), signatures));
  CHECK_EQ(signatures.size(), size_t(1));
  if (signatures.size() == 1) {
    CHECK(facts.isPromiseLike(signatures[0].returnType));
    CHECK_EQ(signatures[0].returnType, promise);
    CHECK_EQ(signatures[0].parameters.size(), size_t(1));
    if (signatures[0].parameters.size() == 1) {
      CHECK_EQ(std::string(facts.text(graph.symbol(signatures[0].parameters[0]).name)),
               std::string("id"));
    }
  }
  bool stringCallable =
      facts.callSignatures(stringType, signatures) && !signatures.isEmpty();
  CHECK(!stringCallable);

  // A node the side table cannot place answers 0 without a round trip.
  syntax::Diagnostics otherDiagnostics;
  syntax::GrammarTree otherTree;
  ast::AstFile otherFile(&otherTree);
  syntax::Parser otherParser("bar;", {}, otherDiagnostics);
  otherParser.parseFile(otherTree);
  ast::Node *otherRoot = ast::lower(otherTree, otherFile);
  int unmappedBefore = facts.stats().unmappedNodes;
  CHECK_EQ(facts.typeOf(otherRoot->children[0]), 0u);
  CHECK_EQ(facts.stats().unmappedNodes, unmappedBefore + 1);

  int fetchesBefore = facts.stats().typeFetches;
  CHECK_EQ(facts.typeOf(call), promise);
  CHECK_EQ(facts.stats().typeFetches, fetchesBefore);
  CHECK(facts.stats().nodeHits > 0);
  CHECK(graph.typeCount() > 5);

  facts.endFile();
  CHECK(session.release(error));
}

TEST_TAGGED(types_facts, prefetch_batches_a_file, "integration")
{
  if (!haveTsgo()) {
    SKIP("no native tsc found");
  }
  Fixture fx;
  CHECK_EQ(str(fx.error), std::string());
  if (!fx.ok) {
    return;
  }
  tsgo::Session session = fx.session();
  TypeGraph graph;
  TypeFacts facts(session, graph);
  string error;
  CHECK(facts.beginFile(fx.file, fx.main.c_str(), error));

  Vector<const ast::Node *> identifiers;
  for (const ast::PreorderEntry &entry : fx.file.preorder()) {
    if (entry.node->kind == ast::NodeKind::Identifier) {
      identifiers.append(entry.node);
    }
  }
  CHECK(identifiers.size() > 20);
  CHECK(facts.prefetch(
      span<const ast::Node *const>(identifiers.data(), identifiers.size())));
  CHECK_EQ(str(facts.lastError()), std::string());
  CHECK_EQ(facts.stats().typeFetches, 1);
  int typed = 0;
  for (const ast::Node *node : identifiers) {
    if (facts.typeOf(node)) {
      typed++;
    }
  }
  CHECK_EQ(facts.stats().typeFetches, 1);
  CHECK_EQ(facts.stats().nodeHits, int(identifiers.size()));
  CHECK(typed > 10);
  CHECK(session.release(error));
}

// Type handles are scoped to one project's registry within a snapshot, so a
// row interned under one project must lose its handle when the source moves
// to another (seen as "type handle N not found in project registry" on the
// second project of a monorepo).
TEST_TAGGED(types_facts, switching_projects_drops_the_session_ids, "integration")
{
  if (!haveTsgo()) {
    SKIP("no native tsc found");
  }
  std::string root = fs::path(projectDir()).parent_path().generic_string() + "/";
  Vector<string> tsconfigs;
  tsconfigs.append(string((root + "basic/tsconfig.json").c_str()));
  tsconfigs.append(string((root + "loose/tsconfig.json").c_str()));
  ProjectTypes types;
  string error;
  REQUIRE(types.open(tsconfigs, error));

  std::string mainPath = root + "basic/src/main.ts";
  std::string mainText = readText(mainPath);
  syntax::Diagnostics diagnostics;
  syntax::GrammarTree tree;
  ast::AstFile file(&tree);
  syntax::Parser parser(mainText, {}, diagnostics);
  parser.parseFile(tree);
  ast::lower(tree, file);
  types.setFileProject(mainPath, root + "basic/tsconfig.json");
  TypeFacts *facts = types.beginFile(file, mainPath, mainText, error);
  REQUIRE(facts != nullptr);
  const ast::Node *param = nullptr;
  for (const ast::PreorderEntry &entry : file.preorder()) {
    if (entry.node->kind == ast::NodeKind::Identifier &&
        entry.node->start == uint32_t(mainText.find("id: string")))
    {
      param = entry.node;
    }
  }
  REQUIRE(param != nullptr);
  TypeId stringType = facts->typeOf(param);
  REQUIRE_NE(stringType, 0u);
  CHECK_NE(types.graph().type(stringType).sessionId, 0);
  types.endFile();

  std::string otherPath = root + "loose/src/case.ts";
  // The text on disk, so no snapshot update clears the ids by itself.
  std::string otherText = readText(otherPath);
  syntax::Diagnostics otherDiagnostics;
  syntax::GrammarTree otherTree;
  ast::AstFile other(&otherTree);
  syntax::Parser otherParser(otherText, {}, otherDiagnostics);
  otherParser.parseFile(otherTree);
  ast::lower(otherTree, other);
  types.setFileProject(otherPath, root + "loose/tsconfig.json");
  facts = types.beginFile(other, otherPath, otherText, error);
  REQUIRE(facts != nullptr);
  CHECK_EQ(types.graph().type(stringType).sessionId, 0);
  types.endFile();
}

// tsc 7.0.2 crashes serializing an empty tuple literal's type (a reference
// carrying the tuple flag, microsoft/typescript-go#64080); the server recovers,
// and the file's other questions keep their answers. A fixed tsc answers the
// tuple as well.
TEST_TAGGED(types_facts, a_query_that_crashes_the_server_fails_alone, "integration")
{
  if (!haveTsgo()) {
    SKIP("no native tsc found");
  }
  Vector<string> tsconfigs;
  tsconfigs.append(string((projectDir() + "/tsconfig.json").c_str()));
  ProjectTypes types;
  string error;
  REQUIRE(types.open(tsconfigs, error));

  // Served over the on-disk case.ts, the way the typed rule tester lints a case.
  std::string path = projectDir() + "/src/case.ts";
  std::string text = "export const empty = [] as const;\n"
                     "export const pair = Math.random() > 0.5 ? empty : undefined;\n"
                     "export const name = 'x';\n";
  syntax::Diagnostics diagnostics;
  syntax::GrammarTree tree;
  ast::AstFile file(&tree);
  syntax::Parser parser(text, {}, diagnostics);
  parser.parseFile(tree);
  ast::lower(tree, file);
  types.setFileProject(path, projectDir() + "/tsconfig.json");
  TypeFacts *facts = types.beginFile(file, path, text, error);
  REQUIRE(facts != nullptr);
  const ast::Node *literal = nullptr;
  const ast::Node *name = nullptr;
  for (const ast::PreorderEntry &entry : file.preorder()) {
    if (entry.node->kind == ast::NodeKind::ArrayExpression) {
      literal = entry.node;
    } else if (entry.node->kind == ast::NodeKind::Identifier &&
               entry.node->start == uint32_t(text.find("name =")))
    {
      name = entry.node;
    }
  }
  REQUIRE(literal != nullptr);
  REQUIRE(name != nullptr);
  facts->typeOf(literal);
  std::string failure = str(facts->lastError());
  if (str(types.tscVersion()) == "7.0.2") {
    CHECK(!failure.empty());
  }
  if (!failure.empty()) {
    INFO("{}", failure);
    CHECK(failure.starts_with("panic:"));
    CHECK_NE(failure.find("FASTLINT_TSGO"), std::string::npos);
    CHECK_EQ(failure.find("goroutine"), std::string::npos);
  }
  TypeId nameType = facts->typeOf(name);
  REQUIRE_NE(nameType, 0u);
  CHECK((facts->flags(nameType) & tsgo::TypeFlags::StringLiteral) != 0u);
  types.endFile();
}
