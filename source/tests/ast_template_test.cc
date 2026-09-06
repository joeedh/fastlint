#include "fastlint/ast/file.h"
#include "fastlint/ast/fixer.h"
#include "fastlint/ast/generated/views.h"
#include "fastlint/ast/lower.h"
#include "fastlint/ast/precedence.h"
#include "fastlint/ast/printer.h"
#include "fastlint/ast/template.h"
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

  explicit Lowered(std::string_view text) : file(&tree)
  {
    syntax::Parser parser(text, {}, diagnostics);
    parser.parseFile(tree);
    root = lower(tree, file);
  }

  std::string print()
  {
    litestl::util::string out;
    printAst(file, out);
    return std::string(out.c_str(), out.size());
  }

  Node *statement(int i) const
  {
    return root->children[i];
  }
  /** The expression of statement `i`, which must be an expression statement. */
  Node *expression(int i) const
  {
    return statement(i)->children[0];
  }
};

/** The printed form of `expr` inserted for the expression of `l`'s first statement. */
std::string replaced(Lowered &l, Node *fresh)
{
  Fixer fixer(l.file);
  if (!fresh || !fixer.replace(l.expression(0), fresh)) {
    return "<replace failed>";
  }
  return l.print();
}

} // namespace

// --------------------------------------------------------------- compile

TEST(ast_template, compile_caches_by_text_and_mode)
{
  const Template *a = Template::compile("$x + 1");
  const Template *b = Template::compile("$x + 1");
  const Template *c = Template::compile("$x + 1", Template::Mode::Expression);
  CHECK(a == b);
  CHECK(a != c);
  CHECK(a->ok());
  CHECK(c->ok());
  CHECK(a->mode() == Template::Mode::Expression);
  CHECK_EQ(a->placeholders().size(), size_t(1));
  CHECK(a->placeholders()[0].name == "x");
  CHECK(a->placeholders()[0].slot == SlotKind::Expression);
}

TEST(ast_template, auto_mode_picks_the_prototype_by_shape)
{
  CHECK(Template::compile("if ($c) $s;")->mode() == Template::Mode::Statement);
  CHECK(Template::compile("a();\nb();")->mode() == Template::Mode::Statements);
  CHECK(Template::compile("\"use strict\";")->mode() == Template::Mode::Statement);
  CHECK(Template::compile("$T[]", Template::Mode::Type)->ok());
  CHECK(Template::compile("{ $a }", Template::Mode::Expression)->prototype()->kind ==
        NodeKind::ObjectExpression);
}

TEST(ast_template, compile_rejects_bad_snippets)
{
  CHECK(!Template::compile("a +")->ok());
  CHECK(!Template::compile("$a$ + 1")->ok());
  CHECK(!Template::compile("a();", Template::Mode::Type)->ok());
}

// ----------------------------------------------------------- instantiate

TEST(ast_template, instantiate_expression_keeps_template_text_verbatim)
{
  Lowered l("foo(a + b);");
  Node *arg = l.expression(0)->children[2];
  Node *n = Template::compile("console.log( $x , 2 )")->instantiate(l.file, {{"x", arg}});
  CHECK(n != nullptr);
  CHECK_EQ(replaced(l, n), "console.log( a + b , 2 );");
}

TEST(ast_template, instantiate_parenthesizes_by_precedence)
{
  {
    Lowered l("foo(a + b);");
    Node *arg = l.expression(0)->children[2];
    Node *n = Template::compile("$x * 2")->instantiate(l.file, {{"x", arg}});
    CHECK_EQ(replaced(l, n), "(a + b) * 2;");
  }
  {
    Lowered l("foo(a * b);");
    Node *arg = l.expression(0)->children[2];
    Node *n = Template::compile("$x * 2")->instantiate(l.file, {{"x", arg}});
    CHECK_EQ(replaced(l, n), "a * b * 2;");
  }
  {
    Lowered l("foo(a + b);");
    Node *arg = l.expression(0)->children[2];
    Node *n = Template::compile("$x.y")->instantiate(l.file, {{"x", arg}});
    CHECK_EQ(replaced(l, n), "(a + b).y;");
  }
  {
    Lowered l("foo(-a);");
    Node *arg = l.expression(0)->children[2];
    Node *n = Template::compile("$x ** 2")->instantiate(l.file, {{"x", arg}});
    CHECK_EQ(replaced(l, n), "(-a) ** 2;");
  }
  {
    Lowered l("foo(a || b);");
    Node *arg = l.expression(0)->children[2];
    Node *n = Template::compile("$x ?? y")->instantiate(l.file, {{"x", arg}});
    CHECK_EQ(replaced(l, n), "(a || b) ?? y;");
  }
  {
    Lowered l("foo(f());");
    Node *arg = l.expression(0)->children[2];
    Node *n = Template::compile("new $x")->instantiate(l.file, {{"x", arg}});
    CHECK_EQ(replaced(l, n), "new (f());");
  }
  {
    Lowered l("foo((a, b));");
    Node *arg = l.expression(0)->children[2];
    Node *n = Template::compile("bar($x)")->instantiate(l.file, {{"x", arg}});
    CHECK_EQ(replaced(l, n), "bar((a, b));");
  }
}

TEST(ast_template, instantiate_statement_slot_takes_statements_and_expressions)
{
  {
    Lowered l("foo();\nbar();");
    Node *s0 = l.statement(0);
    Node *s1 = l.statement(1);
    Fixer fixer(l.file);
    Node *n = Template::compile("if ($c) $s;")
                  ->instantiate(l.file, {{"c", fixer.identifier("cond")}, {"s", s0}});
    CHECK(n != nullptr);
    CHECK(n->kind == NodeKind::IfStatement);
    CHECK(n->children[1] == s0);
    CHECK(fixer.replace(s1, n));
    CHECK_EQ(l.print(), "if (cond) foo();");
  }
  {
    Lowered l("foo();");
    Fixer fixer(l.file);
    Node *n =
        Template::compile("if ($c) $s;")
            ->instantiate(
                l.file, {{"c", fixer.identifier("cond")}, {"s", fixer.identifier("x")}});
    CHECK(n->children[1]->kind == NodeKind::ExpressionStatement);
    CHECK(fixer.replace(l.statement(0), n));
    CHECK_EQ(l.print(), "if (cond) x;");
  }
}

TEST(ast_template, instantiate_bare_placeholder_returns_the_argument)
{
  Lowered l("foo();");
  Fixer fixer(l.file);
  Node *id = fixer.identifier("x");
  CHECK(Template::compile("$x")->instantiate(l.file, {{"x", id}}) == id);
  // Instantiating detaches the argument from the program, so it goes back.
  Node *first = l.statement(0);
  CHECK(Template::compile("$s;", Template::Mode::Statement)
            ->instantiate(l.file, {{"s", first}}) == first);
  CHECK(first->parent == nullptr);
  CHECK(fixer.append(l.root, first));
  Node *stmt = Template::compile("$s;", Template::Mode::Statement)
                   ->instantiate(l.file, {{"s", fixer.identifier("y")}});
  CHECK(stmt->kind == NodeKind::ExpressionStatement);
  CHECK(fixer.append(l.root, stmt));
  CHECK_EQ(l.print(), "foo();\ny;");
}

TEST(ast_template, instantiate_type_slot)
{
  Lowered l("let v: number | string;");
  Node *id = l.statement(0)->children[0]->children[0];
  Node *type = id->children[0];
  Fixer fixer(l.file);
  const Template *array = Template::compile("$T[]", Template::Mode::Type);
  Node *n = array->instantiate(l.file, {{"T", type}});
  CHECK(n != nullptr);
  CHECK(fixer.set(id, 0, n));
  CHECK_EQ(l.print(), "let v: (number | string)[];");

  Node *named = array->instantiate(l.file, {{"T", fixer.identifier("Foo")}});
  CHECK(named->children[0]->kind == NodeKind::TSTypeReference);
  CHECK(fixer.set(id, 0, named));
  CHECK_EQ(l.print(), "let v: Foo[];");
}

TEST(ast_template, instantiate_property_and_pattern_slots)
{
  Lowered l("foo(a + b);");
  Fixer fixer(l.file);
  Node *arg = l.expression(0)->children[2];
  const Template *member = Template::compile("$a.$b");
  CHECK(member->placeholders()[1].slot == SlotKind::Property);
  CHECK(member->instantiate(l.file, {{"a", arg}, {"b", fixer.numberLiteral("1")}}) ==
        nullptr);
  Node *n = member->instantiate(l.file, {{"a", arg}, {"b", fixer.identifier("p")}});
  CHECK_EQ(replaced(l, n), "(a + b).p;");

  const Template *decl = Template::compile("const $id = $init;");
  CHECK(decl->placeholders()[0].slot == SlotKind::Pattern);
  CHECK(decl->instantiate(l.file, {{"id", n}, {"init", n}}) == nullptr);
  Node *d = decl->instantiate(l.file, {{"id", fixer.identifier("t")}, {"init", n}});
  CHECK(fixer.replace(l.statement(0), d));
  CHECK_EQ(l.print(), "const t = (a + b).p;");
}

TEST(ast_template, instantiate_splices_lists)
{
  {
    Lowered l("g(1, 2, 3);");
    Node *call = l.expression(0);
    Vector<Node *, 4> args;
    for (size_t i = 2; i < call->children.size(); i++) {
      args.append(call->children[int(i)]);
    }
    TemplateArgs b;
    b.set("args", span<Node *const>(args.data(), args.size()));
    Node *n = Template::compile("f(0, $args$)")->instantiate(l.file, b);
    CHECK(n != nullptr);
    CHECK_EQ(n->children.size(), size_t(6));
    CHECK_EQ(replaced(l, n), "f(0, 1, 2, 3);");
  }
  {
    Lowered l("{\n  a();\n  b();\n}\nc();");
    Node *block = l.statement(0);
    Vector<Node *, 4> body;
    for (Node *s : block->children) {
      body.append(s);
    }
    TemplateArgs b;
    b.set("body", span<Node *const>(body.data(), body.size()));
    Node *n = Template::compile("while (x) {\n  $body$\n}")->instantiate(l.file, b);
    CHECK(n != nullptr);
    Fixer fixer(l.file);
    CHECK(fixer.replace(l.statement(1), n));
    CHECK(fixer.remove(block));
    CHECK_EQ(l.print(), "while (x) {\n  a();\n  b();\n}");
  }
}

TEST(ast_template, instantiate_repeated_placeholder_clones_later_uses)
{
  Lowered l("foo(a.b);");
  Node *arg = l.expression(0)->children[2];
  Node *n = Template::compile("$x && $x.c")->instantiate(l.file, {{"x", arg}});
  CHECK(n->children[0] == arg);
  CHECK(n->children[1]->children[0] != arg);
  CHECK_EQ(replaced(l, n), "a.b && a.b.c;");
}

TEST(ast_template, instantiate_shorthand_property)
{
  Lowered l("foo(x);");
  Fixer fixer(l.file);
  const Template *t = Template::compile("{ $a, b: 1 }", Template::Mode::Expression);
  CHECK(t->ok());
  Node *n = t->instantiate(l.file, {{"a", fixer.identifier("name")}});
  CHECK(n != nullptr);
  CHECK_EQ(replaced(l, n), "{ name, b: 1 };");
}

TEST(ast_template, instantiate_escapes_dollar)
{
  Lowered l("foo(x);");
  Fixer fixer(l.file);
  Node *n = Template::compile("$$x + $y")
                ->instantiate(l.file, {{"y", fixer.numberLiteral("1")}});
  CHECK(n != nullptr);
  CHECK_EQ(replaced(l, n), "$x + 1;");
}

TEST(ast_template, instantiate_all_yields_statements)
{
  Lowered l("x;");
  Fixer fixer(l.file);
  const Template *t = Template::compile("const $n = $v;\nconsole.log($n);");
  CHECK(t->mode() == Template::Mode::Statements);
  CHECK(t->instantiate(l.file, {}) == nullptr);
  Vector<Node *, 4> out;
  CHECK(t->instantiateAll(
      l.file, {{"n", fixer.identifier("t")}, {"v", fixer.numberLiteral("1")}}, out));
  CHECK_EQ(out.size(), size_t(2));
  for (Node *s : out) {
    CHECK(fixer.append(l.root, s));
  }
  CHECK_EQ(l.print(), "x;\nconst t = 1;\nconsole.log(t);");
}

TEST(ast_template, instantiate_errors_move_nothing)
{
  Lowered l("foo(a + b);");
  Node *arg = l.expression(0)->children[2];
  const Template *t = Template::compile("$x * $y");
  CHECK(t->instantiate(l.file, {{"x", arg}}) == nullptr);
  CHECK(t->instantiate(l.file, {{"x", arg}, {"y", l.statement(0)}}) == nullptr);
  CHECK(arg->parent == l.expression(0));
  CHECK(!l.statement(0)->dirty);
  CHECK_EQ(l.print(), "foo(a + b);");
}

// ----------------------------------------------------------------- match

TEST(ast_template, match_binds_placeholders_and_splices)
{
  Lowered l("foo.bar(1, 2);");
  TemplateArgs b;
  CHECK(Template::compile("$obj.$m($args$)")->match(l.expression(0), b));
  CHECK(b.get("obj")->isIdentifier("foo"));
  CHECK(b.get("m")->isIdentifier("bar"));
  CHECK_EQ(b.list("args").size(), size_t(2));
  CHECK(b.list("args")[1]->isLiteral());

  TemplateArgs none;
  CHECK(!Template::compile("$obj.$m()")->match(l.expression(0), none));
  CHECK(!Template::compile("$obj.$m($a)")->match(l.expression(0), none));
  TemplateArgs one;
  CHECK(Template::compile("$obj.$m($a, $rest$)")->match(l.expression(0), one));
  CHECK(one.get("a")->isLiteral());
  CHECK_EQ(one.list("rest").size(), size_t(1));
}

TEST(ast_template, match_repeated_placeholder_needs_equal_subtrees)
{
  const Template *t = Template::compile("$a + $a");
  {
    Lowered l("x.y + x.y;");
    TemplateArgs b;
    CHECK(t->match(l.expression(0), b));
  }
  {
    Lowered l("x.y + x.z;");
    TemplateArgs b;
    CHECK(!t->match(l.expression(0), b));
  }
}

TEST(ast_template, match_ignores_parentheses_and_binds_statements)
{
  {
    Lowered l("(a) + (b * c);");
    TemplateArgs b;
    CHECK(Template::compile("$x + $y")->match(l.expression(0), b));
    CHECK(b.get("y")->kind == NodeKind::BinaryExpression);
  }
  {
    Lowered l("if (x) { y(); }");
    TemplateArgs b;
    CHECK(Template::compile("if ($c) $s;")->match(l.statement(0), b));
    CHECK(b.get("s")->kind == NodeKind::BlockStatement);
    CHECK(!Template::compile("if ($c) $s; else $t;")->match(l.statement(0), b));
  }
  {
    Lowered l("let v: Foo<string>;");
    Node *type = l.statement(0)->children[0]->children[0]->children[0];
    TemplateArgs b;
    CHECK(Template::compile("$T<$U>", Template::Mode::Type)->match(type, b));
    CHECK(b.get("T")->isIdentifier("Foo"));
    CHECK(b.get("U")->kind == NodeKind::TSKeywordType);
  }
}

TEST(ast_template, match_then_instantiate_rewrites)
{
  Lowered l("foo.bar(1, 2);");
  TemplateArgs b;
  CHECK(Template::compile("$obj.$m($args$)")->match(l.expression(0), b));
  Node *n = Template::compile("$m.call($obj, $args$)")->instantiate(l.file, b);
  CHECK(n != nullptr);
  CHECK_EQ(replaced(l, n), "bar.call(foo, 1, 2);");
}

// ------------------------------------------------------------ precedence

TEST(ast_template, builders_parenthesize_children)
{
  Lowered l("x;");
  Fixer fixer(l.file);
  Node *sum =
      fixer.binary(BinaryOperator::Add, fixer.identifier("a"), fixer.identifier("b"));
  Node *n = fixer.member(sum, fixer.identifier("c"));
  CHECK(sum->hasFlag(Flag::Parenthesized));
  CHECK_EQ(replaced(l, n), "(a + b).c;");
  CHECK(!needsParens(n, 0, fixer.identifier("d")));
}
