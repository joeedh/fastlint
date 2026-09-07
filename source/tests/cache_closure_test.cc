#include "fastlint/ast/file.h"
#include "fastlint/ast/lower.h"
#include "fastlint/cache/closure.h"
#include "fastlint/cache/imports.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "testing/test.h"

#include <filesystem>
#include <string>

using namespace fastlint;
using namespace fastlint::cache;

namespace {

std::string str(const string &s)
{
  return std::string(s.c_str(), s.size());
}

/** A fixed tree of files; paths are compared as written. */
class MemoryFileSystem : public FileSystem {
public:
  void add(const char *path, const char *content)
  {
    m_paths.append(string(path));
    m_contents.append(string(content));
  }
  void set(const char *path, const char *content)
  {
    for (int i = 0; i < int(m_paths.size()); i++) {
      if (str(m_paths[i]) == path) {
        m_contents[i] = string(content);
        return;
      }
    }
    add(path, content);
  }
  bool isFile(std::string_view path) override
  {
    string ignored;
    return readFile(path, ignored);
  }
  bool readFile(std::string_view path, string &out) override
  {
    for (int i = 0; i < int(m_paths.size()); i++) {
      if (view(m_paths[i]) == path) {
        out = m_contents[i];
        return true;
      }
    }
    return false;
  }

private:
  Vector<string> m_paths;
  Vector<string> m_contents;
};

Vector<string> importsOf(std::string_view source)
{
  syntax::Diagnostics diagnostics;
  syntax::GrammarTree tree;
  ast::AstFile file(&tree);
  syntax::Parser parser(source, {}, diagnostics);
  parser.parseFile(tree);
  ast::lower(tree, file);
  Vector<string> specifiers;
  collectImports(file, specifiers);
  return specifiers;
}

std::string projectDir()
{
  return (std::filesystem::path(FASTLINT_TESTS_DIR).parent_path() / "tests" / "fixtures" /
          "projects" / "basic")
      .generic_string();
}

} // namespace

TEST(cache_closure, collects_every_import_form)
{
  Vector<string> specifiers = importsOf("import { a } from \"./a\";\n"
                                        "import type { B } from './b.ts';\n"
                                        "import * as c from \"../c\";\n"
                                        "import d = require(\"d-pkg\");\n"
                                        "export { e } from \"./e\";\n"
                                        "export * from \"./f\";\n"
                                        "export * as g from \"./g\";\n"
                                        "export const h = 1;\n"
                                        "const i = await import(\"./i\");\n"
                                        "const j = require(\"./j\");\n"
                                        "const k = require(name);\n"
                                        "type L = import(\"./l\").L;\n"
                                        "import { a as again } from \"./a\";\n"
                                        "import \"./side-effect\";\n");
  const char *expected[] = {"./a",
                            "./b.ts",
                            "../c",
                            "d-pkg",
                            "./e",
                            "./f",
                            "./g",
                            "./i",
                            "./j",
                            "./l",
                            "./side-effect"};
  REQUIRE_EQ(specifiers.size(), sizeof expected / sizeof expected[0]);
  for (int i = 0; i < int(specifiers.size()); i++) {
    INFO("specifier %d", i);
    CHECK_EQ(str(specifiers[i]), std::string(expected[i]));
  }
}

TEST(cache_closure, joins_and_normalizes_paths)
{
  CHECK_EQ(str(joinPath("c:/p/src", "./users")), std::string("c:/p/src/users"));
  CHECK_EQ(str(joinPath("c:/p/src", "../lib/x.ts")), std::string("c:/p/lib/x.ts"));
  CHECK_EQ(str(joinPath("c:\\p\\src", ".\\a\\..\\b")), std::string("c:/p/src/b"));
  CHECK_EQ(str(joinPath("/p/src", "/abs/x")), std::string("/abs/x"));
  CHECK_EQ(str(joinPath("src", "../../x")), std::string("../x"));
  CHECK_EQ(str(joinPath("", "./x")), std::string("x"));
  CHECK_EQ(str(dirOf("c:/p/src/main.ts")), std::string("c:/p/src"));
  CHECK_EQ(str(dirOf("main.ts")), std::string("."));
  CHECK(isRelativeSpecifier("./a"));
  CHECK(isRelativeSpecifier("../a"));
  CHECK(!isRelativeSpecifier("react"));
  CHECK(!isRelativeSpecifier("@scope/pkg"));
}

TEST(cache_closure, resolves_relative_specifiers)
{
  MemoryFileSystem fs;
  fs.add("c:/p/src/main.ts", "");
  fs.add("c:/p/src/users.ts", "");
  fs.add("c:/p/src/view.tsx", "");
  fs.add("c:/p/src/lib/index.ts", "");
  fs.add("c:/p/src/types.d.ts", "");
  fs.add("c:/p/src/legacy.js", "");
  const char *from = "c:/p/src/main.ts";

  CHECK_EQ(str(resolveImport(from, "./users", fs)), std::string("c:/p/src/users.ts"));
  CHECK_EQ(str(resolveImport(from, "./users.ts", fs)), std::string("c:/p/src/users.ts"));
  CHECK_EQ(str(resolveImport(from, "./users.js", fs)), std::string("c:/p/src/users.ts"));
  CHECK_EQ(str(resolveImport(from, "./view.js", fs)), std::string("c:/p/src/view.tsx"));
  CHECK_EQ(str(resolveImport(from, "./view", fs)), std::string("c:/p/src/view.tsx"));
  CHECK_EQ(str(resolveImport(from, "./lib", fs)), std::string("c:/p/src/lib/index.ts"));
  CHECK_EQ(str(resolveImport(from, "./types", fs)), std::string("c:/p/src/types.d.ts"));
  CHECK_EQ(str(resolveImport(from, "./legacy", fs)), std::string("c:/p/src/legacy.js"));
  CHECK_EQ(str(resolveImport("c:/p/src/lib/index.ts", "../users", fs)),
           std::string("c:/p/src/users.ts"));
  CHECK_EQ(str(resolveImport(from, "./missing", fs)), std::string());
  CHECK_EQ(str(resolveImport(from, "react", fs)), std::string());
}

TEST(cache_closure, closure_hash_follows_dependencies)
{
  MemoryFileSystem fs;
  fs.add("/p/a.ts", "import { b } from './b'; import 'react'; export const a = b;");
  fs.add("/p/b.ts", "import { c } from './c'; export const b = c;");
  fs.add("/p/c.ts", "export const c = 1;");
  fs.add("/p/d.ts", "export const d = 2;");

  ImportGraph graph;
  string error;
  REQUIRE(loadClosure(graph, "/p/a.ts", fs, error));
  CHECK_EQ(graph.fileCount(), size_t(3));
  REQUIRE(loadClosure(graph, "/p/d.ts", fs, error));
  CHECK_EQ(graph.fileCount(), size_t(4));
  const FileEntry *a = graph.file("/p/a.ts");
  REQUIRE(a != nullptr);
  REQUIRE_EQ(a->imports.size(), size_t(1));
  CHECK_EQ(str(a->imports[0]), std::string("/p/b.ts"));
  REQUIRE_EQ(a->unresolved.size(), size_t(1));
  CHECK_EQ(str(a->unresolved[0]), std::string("react"));

  uint64_t hashA = graph.closureHash("/p/a.ts");
  uint64_t hashB = graph.closureHash("/p/b.ts");
  uint64_t hashC = graph.closureHash("/p/c.ts");
  uint64_t hashD = graph.closureHash("/p/d.ts");
  CHECK_NE(hashA, hashB);
  CHECK_NE(hashA, uint64_t(0));
  CHECK_EQ(graph.closureHash("/p/a.ts"), hashA);
  CHECK_EQ(graph.closureHash("/p/missing.ts"), uint64_t(0));

  // Editing the leaf changes every closure above it and nothing beside it.
  fs.set("/p/c.ts", "export const c = 2;");
  REQUIRE(loadClosure(graph, "/p/c.ts", fs, error));
  CHECK_EQ(graph.fileCount(), size_t(4));
  CHECK_NE(graph.closureHash("/p/a.ts"), hashA);
  CHECK_NE(graph.closureHash("/p/b.ts"), hashB);
  CHECK_NE(graph.closureHash("/p/c.ts"), hashC);
  CHECK_EQ(graph.closureHash("/p/d.ts"), hashD);

  // Reloading an unchanged file keeps its hashes.
  uint64_t hashA2 = graph.closureHash("/p/a.ts");
  REQUIRE(loadClosure(graph, "/p/a.ts", fs, error));
  CHECK_EQ(graph.closureHash("/p/a.ts"), hashA2);

  // Dropping the bare import changes the hash too.
  fs.set("/p/a.ts", "import { b } from './b'; export const a = b;");
  REQUIRE(loadClosure(graph, "/p/a.ts", fs, error));
  CHECK_NE(graph.closureHash("/p/a.ts"), hashA2);
}

TEST(cache_closure, cycles_terminate_and_share_a_hash)
{
  MemoryFileSystem fs;
  fs.add("/p/a.ts", "import './b'; export const a = 1;");
  fs.add("/p/b.ts", "import './a'; export const b = 1;");
  ImportGraph graph;
  string error;
  REQUIRE(loadClosure(graph, "/p/a.ts", fs, error));
  CHECK_EQ(graph.fileCount(), size_t(2));
  // Both files reach the same set, so both closures hash alike.
  CHECK_EQ(graph.closureHash("/p/a.ts"), graph.closureHash("/p/b.ts"));
}

TEST(cache_closure, loads_the_basic_project_from_disk)
{
  DiskFileSystem fs;
  ImportGraph graph;
  string error;
  std::string main = projectDir() + "/src/main.ts";
  REQUIRE(loadClosure(graph, main, fs, error));
  CHECK_EQ(str(error), std::string());
  CHECK_EQ(graph.fileCount(), size_t(2));
  const FileEntry *entry = graph.file(main);
  REQUIRE(entry != nullptr);
  REQUIRE_EQ(entry->imports.size(), size_t(1));
  CHECK_EQ(str(entry->imports[0]), projectDir() + "/src/users.ts");
  CHECK_NE(entry->contentHash, uint64_t(0));
  CHECK(!fs.isFile(projectDir() + "/src/nope.ts"));
  string content;
  CHECK(fs.readFile(main, content));
  CHECK_EQ(hashContent(view(content)), entry->contentHash);
}

TEST(cache_closure, file_cache_tracks_freshness)
{
  MemoryFileSystem fs;
  fs.add("/p/a.ts", "import { b } from './b'; export const a = b;");
  fs.add("/p/b.ts", "export const b = 1;");
  ImportGraph graph;
  string error;
  REQUIRE(loadClosure(graph, "/p/a.ts", fs, error));

  Store store;
  StoreOptions options;
  options.path = string(":memory:");
  options.tsgoVersion = string("7.0.2");
  REQUIRE(store.open(options, error));
  FileCache cache(store, graph, 77);

  FileRecord record;
  Freshness freshness = Freshness::Fresh;
  CHECK(!cache.lookup("/p/zzz.ts", record, freshness, error));
  REQUIRE(cache.lookup("/p/a.ts", record, freshness, error));
  CHECK(freshness == Freshness::Missing);
  CHECK_EQ(record.tsconfigHash, uint64_t(77));
  CHECK_EQ(record.closureHash, graph.closureHash("/p/a.ts"));

  // A lint fills the store; the next lookup replays.
  NodeType nodes[] = {{0, 6, 1, 0xabc}};
  CHECK(store.putNodeTypes(record.contentHash, nodes, error));
  CHECK(cache.saveRuleResult(record, "no-floating-promises", "[]", error));
  CHECK(cache.commitFile(record, error));
  REQUIRE(cache.lookup("/p/a.ts", record, freshness, error));
  CHECK(freshness == Freshness::Fresh);
  string payload;
  bool found = false;
  CHECK(cache.ruleResult(record, "no-floating-promises", payload, found, error));
  CHECK(found);
  CHECK_EQ(str(payload), std::string("[]"));

  // A dependency edit leaves a's content alone but stales its closure; the stale entry's
  // node types and rule results go away.
  fs.set("/p/b.ts", "export const b = 'two';");
  REQUIRE(loadClosure(graph, "/p/b.ts", fs, error));
  FileRecord after;
  REQUIRE(cache.lookup("/p/a.ts", after, freshness, error));
  CHECK(freshness == Freshness::Stale);
  CHECK_EQ(after.contentHash, record.contentHash);
  CHECK_NE(after.closureHash, record.closureHash);
  Vector<NodeType> loaded;
  CHECK(store.getNodeTypes(record.contentHash, loaded, error));
  CHECK_EQ(loaded.size(), size_t(0));
  CHECK(cache.ruleResult(record, "no-floating-promises", payload, found, error));
  CHECK(!found);

  // Once relinted and committed it is fresh again; a tsconfig change stales it.
  CHECK(cache.commitFile(after, error));
  REQUIRE(cache.lookup("/p/a.ts", after, freshness, error));
  CHECK(freshness == Freshness::Fresh);
  FileCache other(store, graph, 78);
  REQUIRE(other.lookup("/p/a.ts", after, freshness, error));
  CHECK(freshness == Freshness::Stale);
}
