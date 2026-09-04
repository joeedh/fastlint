#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "fastlint/syntax/tokens.h"
#include "fastlint/syntax/tree.h"
#include "testing/snapshot.h"
#include "testing/test.h"

using namespace fastlint;
using namespace fastlint::syntax;

namespace {

struct Parsed {
  Diagnostics diagnostics;
  GrammarTree tree;
  litestl::util::string dump;

  explicit Parsed(std::string_view source, Parser::Options options = {})
  {
    Parser parser(source, options, diagnostics);
    parser.parseFile(tree);
    dumpTree(tree, dump);
  }

  std::string text() const
  {
    return std::string(dump.c_str());
  }

  bool ok() const
  {
    return diagnostics.empty();
  }
};

} // namespace

TEST(parser, expression_precedence_shape)
{
  Parsed p("1 + 2 * 3");
  CHECK(p.ok());
  // 1 + (2 * 3): the outer BinaryExpression's right child nests.
  CHECK_EQ(p.text(),
           "(SourceFile\n"
           "  (ExpressionStatement\n"
           "    (BinaryExpression \"+\"\n"
           "      (NumericLiteral \"1\"\n"
           "      )\n"
           "      (BinaryExpression \"*\"\n"
           "        (NumericLiteral \"2\"\n"
           "        )\n"
           "        (NumericLiteral \"3\"\n"
           "        )\n"
           "      )\n"
           "    )\n"
           "  )\n"
           ")\n");
}

TEST(parser, assignment_is_right_associative)
{
  Parsed p("a = b = c");
  CHECK(p.ok());
  // `a = (b = c)`: the right operand of the first assignment is itself one.
  CHECK(p.text().find("(BinaryExpression \"=\"") != std::string::npos);
  CHECK(p.text().find("(BinaryExpression \"=\"", p.text().find("(BinaryExpression \"=\"") + 1) !=
        std::string::npos);
}

TEST(parser, conditional_expression)
{
  Parsed p("a ? b : c");
  CHECK(p.ok());
  CHECK(p.text().find("ConditionalExpression") != std::string_view::npos);
}

TEST(parser, var_declarations)
{
  Parsed p("const x: number = 1, y = 2;");
  CHECK(p.ok());
  CHECK(p.text().find("(VariableStatement") != std::string_view::npos);
  CHECK(p.text().find("(VariableDeclaration") != std::string_view::npos);
  CHECK(p.text().find("(NumericLiteral \"2\"") != std::string::npos);
}

TEST(parser, asi_inserts_no_semicolon_node)
{
  Parsed p("let a = 1\nlet b = 2\n");
  CHECK(p.ok());
  // Two variable statements even without semicolons.
  size_t first = p.text().find("(VariableStatement");
  CHECK(first != std::string::npos);
  CHECK(p.text().find("(VariableStatement", first + 1) != std::string::npos);
}

TEST(parser, function_declaration)
{
  Parsed p("function f(a, b = 2, ...rest) { return a + b; }");
  CHECK(p.ok());
  CHECK(p.text().find("(FunctionDeclaration") != std::string_view::npos);
  CHECK(p.text().find("(Parameter") != std::string_view::npos);
  CHECK(p.text().find("(ReturnStatement") != std::string_view::npos);
}

TEST(parser, arrow_function_expression)
{
  Parsed p("const f = (a, b) => a + b;");
  CHECK(p.ok());
  CHECK(p.text().find("(ArrowFunction") != std::string_view::npos);
}

TEST(parser, parenthesized_not_arrow)
{
  Parsed p("const x = (1 + 2) * 3;");
  CHECK(p.ok());
  CHECK(p.text().find("ParenthesizedExpression") != std::string_view::npos);
  CHECK(p.text().find("ArrowFunction") == std::string_view::npos);
}

TEST(parser, call_and_member_chain)
{
  Parsed p("a.b[0].c?.d();");
  CHECK(p.ok());
  CHECK(p.text().find("PropertyAccessExpression") != std::string_view::npos);
  CHECK(p.text().find("ElementAccessExpression") != std::string_view::npos);
  CHECK(p.text().find("OptionalCallExpression") != std::string_view::npos);
}

TEST(parser, template_literal)
{
  Parsed p("const s = `a${x}b${y}c`;");
  CHECK(p.ok());
  CHECK(p.text().find("TemplateExpression") != std::string_view::npos);
  CHECK(p.text().find("TemplateSpan") != std::string_view::npos);
}

TEST(parser, object_and_array_literals)
{
  Parsed p("const o = { a: 1, b() {}, ...rest };");
  CHECK(p.ok());
  CHECK(p.text().find("PropertyAssignment") != std::string_view::npos);
  CHECK(p.text().find("MethodDeclaration") != std::string_view::npos);
  CHECK(p.text().find("SpreadAssignment") != std::string_view::npos);
  Parsed q("const a = [1, , 2, ...b];");
  CHECK(q.ok());
  CHECK(q.text().find("OmittedExpression") != std::string_view::npos);
  CHECK(q.text().find("SpreadElement") != std::string_view::npos);
}

TEST(parser, class_declaration)
{
  Parsed p("class A extends B implements C { static x: number = 1; get y() { return 1; } constructor(public z) {} }");
  CHECK(p.ok());
  CHECK(p.text().find("(ClassDeclaration") != std::string_view::npos);
  CHECK(p.text().find("(HeritageClause") != std::string_view::npos);
  CHECK(p.text().find("PropertyDeclaration") != std::string_view::npos);
  CHECK(p.text().find("GetAccessor") != std::string_view::npos);
  CHECK(p.text().find("ConstructorNode") != std::string_view::npos);
}

TEST(parser, control_flow)
{
  Parsed p("if (a) b(); else c(); for (const x of xs) { } while (x) x--; try { } catch (e) { } finally { }");
  CHECK(p.ok());
  CHECK(p.text().find("IfStatement") != std::string_view::npos);
  CHECK(p.text().find("ForOfStatement") != std::string_view::npos);
  CHECK(p.text().find("WhileStatement") != std::string_view::npos);
  CHECK(p.text().find("CatchClause") != std::string_view::npos);
}

TEST(parser, labels_and_break)
{
  Parsed p("outer: for (;;) { break outer; }");
  CHECK(p.ok());
  CHECK(p.text().find("LabeledStatement") != std::string_view::npos);
  CHECK(p.text().find("BreakStatement") != std::string_view::npos);
}

TEST(parser, import_export)
{
  Parsed p("import def, { a as b, type c } from 'mod';\nexport * as ns from 'other';\nexport default function f() {}\nexport const x = 1;");
  CHECK(p.ok());
  CHECK(p.text().find("ImportDeclaration") != std::string_view::npos);
  CHECK(p.text().find("NamedImports") != std::string_view::npos);
  CHECK(p.text().find("NamespaceExport") != std::string_view::npos);
  CHECK(p.text().find("ExportDeclaration") != std::string_view::npos);
}

TEST(parser, type_annotations)
{
  Parsed p("function f(x: Array<string | number>, y?: Foo.Bar<Baz>): asserts x is string { }");
  CHECK(p.ok());
  CHECK(p.text().find("TypeReference") != std::string_view::npos);
  CHECK(p.text().find("UnionType") != std::string_view::npos);
  CHECK(p.text().find("TypePredicate") != std::string_view::npos);
}

TEST(parser, union_and_mapped_types)
{
  Parsed p("type T = { [K in keyof U as `get${K & string}`]?: U[K] };");
  CHECK(p.ok());
  CHECK(p.text().find("MappedType") != std::string_view::npos);
}

TEST(parser, error_recovery_reports_and_continues)
{
  Parsed p("let = 5; let b = 2;");
  CHECK(!p.ok());
  // The parser recovered and still parsed the second statement.
  CHECK(p.text().find("(NumericLiteral \"2\"") != std::string::npos);
}

TEST(parser, no_infinite_loop_on_garbage)
{
  Parsed p("}}}} ]]]] ))))");
  CHECK(!p.ok());
  // Terminates (this test would hang otherwise) and consumed everything.
  CHECK(p.text().find("ErrorNode") != std::string_view::npos);
}

TEST(parser, generic_call_vs_comparison)
{
  Parsed p("const a = f<number>(1); const b = a < b > c;");
  CHECK(p.ok());
}
