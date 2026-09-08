#include "fastlint/ast/dispatch.h"
#include "fastlint/ast/file.h"
#include "fastlint/ast/generated/views.h"
#include "fastlint/ast/kind_info.h"
#include "fastlint/ast/lower.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "testing/test.h"

#include <string>

using namespace fastlint;
using namespace fastlint::ast;

namespace {

struct Lowered {
  syntax::Diagnostics diagnostics;
  syntax::GrammarTree tree;
  AstFile file;
  Node *root = nullptr;

  explicit Lowered(std::string_view source) : file(&tree)
  {
    syntax::Parser parser(source, {}, diagnostics);
    parser.parseFile(tree);
    root = lower(tree, file);
  }

  /** The first descendant of `kind`, or null. */
  Node *find(NodeKind kind) const
  {
    Node *found = nullptr;
    root->descendants([&](Node *n) {
      if (!found && n->kind == kind) {
        found = n;
      }
    });
    return found;
  }

  Node *findIdentifier(std::string_view name) const
  {
    Node *found = nullptr;
    root->descendants([&](Node *n) {
      if (!found && n->isIdentifier(name)) {
        found = n;
      }
    });
    return found;
  }
};

} // namespace

TEST(ast_query, categories_follow_nodes_def)
{
  CHECK(hasCategory(NodeKind::IfStatement, Category::Statement));
  CHECK(!hasCategory(NodeKind::IfStatement, Category::Expression));
  CHECK(hasCategory(NodeKind::Identifier, Category::Expression));
  CHECK(hasCategory(NodeKind::Identifier, Category::Pattern));
  CHECK(hasCategory(NodeKind::TSUnionType, Category::Type));
  CHECK_EQ(int(kindInfo(NodeKind::Program).categories), 0);
}

TEST(ast_query, identifier_and_literal_predicates)
{
  Lowered l("foo('bar', \"baz\", 1);");
  CallExpression call = l.find(NodeKind::CallExpression)->as<CallExpression>();
  CHECK(call.callee()->isIdentifier());
  CHECK(call.callee()->isIdentifier("foo"));
  CHECK(!call.callee()->isIdentifier("bar"));
  CHECK(call.callee()->isExpression());
  CHECK(call.callee()->isPattern());
  CHECK(!call.callee()->isStatement());
  span<Node *> args = call.arguments();
  CHECK(args[0]->isLiteral());
  CHECK(args[0]->isStringLiteral());
  CHECK(args[0]->isStringLiteral("bar"));
  CHECK(args[1]->isStringLiteral("baz"));
  CHECK(!args[1]->isStringLiteral("bar"));
  CHECK(args[2]->isLiteral());
  CHECK(!args[2]->isStringLiteral());
  CHECK(!call.node()->isStringLiteral());
}

TEST(ast_query, ancestors_walk_to_the_root)
{
  Lowered l("function f() { return a + b; }");
  Node *a = l.find(NodeKind::BinaryExpression)->children[0];
  CHECK(a->isIdentifier("a"));
  std::string chain;
  for (Node *n : a->ancestors()) {
    chain += kindName(n->kind);
    chain += " ";
  }
  CHECK_EQ(
      chain,
      "BinaryExpression ReturnStatement BlockStatement FunctionDeclaration Program ");
}

TEST(ast_query, enclosing_finds_strict_ancestors)
{
  Lowered l("function f() { return () => a; }");
  Node *a = l.findIdentifier("a");
  CHECK(a != nullptr);
  Node *arrow = a->enclosingFunction();
  CHECK(bool(arrow && arrow->kind == NodeKind::ArrowFunctionExpression));
  CHECK(arrow->enclosingFunction()->kind == NodeKind::FunctionDeclaration);
  CHECK(l.root->enclosingFunction() == nullptr);
  CHECK(a->enclosingStatement()->kind == NodeKind::ReturnStatement);
  Node *ret = a->enclosingStatement();
  CHECK(ret->enclosingStatement()->kind == NodeKind::BlockStatement);
  CHECK(a->enclosing(NodeKind::BlockStatement) != nullptr);
  CHECK(a->enclosing(NodeKind::ClassBody) == nullptr);
  FunctionLike outer = a->enclosing<FunctionLike>();
  CHECK(outer.node() == arrow);
  CHECK(!a->enclosing<ClassLike>());
}

TEST(ast_query, first_child_and_typed_descendants)
{
  Lowered l("class C { m() {} } function f() {} const g = () => {};");
  ClassLike cls = l.root->firstChild<ClassLike>();
  CHECK(bool(cls && cls.id()->isIdentifier("C")));
  FunctionLike fn = l.root->firstChild<FunctionLike>();
  CHECK(bool(fn && fn.id()->isIdentifier("f")));
  CHECK(!l.root->firstChild<Loop>());
  int functions = 0;
  l.root->descendants<FunctionLike>([&](FunctionLike f) {
    CHECK(bool(f));
    functions++;
  });
  CHECK_EQ(functions, 3);
  int total = 0;
  l.root->descendants([&](Node *) { total++; });
  CHECK_EQ(total, l.file.nodeCount() - 1);
}

TEST(ast_query, preorder_covers_every_node_with_nested_subtree_ends)
{
  Lowered l("if (a) { b(c, d); } else e = [1, , 2];");
  span<const PreorderEntry> order = l.file.preorder();
  CHECK_EQ(int(order.size()), l.file.nodeCount());
  CHECK(order[0].node == l.root);
  CHECK_EQ(int(order[0].subtreeEnd), int(order.size()));
  for (uint32_t i = 0; i < order.size(); i++) {
    const PreorderEntry &e = order[i];
    CHECK(e.subtreeEnd > i);
    // Every descendant sits inside the parent's range.
    for (Node *c : e.node->children) {
      if (!c) {
        continue;
      }
      bool found = false;
      for (uint32_t j = i + 1; j < e.subtreeEnd; j++) {
        if (order[j].node == c) {
          found = true;
          CHECK(order[j].subtreeEnd <= e.subtreeEnd);
        }
      }
      CHECK(found);
    }
  }
  // Rebuilding after a manual edit reflects the new shape.
  Node *extra = l.file.make(NodeKind::EmptyStatement);
  l.root->appendChild(extra);
  l.file.buildPreorder();
  CHECK_EQ(int(l.file.preorder().size()), l.file.nodeCount());
}

TEST(ast_query, dispatch_fires_enter_and_exit_in_tree_order)
{
  Lowered l("function f() { g(); } h();");
  std::string trace;
  auto enter = [&](Node *n) { trace += std::string("+") + kindName(n->kind) + " "; };
  auto exit = [&](Node *n) { trace += std::string("-") + kindName(n->kind) + " "; };
  Dispatcher d;
  d.on<FunctionLike>(enter);
  d.onExit<FunctionLike>(exit);
  d.on(NodeKind::CallExpression, enter);
  d.on(NodeKind::Program, enter);
  d.onExit(NodeKind::Program, exit);
  d.run(l.file);
  CHECK_EQ(trace,
           "+Program +FunctionDeclaration +CallExpression -FunctionDeclaration "
           "+CallExpression -Program ");
}

TEST(ast_query, dispatch_keeps_registration_order_within_a_kind)
{
  Lowered l("a;");
  std::string trace;
  auto first = [&](Node *) { trace += "1"; };
  auto second = [&](Node *) { trace += "2"; };
  auto third = [&](Node *) { trace += "3"; };
  Dispatcher d;
  d.on(NodeKind::Identifier, first);
  d.on(NodeKind::ExpressionStatement, third);
  d.on(NodeKind::Identifier, second);
  CHECK_EQ(d.listenerCount(), 3);
  d.run(l.file);
  CHECK_EQ(trace, "312");
  // A second run over the same table is allowed.
  d.run(l.file);
  CHECK_EQ(trace, "312312");
}

TEST(ast_query, dispatch_with_no_listeners_is_a_no_op)
{
  Lowered l("let x = 1;");
  Dispatcher d;
  d.run(l.file);
  CHECK_EQ(d.listenerCount(), 0);
}

TEST(ast_query, dispatch_scope_hook_brackets_each_listener_with_its_owner)
{
  Lowered l("a;");
  std::string trace;
  int ownerA = 0;
  int ownerB = 0;
  auto listen = [&](Node *) { trace += "L"; };
  Dispatcher d;
  d.on(NodeKind::Identifier, listen, &ownerA);
  d.on(NodeKind::Identifier, listen, &ownerB);
  struct Ctx {
    std::string *trace;
    int *a;
    int *b;
  } ctx{&trace, &ownerA, &ownerB};
  d.setScope(
      [](void *c, void *owner, bool begin) {
        Ctx *ctx = static_cast<Ctx *>(c);
        *ctx->trace += begin ? (owner == ctx->a ? "(a" : "(b") : ")";
      },
      &ctx);
  d.run(l.file);
  // Each listener runs bracketed by its owner's begin and a close, in order.
  CHECK_EQ(trace, "(aL)(bL)");
}
