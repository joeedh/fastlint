#include "fastlint/ast/dump.h"
#include "fastlint/ast/file.h"
#include "fastlint/ast/fixer.h"
#include "fastlint/ast/generated/views.h"
#include "fastlint/ast/lower.h"
#include "fastlint/ast/printer.h"
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
  std::string_view source;

  explicit Lowered(std::string_view text) : file(&tree), source(text)
  {
    syntax::Parser parser(text, {}, diagnostics);
    parser.parseFile(tree);
    root = lower(tree, file);
  }

  std::string dump() const
  {
    litestl::util::string out;
    dumpAst(file, out);
    return std::string(out.c_str());
  }

  Node *statement(int i) const
  {
    return root->children[i];
  }

  /** The comments on a node as `place:text` joined by `|`. */
  std::string comments(const Node *n) const
  {
    const CommentList *list = file.comments(n);
    std::string out;
    if (!list) {
      return out;
    }
    for (const Comment &c : *list) {
      if (!out.empty()) {
        out += "|";
      }
      switch (c.place) {
      case CommentPlace::Leading:
        out += "leading:";
        break;
      case CommentPlace::Trailing:
        out += "trailing:";
        break;
      case CommentPlace::Dangling:
        out += "dangling:";
        break;
      }
      out += std::string(source.substr(c.offset, c.length));
    }
    return out;
  }

  /** The kinds of the statements as one string. */
  std::string statementKinds() const
  {
    std::string out;
    for (Node *s : root->children) {
      if (!out.empty()) {
        out += " ";
      }
      out += kindName(s->kind);
    }
    return out;
  }
};

} // namespace

TEST(ast_fixer, replace_swaps_the_slot_and_moves_comments)
{
  Lowered l("foo(/* arg */ a);");
  CallExpression call = l.statement(0)->children[0]->as<CallExpression>();
  Node *a = call.arguments()[0];
  Fixer fixer(l.file);
  Node *b = fixer.identifier("b");
  CHECK(fixer.replace(a, b));
  CHECK(call.arguments()[0] == b);
  CHECK(b->parent == call.node());
  CHECK(a->parent == nullptr);
  CHECK(!isAttached(l.file, a));
  CHECK(isAttached(l.file, b));
  CHECK_EQ(l.comments(b), "leading:/* arg */");
  CHECK_EQ(l.comments(a), "");
  // Dirty from the parent up; the callee stays clean.
  CHECK(call.node()->dirty);
  CHECK(l.statement(0)->dirty);
  CHECK(l.root->dirty);
  CHECK(!call.callee()->dirty);
  CHECK(!fixer.replace(a, b));
  CHECK(!fixer.replace(b, b));
}

TEST(ast_fixer, insert_around_list_elements)
{
  Lowered l("a; b;");
  Fixer fixer(l.file);
  Node *before = fixer.expressionStatement(fixer.identifier("x"));
  Node *after = fixer.expressionStatement(fixer.identifier("y"));
  CHECK(fixer.insertBefore(l.statement(0), before));
  CHECK(fixer.insertAfter(l.statement(2), after));
  CHECK_EQ(int(l.root->children.size()), 4);
  CHECK(l.statement(0) == before);
  CHECK(l.statement(3) == after);
  CHECK(before->parent == l.root);
  CHECK(l.root->dirty);
  CHECK(!l.statement(1)->dirty);
  // An expression statement's expression is a fixed slot.
  Node *expression = l.statement(1)->children[0];
  CHECK(!fixer.insertBefore(expression, fixer.identifier("z")));
  CHECK(!fixer.insertAfter(expression, fixer.identifier("z")));
  CHECK(fixer.append(l.root, fixer.expressionStatement(fixer.identifier("w"))));
  CHECK_EQ(int(l.root->children.size()), 5);
  CHECK(!fixer.append(l.statement(1), fixer.identifier("z")));
}

TEST(ast_fixer, remove_from_list_and_optional_slots)
{
  Lowered l("a; return b; if (c) d; else e;");
  Fixer fixer(l.file);
  Node *first = l.statement(0);
  CHECK(fixer.remove(first));
  CHECK_EQ(l.statementKinds(), "ReturnStatement IfStatement");
  CHECK(first->parent == nullptr);
  ReturnStatement ret = l.statement(0)->as<ReturnStatement>();
  Node *argument = ret.argument();
  CHECK(fixer.remove(argument));
  CHECK(ret.argument() == nullptr);
  CHECK(ret.node()->dirty);
  IfStatement branch = l.statement(1)->as<IfStatement>();
  CHECK(!fixer.remove(branch.test()));
  CHECK(!fixer.remove(branch.consequent()));
  CHECK(fixer.remove(branch.alternate()));
  CHECK(branch.alternate() == nullptr);
  CHECK(!fixer.remove(l.root));
}

TEST(ast_fixer, remove_moves_leading_comments_and_drops_same_line_trailing)
{
  Lowered l("// about a\na(); // after a\nb();");
  Fixer fixer(l.file);
  Node *a = l.statement(0);
  Node *b = l.statement(1);
  CHECK_EQ(l.comments(a), "leading:// about a|trailing:// after a");
  CHECK(fixer.remove(a));
  CHECK_EQ(l.comments(b), "leading:// about a");
  CHECK_EQ(l.comments(a), "");
}

TEST(ast_fixer, remove_keeps_trailing_when_asked)
{
  Lowered l("a(); // after a\nb();");
  Fixer fixer(l.file);
  CHECK(fixer.remove(l.statement(0), CommentPolicy::KeepTrailing));
  CHECK_EQ(l.comments(l.statement(0)), "leading:// after a");
}

TEST(ast_fixer, remove_last_element_trails_the_previous_one)
{
  Lowered l("a();\n// about b\nb();");
  Fixer fixer(l.file);
  Node *a = l.statement(0);
  CHECK(fixer.remove(l.statement(1)));
  CHECK_EQ(l.comments(a), "trailing:// about b");
}

TEST(ast_fixer, remove_only_element_dangles_on_the_parent)
{
  Lowered l("function f() {\n  // about a\n  a();\n}");
  Fixer fixer(l.file);
  Node *body = l.statement(0)->as<FunctionLike>().body();
  Node *a = body->children[0];
  CHECK(fixer.remove(a));
  CHECK_EQ(int(body->children.size()), 0);
  CHECK_EQ(l.comments(body), "dangling:// about a");
}

TEST(ast_fixer, remove_own_line_trailing_comment_moves)
{
  Lowered l("function f() {\n  a();\n  // after a\n}");
  Fixer fixer(l.file);
  Node *body = l.statement(0)->as<FunctionLike>().body();
  Node *a = body->children[0];
  CHECK_EQ(l.comments(a), "trailing:// after a");
  CHECK(fixer.remove(a));
  CHECK_EQ(l.comments(body), "dangling:// after a");
}

TEST(ast_fixer, remove_drop_all_forgets_the_comments)
{
  Lowered l("// about a\na();\nb();");
  Fixer fixer(l.file);
  CHECK(fixer.remove(l.statement(0), CommentPolicy::DropAll));
  CHECK_EQ(l.comments(l.statement(0)), "");
}

TEST(ast_fixer, set_fills_clears_and_replaces_fixed_slots)
{
  Lowered l("return; let x = 1;");
  Fixer fixer(l.file);
  ReturnStatement ret = l.statement(0)->as<ReturnStatement>();
  Node *value = fixer.numberLiteral("2");
  CHECK(fixer.set(ret.node(), 0, value));
  CHECK(ret.argument() == value);
  Node *other = fixer.numberLiteral("3");
  CHECK(fixer.set(ret.node(), 0, other));
  CHECK(ret.argument() == other);
  CHECK(value->parent == nullptr);
  CHECK(fixer.set(ret.node(), 0, nullptr));
  CHECK(ret.argument() == nullptr);
  CHECK(fixer.set(ret.node(), 0, nullptr));
  CHECK(!fixer.set(ret.node(), 1, other));
  // A required slot can be replaced through set but not cleared.
  Node *declarator = l.statement(1)->children[0];
  CHECK(!fixer.set(declarator, 0, nullptr));
  CHECK(fixer.set(declarator, 0, fixer.identifier("y")));
  CHECK(declarator->children[0]->isIdentifier("y"));
}

TEST(ast_fixer, builders_fill_every_slot)
{
  Lowered l("x;");
  Fixer fixer(l.file);
  Node *call =
      fixer.call(fixer.member(fixer.identifier("console"), fixer.identifier("log")),
                 {fixer.stringLiteral("hi"),
                  fixer.numberLiteral("1"),
                  fixer.booleanLiteral(true),
                  fixer.nullLiteral()});
  Node *statement = fixer.expressionStatement(
      fixer.logical(LogicalOperator::And,
                    fixer.unary(UnaryOperator::Not, call),
                    fixer.binary(BinaryOperator::Add,
                                 fixer.identifier("a"),
                                 fixer.awaitExpression(fixer.identifier("b")))));
  CHECK(fixer.replace(l.statement(0), statement));
  Node *decl = fixer.variableDeclaration(VariableKind::Const,
                                         fixer.identifier("t"),
                                         fixer.typeReference(fixer.identifier("T")));
  CHECK(fixer.append(l.root, decl));
  CHECK(fixer.append(
      l.root, fixer.block({fixer.returnStatement(fixer.keywordType(Keyword::Any))})));
  CHECK(fixer.build(NodeKind::IfStatement, {nullptr}) == nullptr);
  CHECK(fixer.build(NodeKind::EmptyStatement, {nullptr}) == nullptr);
  CHECK(fixer.build(NodeKind::EmptyStatement, {}) != nullptr);
  std::string dump = l.dump();
  CHECK(dump.find("(CallExpression :dirty @0-0") != std::string::npos);
  CHECK(dump.find("(Literal \"\\\"hi\\\"\" literalKind=string") != std::string::npos);
  CHECK(dump.find("(LogicalExpression operator=and :dirty") != std::string::npos);
  CHECK(dump.find("(UnaryExpression operator=not :dirty") != std::string::npos);
  CHECK(dump.find("(VariableDeclaration kind=const :dirty") != std::string::npos);
  CHECK(dump.find("(TSKeywordType keyword=any :dirty") != std::string::npos);
  // Every synthesized node has the child count its kind requires.
  bool ok = true;
  l.root->descendants([&](Node *n) {
    const KindInfo &info = kindInfo(n->kind);
    if (n->children.size() < info.fixedChildren) {
      ok = false;
    }
    for (Node *c : n->children) {
      if (c && c->parent != n) {
        ok = false;
      }
    }
  });
  CHECK(ok);
}

TEST(ast_fixer, apply_fixes_in_source_order_and_defers_conflicts)
{
  Lowered l("a(); b(); c();");
  std::string trace;
  Node *callC = l.statement(2)->children[0];
  Node *callA = l.statement(0)->children[0];
  Vector<Fix> fixes;
  // Registered out of order: on c's call, on statement a, on a's call, on a's statement
  // again.
  fixes.append({callC, [&](Fixer &) { trace += "c"; }});
  fixes.append({l.statement(0), [&](Fixer &f) {
                  trace += "A";
                  f.replace(callA, f.identifier("z"));
                }});
  fixes.append({callA, [&](Fixer &) { trace += "a"; }});
  fixes.append({l.statement(0), [&](Fixer &) { trace += "A2"; }});
  FixReport report = applyFixes(l.file, span<Fix>(fixes.data(), fixes.size()));
  // The second fix on statement a finds it dirty; the fix on a's call finds it detached.
  CHECK_EQ(trace, "Ac");
  CHECK_EQ(report.applied, 2);
  CHECK_EQ(report.deferred, 2);
}

TEST(ast_fixer, apply_fixes_on_siblings_all_land)
{
  Lowered l("a(); b();");
  Vector<Fix> fixes;
  for (int i = 0; i < 2; i++) {
    Node *call = l.statement(i)->children[0];
    fixes.append({call, [call](Fixer &f) {
                    CallExpression c(call);
                    f.replace(c.callee(), f.identifier("q"));
                  }});
  }
  FixReport report = applyFixes(l.file, span<Fix>(fixes.data(), fixes.size()));
  CHECK_EQ(report.applied, 2);
  CHECK_EQ(report.deferred, 0);
  CHECK(l.statement(0)->children[0]->as<CallExpression>().callee()->isIdentifier("q"));
  CHECK(l.statement(1)->children[0]->as<CallExpression>().callee()->isIdentifier("q"));
}

TEST(ast_fixer, set_data_reprints_the_node_from_its_template)
{
  Lowered l("var a = 1;\nfor (var i = 0; i < 2; i++) {}\nfor (var k of ks) {}\n");
  Fixer fixer(l.file);
  fixer.setData(l.statement(0), 0, uint8_t(VariableKind::Let));
  fixer.setData(l.statement(1)->children[0], 0, uint8_t(VariableKind::Let));
  fixer.setData(l.statement(2)->children[0], 0, uint8_t(VariableKind::Const));
  litestl::util::string out;
  printAst(l.file, out);
  CHECK_EQ(std::string(out.c_str()),
           "let a = 1;\nfor (let i = 0; i < 2; i++) {}\nfor (const k of ks) {}\n");
}

TEST(ast_fixer, set_flag_makes_a_member_optional)
{
  Lowered l("a.b;");
  Fixer fixer(l.file);
  Node *member = l.statement(0)->children[0];
  fixer.setFlag(member, Flag::Optional, true);
  litestl::util::string out;
  printAst(l.file, out);
  CHECK_EQ(std::string(out.c_str()), "a?.b;");
}

TEST(ast_fixer, add_comment_fills_an_empty_block)
{
  Lowered l("if (x) {}\n");
  Fixer fixer(l.file);
  Node *block = l.statement(0)->children[1];
  CHECK(fixer.addComment(block, "/* empty */"));
  litestl::util::string out;
  printAst(l.file, out);
  CHECK_EQ(std::string(out.c_str()), "if (x) { /* empty */ }\n");
}

TEST(ast_fixer, add_comment_refuses_a_non_empty_block)
{
  Lowered l("if (x) { a(); }\n");
  Fixer fixer(l.file);
  CHECK(!fixer.addComment(l.statement(0)->children[1], "/* empty */"));
}

TEST(ast_fixer, moved_nodes_drop_old_parentheses_and_gain_needed_ones)
{
  Lowered l("type A = (string | number)[]; type B = Array<string | number>;");
  Fixer fixer(l.file);
  // The union leaves its array (parentheses and all) for a generic's argument.
  Node *arrayType = l.statement(0)->children[2];
  Node *unionType = arrayType->children[0];
  CHECK(unionType->hasFlag(Flag::Parenthesized));
  fixer.detach(unionType);
  CHECK(!unionType->hasFlag(Flag::Parenthesized));
  Node *arguments = fixer.build(NodeKind::TSTypeParameterInstantiation, {});
  CHECK(fixer.append(arguments, unionType));
  Node *generic = fixer.typeReference(fixer.identifier("Array"));
  CHECK(fixer.set(generic, 1, arguments));
  CHECK(fixer.replace(arrayType, generic));
  // The other union leaves its generic for an array, which needs parentheses.
  Node *reference = l.statement(1)->children[2];
  Node *otherUnion = reference->children[1]->children[0];
  fixer.detach(otherUnion);
  Node *fresh = fixer.build(NodeKind::TSArrayType, {otherUnion});
  CHECK(fixer.replace(reference, fresh));
  litestl::util::string out;
  printAst(l.file, out);
  CHECK_EQ(std::string(out.c_str()),
           "type A = Array<string | number>; type B = (string | number)[];");
}
