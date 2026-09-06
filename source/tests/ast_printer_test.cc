#include "fastlint/ast/file.h"
#include "fastlint/ast/fixer.h"
#include "fastlint/ast/generated/views.h"
#include "fastlint/ast/lower.h"
#include "fastlint/ast/printer.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "test_sources.h"
#include "testing/fixture.h"
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

  explicit Lowered(std::string_view text, syntax::Parser::Options options = {})
      : file(&tree), source(text)
  {
    syntax::Parser parser(text, options, diagnostics);
    parser.parseFile(tree);
    root = lower(tree, file);
  }

  std::string print(PrintOptions options = {})
  {
    litestl::util::string out;
    printAst(file, out, options);
    return std::string(out.c_str(), out.size());
  }

  Node *statement(int i) const
  {
    return root->children[i];
  }

  /** Captures every node's layout and flags it dirty, as a fix touching it would. */
  void dirtyAll()
  {
    dirtyAll(root);
  }
  void dirtyAll(Node *n)
  {
    file.captureLayout(n);
    n->dirty = true;
    for (Node *c : n->children) {
      if (c) {
        dirtyAll(c);
      }
    }
  }
};

std::string_view sv(const litestl::util::string &s)
{
  return std::string_view(s.c_str(), s.size());
}

} // namespace

TEST(ast_printer, clean_tree_round_trips_fixtures)
{
  for (const char *dir : {"tests/fixtures/ast", "tests/fixtures/binder"}) {
    test::forEachFile(dir, ".ts", [&](const test::Fixture &fixture) {
      Lowered l(sv(fixture.text));
      CHECK_EQ(l.print(), std::string(sv(fixture.text)));
      l.dirtyAll();
      CHECK_EQ(l.print(), std::string(sv(fixture.text)));
    });
  }
}

TEST(ast_printer, dirty_but_unchanged_tree_round_trips_corpus)
{
  test::loadTestSources();
  for (const test::TestSource &source : test::tsTestSources) {
    syntax::Parser::Options options;
    std::string path(source.path.c_str());
    options.jsx = path.ends_with(".tsx");
    Lowered l(sv(source.source), options);
    std::string clean = l.print();
    if (clean != std::string(sv(source.source))) {
      CHECK_EQ(path + ": clean", path + ": differs");
      continue;
    }
    l.dirtyAll();
    std::string dirty = l.print();
    if (dirty != std::string(sv(source.source))) {
      CHECK_EQ(path + ": dirty", path + ": differs");
    }
  }
}

TEST(ast_printer, replaced_identifier_keeps_surrounding_text)
{
  Lowered l("foo( a ,  b );");
  Fixer fixer(l.file);
  CallExpression call = l.statement(0)->children[0]->as<CallExpression>();
  CHECK(fixer.replace(call.arguments()[0], fixer.identifier("x")));
  CHECK_EQ(l.print(), "foo( x ,  b );");
}

TEST(ast_printer, removed_statement_takes_its_leading_text)
{
  Lowered l("a();\n  b();\n  c();\n");
  Fixer fixer(l.file);
  CHECK(fixer.remove(l.statement(1)));
  CHECK_EQ(l.print(), "a();\n  c();\n");
  CHECK(fixer.remove(l.statement(0)));
  CHECK_EQ(l.print(), "c();\n");
}

TEST(ast_printer, removed_arguments_take_their_separators)
{
  Lowered l("f(a, b, c);");
  Fixer fixer(l.file);
  CallExpression call = l.statement(0)->children[0]->as<CallExpression>();
  CHECK(fixer.remove(call.arguments()[1]));
  CHECK_EQ(l.print(), "f(a, c);");
  CHECK(fixer.remove(call.arguments()[0]));
  CHECK_EQ(l.print(), "f(c);");
  CHECK(fixer.remove(call.arguments()[0]));
  CHECK_EQ(l.print(), "f();");
}

TEST(ast_printer, inserted_statement_copies_a_neighbours_separator)
{
  Lowered l("{\n  a();\n  b();\n}");
  Fixer fixer(l.file);
  Node *block = l.statement(0);
  Node *fresh = fixer.expressionStatement(fixer.call(fixer.identifier("x"), {}));
  CHECK(fixer.insertAfter(block->children[0], fresh));
  CHECK_EQ(l.print(), "{\n  a();\n  x();\n  b();\n}");
  Node *first = fixer.expressionStatement(fixer.call(fixer.identifier("w"), {}));
  CHECK(fixer.insertBefore(block->children[0], first));
  CHECK_EQ(l.print(), "{\n  w();\n  a();\n  x();\n  b();\n}");
  Node *last = fixer.expressionStatement(fixer.call(fixer.identifier("z"), {}));
  CHECK(fixer.append(block, last));
  CHECK_EQ(l.print(), "{\n  w();\n  a();\n  x();\n  b();\n  z();\n}");
}

TEST(ast_printer, inserted_argument_copies_the_separator)
{
  Lowered l("f(a,b);");
  Fixer fixer(l.file);
  CallExpression call = l.statement(0)->children[0]->as<CallExpression>();
  CHECK(fixer.insertAfter(call.arguments()[1], fixer.identifier("c")));
  CHECK_EQ(l.print(), "f(a,b,c);");
  CHECK(fixer.insertBefore(call.arguments()[0], fixer.identifier("z")));
  CHECK_EQ(l.print(), "f(z,a,b,c);");
}

TEST(ast_printer, appending_to_an_empty_list_uses_the_default_separator)
{
  Lowered l("f();\n{}\n");
  Fixer fixer(l.file);
  CallExpression call = l.statement(0)->children[0]->as<CallExpression>();
  CHECK(fixer.append(call.node(), fixer.identifier("a")));
  CHECK(fixer.append(call.node(), fixer.identifier("b")));
  Node *block = l.statement(1);
  CHECK(fixer.append(block, fixer.expressionStatement(fixer.identifier("x"))));
  CHECK(fixer.append(block, fixer.expressionStatement(fixer.identifier("y"))));
  CHECK_EQ(l.print(), "f(a, b);\n{\n  x;\n  y;\n}\n");
}

TEST(ast_printer, cleared_optional_slot_drops_the_space_before_it)
{
  Lowered l("function f() { return x; }");
  Fixer fixer(l.file);
  Node *ret = l.statement(0)->as<FunctionLike>().body()->children[0];
  CHECK(fixer.remove(ReturnStatement(ret).argument()));
  CHECK_EQ(l.print(), "function f() { return; }");
}

TEST(ast_printer, filled_optional_slot_falls_back_to_the_kind_template)
{
  Lowered l("function f() { return; }");
  Fixer fixer(l.file);
  Node *ret = l.statement(0)->as<FunctionLike>().body()->children[0];
  CHECK(fixer.set(ret, 0, fixer.numberLiteral("1")));
  CHECK_EQ(l.print(), "function f() { return 1; }");
}

TEST(ast_printer, removed_node_comments_move_and_print)
{
  Lowered l("// about a\na(); // after a\nb();\n");
  Fixer fixer(l.file);
  CHECK(fixer.remove(l.statement(0)));
  CHECK_EQ(l.print(), "// about a\nb();\n");
}

TEST(ast_printer, kept_trailing_comment_prints_after_the_neighbour)
{
  Lowered l("a(); // after a\nb();\n");
  Fixer fixer(l.file);
  CHECK(fixer.remove(l.statement(0), CommentPolicy::KeepTrailing));
  CHECK_EQ(l.print(), "// after a\nb();\n");
}

TEST(ast_printer, dropped_comments_leave_no_trace)
{
  Lowered l("// about a\na(); // after a\nb();\n");
  Fixer fixer(l.file);
  CHECK(fixer.remove(l.statement(0), CommentPolicy::DropAll));
  CHECK_EQ(l.print(), "b();\n");
}

TEST(ast_printer, comment_of_a_last_removed_element_trails_the_previous)
{
  Lowered l("{\n  a();\n  // about b\n  b();\n}");
  Fixer fixer(l.file);
  Node *block = l.statement(0);
  CHECK(fixer.remove(block->children[1]));
  CHECK_EQ(l.print(), "{\n  a(); // about b\n}");
}

TEST(ast_printer, synthesized_nodes_print_from_templates)
{
  Lowered l("x;");
  Fixer fixer(l.file);
  Node *call =
      fixer.call(fixer.member(fixer.identifier("console"), fixer.identifier("log")),
                 {fixer.stringLiteral("hi"), fixer.numberLiteral("1")});
  CHECK(fixer.replace(l.statement(0), fixer.expressionStatement(call)));
  CHECK(fixer.append(
      l.root,
      fixer.variableDeclaration(VariableKind::Const,
                                fixer.identifier("t"),
                                fixer.awaitExpression(fixer.identifier("p")))));
  Node *sum =
      fixer.binary(BinaryOperator::Add, fixer.identifier("b"), fixer.identifier("c"));
  Node *test = fixer.logical(
      LogicalOperator::And, fixer.identifier("a"), fixer.unary(UnaryOperator::Not, sum));
  Node *block = fixer.block({fixer.returnStatement(test)});
  CHECK(fixer.append(l.root, block));
  CHECK_EQ(l.print(),
           "console.log(\"hi\", 1);\nconst t = await p;\n{\n  return a && !b + c;\n}");
}

TEST(ast_printer, style_is_sniffed_from_the_file)
{
  Lowered l("const a = 'x'\nconst b = 'y'\n");
  Fixer fixer(l.file);
  CHECK(fixer.append(l.root, fixer.expressionStatement(fixer.stringLiteral("z"))));
  CHECK_EQ(l.print(), "const a = 'x'\nconst b = 'y'\n'z'\n");
  Lowered tabs("if (a) {\n\tb();\n}\n");
  Fixer fixer2(tabs.file);
  Node *inner = tabs.statement(0)->as<IfStatement>().consequent();
  CHECK(fixer2.append(
      inner, fixer2.block({fixer2.expressionStatement(fixer2.identifier("c"))})));
  CHECK_EQ(tabs.print(), "if (a) {\n\tb();\n\t{\n\t\tc;\n\t}\n}\n");
}

TEST(ast_printer, parenthesized_flag_wraps_a_moved_node)
{
  Lowered l("x = a + b; y = c * 2;");
  Fixer fixer(l.file);
  Node *sum = l.statement(0)->children[0]->as<AssignmentExpression>().right();
  Node *product = l.statement(1)->children[0]->as<AssignmentExpression>().right();
  BinaryExpression mul(product);
  CHECK(fixer.replace(mul.left(), sum));
  sum->setFlag(Flag::Parenthesized);
  CHECK_EQ(l.print(), "x =; y = (a + b) * 2;");
}

TEST(ast_printer, update_spans_matches_the_output)
{
  Lowered l("a();\nb(c);\n");
  Fixer fixer(l.file);
  CallExpression call = l.statement(1)->children[0]->as<CallExpression>();
  Node *fresh = fixer.identifier("longer");
  CHECK(fixer.replace(call.arguments()[0], fresh));
  CHECK(fixer.insertBefore(l.statement(0),
                           fixer.expressionStatement(fixer.identifier("z"))));
  PrintOptions options;
  options.updateSpans = true;
  std::string text = l.print(options);
  CHECK_EQ(text, "z;\na();\nb(longer);\n");
  bool ok = true;
  l.root->descendants([&](Node *n) {
    if (n->end < n->start || n->end > text.size()) {
      ok = false;
    }
  });
  CHECK(ok);
  CHECK_EQ(text.substr(fresh->start, fresh->end - fresh->start), "longer");
  Node *b = call.callee();
  CHECK_EQ(text.substr(b->start, b->end - b->start), "b");
  Node *stmtA = l.statement(1);
  CHECK_EQ(text.substr(stmtA->start, stmtA->end - stmtA->start), "a();");
  CHECK_EQ(int(l.root->start), 0);
  CHECK_EQ(l.root->end, uint32_t(text.size()));
}
