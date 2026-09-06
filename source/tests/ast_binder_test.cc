#include "fastlint/ast/binder.h"
#include "fastlint/ast/file.h"
#include "fastlint/ast/generated/views.h"
#include "fastlint/ast/lower.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "testing/fixture.h"
#include "testing/snapshot.h"
#include "testing/test.h"

#include <string>

using namespace fastlint;
using namespace fastlint::ast;

namespace {

struct Bound {
  syntax::Diagnostics diagnostics;
  syntax::GrammarTree tree;
  AstFile file;
  Node *root = nullptr;
  Bindings bindings;

  explicit Bound(std::string_view source, syntax::Parser::Options options = {})
      : file(&tree)
  {
    syntax::Parser parser(source, options, diagnostics);
    parser.parseFile(tree);
    root = lower(tree, file);
    bind(file, bindings);
  }

  std::string dump() const
  {
    litestl::util::string out;
    dumpBindings(bindings, out);
    return std::string(out.c_str());
  }

  /** The first identifier spelling `name` at or after `from`, or null. */
  Node *identifier(std::string_view name, uint32_t from = 0) const
  {
    Node *found = nullptr;
    root->descendants([&](Node *n) {
      if (!found && n->isIdentifier(name) && n->start >= from) {
        found = n;
      }
    });
    return found;
  }

  Declaration *declaration(std::string_view name) const
  {
    return bindings.moduleScope()->lookup(name);
  }
};

} // namespace

TEST(ast_binder, fixtures)
{
  test::forEachFile("tests/fixtures/binder", ".ts", [&](const test::Fixture &fixture) {
    Bound b(std::string_view(fixture.text.c_str(), fixture.text.size()));
    SNAPSHOT(b.dump());
  });
  test::forEachFile("tests/fixtures/binder", ".tsx", [&](const test::Fixture &fixture) {
    syntax::Parser::Options options;
    options.jsx = true;
    Bound b(std::string_view(fixture.text.c_str(), fixture.text.size()), options);
    SNAPSHOT(b.dump());
  });
}

TEST(ast_binder, var_hoists_and_let_stays_in_its_block)
{
  Bound b("function f() { if (x) { var a = 1; let c = 2; } return a + c; }");
  Node *fn = b.root->children[0];
  Scope *fnScope = b.bindings.scopeOf(fn);
  CHECK(fnScope != nullptr);
  CHECK(fnScope->kind == ScopeKind::Function);
  Declaration *a = fnScope->lookupLocal("a");
  CHECK(bool(a && a->kind == DeclKind::Var));
  CHECK(fnScope->lookupLocal("c") == nullptr);
  // `a` resolves from the return, `c` does not.
  Reference *ra = b.bindings.referenceOf(b.identifier("a", 40));
  Reference *rc = b.bindings.referenceOf(b.identifier("c", 40));
  CHECK(bool(ra && ra->resolved == a));
  CHECK(bool(rc && rc->resolved == nullptr));
  CHECK_EQ(int(b.bindings.unresolved().size()), 2);
  CHECK_EQ(std::string(b.bindings.unresolved()[0]->name()), "x");
}

TEST(ast_binder, function_declarations_resolve_before_their_text)
{
  Bound b("g(); function g() {}");
  Reference *r = b.bindings.referenceOf(b.identifier("g"));
  CHECK(bool(r && r->resolved && r->resolved->kind == DeclKind::Function));
  CHECK_EQ(int(r->resolved->references.size()), 1);
}

TEST(ast_binder, initializers_and_assignments_are_writes)
{
  Bound b("let x = 1; x = 2; x += 3; x++; use(x);");
  Declaration *x = b.declaration("x");
  CHECK(x != nullptr);
  CHECK_EQ(int(x->references.size()), 5);
  CHECK(x->references[0]->isInit());
  CHECK(x->references[0]->isWrite());
  CHECK(!x->references[1]->isRead());
  CHECK(x->references[1]->isWrite());
  CHECK(x->references[2]->isRead());
  CHECK(x->references[2]->isWrite());
  CHECK(x->references[3]->isWrite());
  CHECK(!x->references[4]->isWrite());
  CHECK(x->references[4]->isRead());
}

TEST(ast_binder, destructuring_declares_every_name)
{
  Bound b("const { a, b: [c, ...d], ...e } = o; function f({ g = 1 }, [h] = []) {}");
  for (const char *name : {"a", "c", "d", "e"}) {
    Declaration *d = b.declaration(name);
    CHECK(bool(d && d->kind == DeclKind::Const));
    CHECK(bool(d && d->references.size() == 1 && d->references[0]->isInit()));
  }
  CHECK(b.declaration("b") == nullptr);
  CHECK(b.declaration("o") == nullptr);
  Scope *fnScope = b.bindings.scopeOf(b.root->children[1]);
  CHECK(fnScope != nullptr);
  Declaration *g = fnScope->lookupLocal("g");
  CHECK(bool(g && g->kind == DeclKind::Parameter && g->references.size() == 1));
  Declaration *h = fnScope->lookupLocal("h");
  CHECK(bool(h && h->references[0]->isInit()));
}

TEST(ast_binder, property_names_are_not_references)
{
  Bound b("o.p; ({ p: 1, [q]: 2, r }); class C { p() {} } l: for (;;) break l;");
  CHECK(b.bindings.referenceOf(b.identifier("p")) == nullptr);
  CHECK(b.bindings.referenceOf(b.identifier("q")) != nullptr);
  CHECK(b.bindings.referenceOf(b.identifier("l")) == nullptr);
  // The shorthand `r` reads a variable through its value node, not its key.
  Node *shorthand = nullptr;
  b.root->descendants<Property>([&](Property p) {
    if (p.isShorthand()) {
      shorthand = p.node();
    }
  });
  CHECK(shorthand != nullptr);
  CHECK(b.bindings.referenceOf(Property(shorthand).key()) == nullptr);
  CHECK(b.bindings.referenceOf(Property(shorthand).value()) != nullptr);
  CHECK_EQ(int(b.bindings.unresolved().size()), 3);
}

TEST(ast_binder, type_and_value_spaces)
{
  Bound b("interface I {} type T = I; const I = 1; let v: I = I; class C {} let c: C = "
          "new C();");
  Scope *m = b.bindings.moduleScope();
  Declaration *iType = m->lookupLocal("I", Space::Type);
  Declaration *iValue = m->lookupLocal("I", Space::Value);
  CHECK(bool(iType && iType->kind == DeclKind::Interface));
  CHECK(bool(iValue && iValue->kind == DeclKind::Const));
  CHECK(iType != iValue);
  CHECK(iType->nextSameName == iValue);
  // `type T = I` and the annotation on `v` reach the interface; the initializer reaches
  // the const.
  CHECK_EQ(int(iType->references.size()), 2);
  CHECK_EQ(int(iValue->references.size()), 2);
  Declaration *c = m->lookupLocal("C");
  CHECK(bool(c && c->inSpace(Space::Type) && c->inSpace(Space::Value)));
  CHECK_EQ(int(c->references.size()), 2);
  CHECK_EQ(int(b.bindings.unresolved().size()), 0);
}

TEST(ast_binder, type_only_imports_live_in_type_space)
{
  Bound b("import { A, type B } from 'm'; import type { C } from 'm'; let a: A = A; let "
          "bb: B = B; let cc: C = C;");
  Scope *m = b.bindings.moduleScope();
  CHECK(m->lookupLocal("A", Space::Value) != nullptr);
  CHECK(m->lookupLocal("B", Space::Value) == nullptr);
  CHECK(m->lookupLocal("B", Space::Type) != nullptr);
  CHECK(m->lookupLocal("C", Space::Value) == nullptr);
  CHECK_EQ(int(b.bindings.unresolved().size()), 2);
}

TEST(ast_binder, scopes_open_on_the_expected_nodes)
{
  Bound b("for (let i = 0;;) {} for (var j = 0;;) {} switch (x) {} try {} catch (e) {} "
          "class K { static {} } namespace N {} enum E {} type M<T> = T;");
  int forScopes = 0;
  int total = 0;
  Scope *m = b.bindings.moduleScope();
  for (Scope *s : m->children) {
    total++;
    if (s->kind == ScopeKind::For) {
      forScopes++;
    }
  }
  CHECK_EQ(forScopes, 1);
  // for-let, for-var body block, switch, try block, catch, class, namespace, enum, type.
  CHECK_EQ(total, 9);
  Scope *cls = b.bindings.scopeOf(b.root->children[4]);
  CHECK(bool(cls && cls->kind == ScopeKind::Class));
  CHECK(bool(cls && cls->children.size() == 1 &&
             cls->children[0]->kind == ScopeKind::StaticBlock));
  CHECK(cls->children[0]->isVariableScope());
  CHECK(cls->children[0]->variableScope() == cls->children[0]);
}

TEST(ast_binder, function_expression_name_is_local)
{
  Bound b("const f = function g() { return g; }; g;");
  Scope *m = b.bindings.moduleScope();
  CHECK(m->lookupLocal("g") == nullptr);
  Reference *inner = b.bindings.referenceOf(b.identifier("g", 20));
  CHECK(bool(inner && inner->resolved && inner->resolved->kind == DeclKind::Function));
  CHECK_EQ(int(b.bindings.unresolved().size()), 1);
}

TEST(ast_binder, rebind_replaces_previous_contents)
{
  Bound b("let a = 1;");
  CHECK_EQ(b.bindings.declarationCount(), 1);
  bind(b.file, b.bindings);
  CHECK_EQ(b.bindings.declarationCount(), 1);
  CHECK_EQ(b.bindings.scopeCount(), 1);
  CHECK(b.bindings.moduleScope() == b.bindings.scopeOf(b.root));
}
