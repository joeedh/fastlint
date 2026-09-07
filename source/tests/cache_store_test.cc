#include "fastlint/cache/store.h"
#include "fastlint/tsgo/generated/enums.h"
#include "fastlint/types/type_graph.h"
#include "testing/test.h"

#include <filesystem>
#include <string>

using namespace fastlint;
using namespace fastlint::cache;
using namespace fastlint::types;

namespace {

std::string str(const string &s)
{
  return std::string(s.c_str(), s.size());
}

std::string str(std::string_view s)
{
  return std::string(s);
}

StoreOptions memoryOptions()
{
  StoreOptions options;
  options.path = string(":memory:");
  options.tsgoVersion = string("7.0.2");
  options.libHash = 0x1234;
  return options;
}

/** A temporary database path, removed with its WAL side files when the guard dies. */
struct TempDb {
  std::filesystem::path path;

  TempDb()
  {
    path = std::filesystem::temp_directory_path() /
           ("fastlint-store-test-" + std::to_string(uint64_t(this)) + ".db");
    remove();
  }
  ~TempDb()
  {
    remove();
  }
  void remove()
  {
    std::error_code ec;
    for (const char *suffix : {"", "-wal", "-shm"}) {
      std::filesystem::remove(path.string() + suffix, ec);
    }
  }
  string str() const
  {
    return string(path.generic_string().c_str());
  }
};

/** A small graph: `string`, `undefined`, `string | undefined`, `User` and
 * `Promise<User>`. */
struct SampleGraph {
  TypeGraph graph;
  TypeId str = 0, undef = 0, maybe = 0, user = 0, promise = 0;

  SampleGraph()
  {
    TypeRow row;
    row.flags = tsgo::TypeFlags::String;
    row.text = graph.internString("string");
    row.sessionId = 14;
    str = graph.intern(row, {});
    row = TypeRow();
    row.flags = tsgo::TypeFlags::Undefined;
    row.text = graph.internString("undefined");
    undef = graph.intern(row, {});
    TypeId members[] = {str, undef};
    row = TypeRow();
    row.flags = tsgo::TypeFlags::Union;
    row.childKind = ChildKind::UnionMembers;
    maybe = graph.intern(row, members);

    Vector<string> userDecls;
    userDecls.append(string("7.264.c:/p/src/users.ts"));
    SymbolId userSymbol = graph.internSymbol(
        "User", 64, 0, span<const string>(userDecls.data(), userDecls.size()), 0);
    row = TypeRow();
    row.flags = tsgo::TypeFlags::Object;
    row.objectFlags = tsgo::ObjectFlags::Interface;
    row.symbol = userSymbol;
    user = graph.intern(row, {});
    Vector<string> promiseDecls;
    promiseDecls.append(string("12.263.c:/lib/lib.es5.d.ts"));
    promiseDecls.append(string("40.263.c:/lib/lib.es2015.promise.d.ts"));
    SymbolId promiseSymbol =
        graph.internSymbol("Promise",
                           64,
                           0,
                           span<const string>(promiseDecls.data(), promiseDecls.size()),
                           0);
    row = TypeRow();
    row.flags = tsgo::TypeFlags::Object;
    row.objectFlags = tsgo::ObjectFlags::Reference;
    row.symbol = promiseSymbol;
    row.childKind = ChildKind::TypeArguments;
    TypeId args[] = {user};
    promise = graph.intern(row, args);
  }
};

} // namespace

TEST(cache_store, opens_with_schema_and_meta)
{
  Store store;
  string error;
  REQUIRE(store.open(memoryOptions(), error));
  CHECK_EQ(str(error), std::string());
  CHECK(store.isOpen());
  CHECK(!store.rebuilt());
  StoreMeta meta;
  CHECK(store.readMeta(meta, error));
  CHECK_EQ(meta.schemaVersion, kSchemaVersion);
  CHECK_EQ(str(meta.tsgoVersion), std::string("7.0.2"));
  CHECK_EQ(meta.libHash, uint64_t(0x1234));
  Vector<string> problems;
  CHECK(store.verify(problems, error));
  CHECK_EQ(problems.size(), size_t(0));
  store.close();
  CHECK(!store.isOpen());
}

TEST(cache_store, rebuilds_on_version_change)
{
  TempDb db;
  StoreOptions options = memoryOptions();
  options.path = db.str();
  string error;
  {
    Store store;
    REQUIRE(store.open(options, error));
    FileRecord file;
    file.path = string("c:/p/src/main.ts");
    file.contentHash = 1;
    CHECK(store.putFile(file, error));
  }
  {
    Store store;
    REQUIRE(store.open(options, error));
    CHECK(!store.rebuilt());
    FileRecord file;
    bool found = false;
    CHECK(store.getFile("c:/p/src/main.ts", file, found, error));
    CHECK(found);
  }
  {
    options.tsgoVersion = string("7.0.3");
    Store store;
    REQUIRE(store.open(options, error));
    CHECK(store.rebuilt());
    FileRecord file;
    bool found = true;
    CHECK(store.getFile("c:/p/src/main.ts", file, found, error));
    CHECK(!found);
    StoreMeta meta;
    CHECK(store.readMeta(meta, error));
    CHECK_EQ(str(meta.tsgoVersion), std::string("7.0.3"));
  }
  {
    options.libHash = 99;
    Store store;
    REQUIRE(store.open(options, error));
    CHECK(store.rebuilt());
  }
}

TEST(cache_store, files_node_types_and_rule_results_round_trip)
{
  Store store;
  string error;
  REQUIRE(store.open(memoryOptions(), error));
  CHECK(store.begin(error));

  FileRecord file;
  file.path = string("c:/p/src/main.ts");
  file.contentHash = 0xfeedfacecafebeefull;
  file.closureHash = 2;
  file.tsconfigHash = 3;
  CHECK(store.putFile(file, error));
  FileRecord back;
  bool found = false;
  CHECK(store.getFile("c:/p/src/main.ts", back, found, error));
  CHECK(found);
  CHECK_EQ(back.contentHash, file.contentHash);
  CHECK_EQ(back.closureHash, uint64_t(2));
  CHECK_EQ(str(back.path), std::string("c:/p/src/main.ts"));
  CHECK(store.getFile("c:/p/src/other.ts", back, found, error));
  CHECK(!found);

  NodeType nodes[] = {{10, 20, 5, 0xaaaa}, {10, 15, 7, 0xbbbb}, {30, 31, 5, 0xcccc}};
  CHECK(store.putNodeTypes(file.contentHash, nodes, error));
  NodeType replaced[] = {{10, 20, 5, 0xdddd}};
  CHECK(store.putNodeTypes(file.contentHash, replaced, error));
  Vector<NodeType> loaded;
  CHECK(store.getNodeTypes(file.contentHash, loaded, error));
  REQUIRE_EQ(loaded.size(), size_t(3));
  CHECK_EQ(loaded[0].start, 10u);
  CHECK_EQ(loaded[0].end, 15u);
  CHECK_EQ(loaded[1].typeHash, uint64_t(0xdddd));
  CHECK_EQ(loaded[2].start, 30u);
  CHECK(store.getNodeTypes(1, loaded, error));
  CHECK_EQ(loaded.size(), size_t(0));

  CHECK(store.putRuleResult(
      file.contentHash, 2, "no-floating-promises", "[{\"line\":3}]", error));
  string payload;
  CHECK(store.getRuleResult(
      file.contentHash, 2, "no-floating-promises", payload, found, error));
  CHECK(found);
  CHECK_EQ(str(payload), std::string("[{\"line\":3}]"));
  CHECK(store.getRuleResult(
      file.contentHash, 3, "no-floating-promises", payload, found, error));
  CHECK(!found);
  CHECK(store.commit(error));

  CHECK(store.dropNodeTypes(file.contentHash, error));
  CHECK(store.dropRuleResults(file.contentHash, error));
  CHECK(store.getNodeTypes(file.contentHash, loaded, error));
  CHECK_EQ(loaded.size(), size_t(0));
  CHECK(store.getRuleResult(
      file.contentHash, 2, "no-floating-promises", payload, found, error));
  CHECK(!found);
  CHECK(store.removeFile("c:/p/src/main.ts", error));
  CHECK(store.getFile("c:/p/src/main.ts", back, found, error));
  CHECK(!found);
}

TEST(cache_store, graph_rows_survive_a_save_and_load)
{
  SampleGraph sample;
  Store store;
  string error;
  REQUIRE(store.open(memoryOptions(), error));
  GraphCursor cursor;
  CHECK(store.saveGraph(sample.graph, cursor, error));
  CHECK_EQ(str(error), std::string());
  CHECK_EQ(cursor.types, sample.graph.typeCount());
  CHECK_EQ(cursor.symbols, sample.graph.symbolCount());
  size_t types = 0, symbols = 0;
  CHECK(store.countTypes(types, symbols, error));
  CHECK_EQ(types, size_t(5));
  CHECK_EQ(symbols, size_t(2));

  // A second save writes nothing new; an appended row is picked up from the cursor.
  CHECK(store.saveGraph(sample.graph, cursor, error));
  TypeRow row;
  row.flags = tsgo::TypeFlags::Number;
  row.text = sample.graph.internString("number");
  TypeId number = sample.graph.intern(row, {});
  CHECK(store.saveGraph(sample.graph, cursor, error));
  CHECK_EQ(cursor.types, size_t(6));
  CHECK(store.countTypes(types, symbols, error));
  CHECK_EQ(types, size_t(6));

  TypeGraph loaded;
  CHECK(store.loadGraph(loaded, error));
  CHECK_EQ(str(error), std::string());
  REQUIRE_EQ(loaded.typeCount(), size_t(6));
  REQUIRE_EQ(loaded.symbolCount(), size_t(2));
  for (TypeId id :
       {sample.str, sample.undef, sample.maybe, sample.user, sample.promise, number})
  {
    INFO("type %u", id);
    TypeId other = loaded.byHash(sample.graph.type(id).hash);
    REQUIRE_NE(other, 0u);
    CHECK_EQ(loaded.type(other).flags, sample.graph.type(id).flags);
    CHECK_EQ(loaded.children(other).size(), sample.graph.children(id).size());
    // Loaded rows carry no live session id.
    CHECK_EQ(loaded.type(other).sessionId, 0);
  }
  TypeId maybe = loaded.byHash(sample.graph.type(sample.maybe).hash);
  CHECK_EQ(loaded.children(maybe)[0], loaded.byHash(sample.graph.type(sample.str).hash));
  TypeId promise = loaded.byHash(sample.graph.type(sample.promise).hash);
  SymbolId promiseSymbol = loaded.type(promise).symbol;
  REQUIRE_NE(promiseSymbol, 0u);
  CHECK_EQ(str(loaded.text(loaded.symbol(promiseSymbol).name)), std::string("Promise"));
  REQUIRE_EQ(loaded.declarations(promiseSymbol).size(), size_t(2));
  CHECK_EQ(str(loaded.text(loaded.declarations(promiseSymbol)[1])),
           std::string("40.263.c:/lib/lib.es2015.promise.d.ts"));
  TypeId argument = loaded.children(promise)[0];
  CHECK_EQ(str(loaded.text(loaded.symbol(loaded.type(argument).symbol).name)),
           std::string("User"));

  // Loading into a graph that already holds some rows merges by hash.
  TypeGraph merged;
  TypeRow again;
  again.flags = tsgo::TypeFlags::String;
  again.text = merged.internString("string");
  TypeId preexisting = merged.intern(again, {});
  CHECK(store.loadGraph(merged, error));
  CHECK_EQ(merged.typeCount(), size_t(6));
  CHECK_EQ(merged.byHash(sample.graph.type(sample.str).hash), preexisting);

  Vector<string> problems;
  CHECK(store.verify(problems, error));
  CHECK_EQ(problems.size(), size_t(0));
}

TEST(cache_store, verify_reports_dangling_references)
{
  Store store;
  string error;
  REQUIRE(store.open(memoryOptions(), error));
  NodeType dangling[] = {{0, 1, 5, 0x9999}};
  CHECK(store.putNodeTypes(1, dangling, error));
  Vector<string> problems;
  CHECK(store.verify(problems, error));
  REQUIRE_EQ(problems.size(), size_t(1));
  CHECK_EQ(str(problems[0]), std::string("1 node types pointing at a missing type"));
}
