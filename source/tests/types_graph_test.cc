#include "fastlint/tsgo/generated/enums.h"
#include "fastlint/types/type_graph.h"
#include "testing/test.h"

#include <string>

using namespace fastlint;
using namespace fastlint::types;

namespace {

TypeId internIntrinsic(TypeGraph &graph, uint32_t flags, const char *name, int sessionId)
{
  TypeRow row;
  row.flags = flags;
  row.text = graph.internString(name);
  row.sessionId = sessionId;
  return graph.intern(row, {});
}

span<const string> view(Vector<string> &items)
{
  return span<const string>(items.data(), items.size());
}

TypeId internUnion(TypeGraph &graph, span<const TypeId> members, int sessionId)
{
  TypeRow row;
  row.flags = tsgo::TypeFlags::Union;
  row.childKind = ChildKind::UnionMembers;
  row.sessionId = sessionId;
  return graph.intern(row, members);
}

} // namespace

TEST(types_graph, interns_strings_and_symbols)
{
  TypeGraph graph;
  StringId a = graph.internString("Promise");
  StringId b = graph.internString("Promise");
  StringId c = graph.internString("promise");
  CHECK_EQ(a, b);
  CHECK_NE(a, c);
  CHECK_NE(a, 0u);
  CHECK_EQ(std::string(graph.text(a)), std::string("Promise"));
  CHECK_EQ(std::string(graph.text(0)), std::string());
  CHECK_EQ(graph.stringCount(), size_t(2));

  Vector<string> decls;
  decls.append(string("12.263.c:/x/lib.es5.d.ts"));
  SymbolId promise = graph.internSymbol("Promise", 64, 0, view(decls), 3);
  SymbolId again = graph.internSymbol("Promise", 64, 0, view(decls), 3);
  CHECK_EQ(promise, again);
  decls.append(string("40.263.c:/x/lib.es2015.promise.d.ts"));
  SymbolId merged = graph.internSymbol("Promise", 64, 0, view(decls), 4);
  CHECK_NE(promise, merged);
  CHECK_EQ(graph.symbolCount(), size_t(2));
  CHECK_EQ(graph.declarations(promise).size(), size_t(1));
  CHECK_EQ(graph.declarations(merged).size(), size_t(2));
  CHECK_EQ(std::string(graph.text(graph.declarations(merged)[1])),
           std::string("40.263.c:/x/lib.es2015.promise.d.ts"));
  CHECK_EQ(graph.symbolBySessionId(3), promise);
  CHECK_EQ(graph.symbolBySessionId(4), merged);
  CHECK_EQ(graph.symbolBySessionId(99), 0u);
  CHECK_EQ(std::string(graph.text(graph.symbol(merged).name)), std::string("Promise"));
}

TEST(types_graph, interns_types_by_structure)
{
  TypeGraph graph;
  TypeId str = internIntrinsic(graph, tsgo::TypeFlags::String, "string", 14);
  TypeId strAgain = internIntrinsic(graph, tsgo::TypeFlags::String, "string", 14);
  TypeId undef = internIntrinsic(graph, tsgo::TypeFlags::Undefined, "undefined", 4);
  TypeId null = internIntrinsic(graph, tsgo::TypeFlags::Null, "null", 8);
  CHECK_EQ(str, strAgain);
  CHECK_NE(str, undef);
  CHECK_EQ(graph.typeCount(), size_t(3));
  CHECK_EQ(graph.type(str).flags, tsgo::TypeFlags::String);
  CHECK_EQ(graph.type(str).sessionId, 14);

  TypeId members1[] = {str, undef};
  TypeId members2[] = {str, null};
  TypeId u1 = internUnion(graph, members1, 112);
  TypeId u2 = internUnion(graph, members2, 113);
  TypeId u1Again = internUnion(graph, members1, 500);
  CHECK_NE(u1, u2);
  CHECK_EQ(u1, u1Again);
  CHECK_EQ(graph.typeCount(), size_t(5));
  CHECK_EQ(graph.children(u1).size(), size_t(2));
  CHECK_EQ(graph.children(u1)[1], undef);
  CHECK_EQ(graph.children(str).size(), size_t(0));
  CHECK(graph.type(u1).childKind == ChildKind::UnionMembers);

  // The latest session id wins and the lookup follows it.
  CHECK_EQ(graph.type(u1).sessionId, 500);
  CHECK_EQ(graph.bySessionId(500), u1);
  CHECK_EQ(graph.bySessionId(113), u2);
  CHECK_EQ(graph.bySessionId(0), 0u);

  // A union with the same members in another order is another shape.
  TypeId swapped[] = {undef, str};
  CHECK_NE(internUnion(graph, swapped, 0), u1);

  graph.clearSessionIds();
  CHECK_EQ(graph.bySessionId(500), 0u);
  CHECK_EQ(graph.type(u1).sessionId, 0);
  CHECK_EQ(graph.typeCount(), size_t(6));
  CHECK_EQ(graph.children(u1).size(), size_t(2));
}

TEST(types_graph, symbol_identity_feeds_type_identity)
{
  TypeGraph graph;
  Vector<string> declsA;
  declsA.append(string("1.263.c:/a.ts"));
  Vector<string> declsB;
  declsB.append(string("1.263.c:/b.ts"));
  SymbolId userA = graph.internSymbol("User", 64, 0, view(declsA), 0);
  SymbolId userB = graph.internSymbol("User", 64, 0, view(declsB), 0);
  TypeRow row;
  row.flags = tsgo::TypeFlags::Object;
  row.objectFlags = tsgo::ObjectFlags::Interface;
  row.symbol = userA;
  TypeId a = graph.intern(row, {});
  row.symbol = userB;
  TypeId b = graph.intern(row, {});
  row.symbol = userA;
  TypeId aAgain = graph.intern(row, {});
  CHECK_NE(a, b);
  CHECK_EQ(a, aAgain);
  CHECK_NE(graph.type(a).hash, graph.type(b).hash);
}
