#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "fastlint/syntax/tokens.h"
#include "fastlint/syntax/tree.h"
#include "test_sources.h"
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

size_t count(const std::string &text, std::string_view needle)
{
  size_t n = 0;
  for (size_t at = text.find(needle); at != std::string::npos;
       at = text.find(needle, at + 1))
  {
    ++n;
  }
  return n;
}

} // namespace

TEST(parser, expression_precedence_shape)
{
  Parsed p("1 + 2 * 3");
  CHECK(p.ok());
  // 1 + (2 * 3): the outer BinaryExpression's right child nests.
  CHECK_EQ(p.text(),
           "(SourceFile\n"
           "  (ExpressionStatement : asi\n"
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
  // Assignment is right-associative, so the right operand is itself an assignment
  CHECK(p.text().find("(BinaryExpression \"=\"") != std::string::npos);
  CHECK(p.text().find("(BinaryExpression \"=\"",
                      p.text().find("(BinaryExpression \"=\"") + 1) != std::string::npos);
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
  Parsed p("class A extends B implements C { static x: number = 1; get y() { return 1; } "
           "constructor(public z) {} }");
  CHECK(p.ok());
  CHECK(p.text().find("(ClassDeclaration") != std::string_view::npos);
  CHECK(p.text().find("(HeritageClause") != std::string_view::npos);
  CHECK(p.text().find("PropertyDeclaration") != std::string_view::npos);
  CHECK(p.text().find("GetAccessor") != std::string_view::npos);
  CHECK(p.text().find("ConstructorNode") != std::string_view::npos);
}

TEST(parser, class_declaration_accessors)
{
  Parsed p(R"(
class A {
  accessor prop1: number;
  accessor prop2 = -1;
  accessor prop3 = 'c' as 'a'|'b'|'c';
}
)");

  CHECK(p.ok());
  CHECK(p.text().find("(ClassDeclaration") != std::string_view::npos);
  // Every member is a property carrying the accessor flag, not a method.
  CHECK_EQ(count(p.text(), "(PropertyDeclaration : accessor"), size_t(3));
  CHECK_EQ(count(p.text(), "(PropertyDeclaration"), size_t(3));
  CHECK(p.text().find("(MethodDeclaration") == std::string::npos);
  CHECK(p.text().find("(GetAccessor") == std::string::npos);
  // prop1: annotated, no initializer.
  size_t prop1 = p.text().find("(Identifier \"prop1\"");
  size_t prop2 = p.text().find("(Identifier \"prop2\"");
  size_t prop3 = p.text().find("(Identifier \"prop3\"");
  CHECK(prop1 < prop2);
  CHECK(prop2 < prop3);
  size_t number = p.text().find("(KeywordType \"number\"");
  CHECK(prop1 < number);
  CHECK(number < prop2);
  // prop2: a negated literal initializer.
  size_t negative = p.text().find("(PrefixUnaryExpression \"-\"");
  CHECK(prop2 < negative);
  CHECK(negative < prop3);
  CHECK(p.text().find("(NumericLiteral \"1\"", negative) < prop3);
  // prop3: `'c' as 'a'|'b'|'c'` is an AsExpression over a three-way literal union.
  size_t as = p.text().find("(AsExpression");
  CHECK(prop3 < as);
  CHECK(p.text().find("(StringLiteral \"'c'\"", as) != std::string::npos);
  CHECK(p.text().find("(UnionType", as) != std::string::npos);
  CHECK_EQ(count(p.text(), "(LiteralType"), size_t(3));

  // `accessor` is still an ordinary name when no member name follows it.
  Parsed q("class B { accessor; accessor = 1; accessor() {} }");
  CHECK(q.ok());
  CHECK(q.text().find(": accessor") == std::string::npos);
  CHECK_EQ(count(q.text(), "(Identifier \"accessor\""), size_t(3));
  CHECK_EQ(count(q.text(), "(PropertyDeclaration"), size_t(2));
  CHECK_EQ(count(q.text(), "(MethodDeclaration"), size_t(1));
}

TEST(parser, control_flow)
{
  Parsed p("if (a) b(); else c(); for (const x of xs) { } while (x) x--; try { } catch "
           "(e) { } finally { }");
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
  Parsed p("import def, { a as b, type c } from 'mod';\nexport * as ns from "
           "'other';\nexport default function f() {}\nexport const x = 1;");
  CHECK(p.ok());
  CHECK(p.text().find("ImportDeclaration") != std::string_view::npos);
  CHECK(p.text().find("NamedImports") != std::string_view::npos);
  CHECK(p.text().find("NamespaceExport") != std::string_view::npos);
  CHECK(p.text().find("ExportDeclaration") != std::string_view::npos);
}

TEST(parser, type_annotations)
{
  Parsed p(
      "function f(x: Array<string | number>, y?: Foo.Bar<Baz>): asserts x is string { }");
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
  Parsed p("let a = ; let b = 2;");
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

#if 1
TEST(parser, real_world_ts_code)
{
  fastlint::test::loadTestSources();
  for (const auto &ts : fastlint::test::tsTestSources) {
    printf("== parsing %s ==\n", ts.path.c_str());
    fflush(stdout);
    Parsed p(ts.source);
    CHECK(p.ok());
    for (const Diagnostic &d : p.diagnostics.items()) {
      printf("  %s:%u: TS%u %s\n",
             ts.path.c_str(),
             p.tree.lineOf(d.offset),
             d.code,
             d.message.c_str());
    }
    fflush(stdout);
  }
}
#endif

// ------------------------------------------------------------ error recovery

TEST(parser, class_body_recovers_after_bad_token)
{
  Parsed p("class A { a() {} ) b() {} }\nlet z = 1;");
  CHECK(!p.ok());
  // The stray token becomes an ErrorNode; later members and statements survive.
  CHECK_EQ(count(p.text(), "(MethodDeclaration"), size_t(2));
  CHECK(p.text().find("(ErrorNode") != std::string::npos);
  CHECK(p.text().find("(VariableStatement") != std::string::npos);
}

TEST(parser, class_cut_off_at_eof)
{
  Parsed p("class A { a() {");
  CHECK(!p.ok());
  CHECK(p.text().find("(ClassDeclaration") != std::string::npos);
  CHECK(p.text().find("(MethodDeclaration") != std::string::npos);
}

TEST(parser, unclosed_parameter_list_ends_at_class_brace)
{
  Parsed p("class A { foo( }\nlet z = 1;");
  CHECK(!p.ok());
  CHECK(p.text().find("(MethodDeclaration") != std::string::npos);
  CHECK(p.text().find("(VariableStatement") != std::string::npos);
}

TEST(parser, decorator_with_bad_expression)
{
  Parsed p("@) class A {}");
  CHECK(!p.ok());
  CHECK(p.text().find("(Decorator") != std::string::npos);
  CHECK(p.text().find("(ClassDeclaration") != std::string::npos);
}

TEST(parser, unclosed_arguments_stop_at_semicolon)
{
  Parsed p("f(a, b;\nlet z = 1;");
  CHECK(!p.ok());
  CHECK(p.text().find("(CallExpression") != std::string::npos);
  CHECK(p.text().find("(VariableStatement") != std::string::npos);
}

TEST(parser, semicolon_class_element)
{
  Parsed p("class A { ; a() {} ; }");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(SemicolonClassElement"), size_t(2));
}

TEST(parser, keyword_property_names)
{
  Parsed p(
      "const o = { default: 1, delete: 2 }; class A { if() {} } type T = { for: 1 };");
  CHECK(p.ok());
}

TEST(parser, class_visibility_modifiers)
{
  Parsed p("class A { private x = 1; protected y() {} public z: number; "
           "constructor(private w: number) {} }");
  CHECK(p.ok());
  CHECK(p.text().find("(PropertyDeclaration : private") != std::string::npos);
  CHECK(p.text().find("(MethodDeclaration : protected") != std::string::npos);
  CHECK(p.text().find("(PropertyDeclaration : public") != std::string::npos);
  CHECK(p.text().find("(Parameter : private") != std::string::npos);
}

// ---------------------------------------------------------------------- ASI

TEST(parser, asi_flag_records_insertion)
{
  Parsed p("let a = 1\nlet b = 2;");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(VariableStatement : asi"), size_t(1));
  // The written `;` is a token of the statement itself.
  CHECK_EQ(count(p.text(), "(VariableStatement \";\""), size_t(1));
}

TEST(parser, missing_semicolon_is_an_error_not_asi)
{
  Parsed p("let a = 1 let b = 2");
  CHECK(!p.ok());
  // The first statement has no ASI flag; the second ends at EOF by ASI.
  CHECK(p.text().find("(VariableStatement\n") <
        p.text().find("(VariableStatement : asi"));
}

TEST(parser, do_while_asi_needs_no_line_break)
{
  Parsed p("do x(); while (y) z();");
  CHECK(p.ok());
  CHECK(p.text().find("(DoStatement : asi") != std::string::npos);
  CHECK_EQ(count(p.text(), "(ExpressionStatement"), size_t(2));
}

TEST(parser, written_semicolon_is_inside_the_statement)
{
  Parsed p("do x(); while (y);");
  CHECK(p.ok());
  CHECK(p.text().find("(DoStatement : asi") == std::string::npos);
  CHECK(p.text().find("\")\" \";\"") != std::string::npos);
}

// ------------------------------------------------------------------ grammar

TEST(parser, binary_operators_associate_left_except_exponent)
{
  Parsed p("a - b - c");
  CHECK(p.ok());
  // (a - b) - c: the left child of the outer node is the inner node.
  size_t outer = p.text().find("(BinaryExpression \"-\"");
  size_t inner = p.text().find("(BinaryExpression \"-\"", outer + 1);
  size_t c = p.text().find("(Identifier \"c\"");
  CHECK(inner != std::string::npos);
  CHECK(inner < c);
  Parsed q("a ** b ** c");
  CHECK(q.ok());
  // a ** (b ** c): `a` comes before the inner node.
  size_t a = q.text().find("(Identifier \"a\"");
  size_t qouter = q.text().find("(BinaryExpression \"**\"");
  size_t qinner = q.text().find("(BinaryExpression \"**\"", qouter + 1);
  CHECK(a < qinner);
}

TEST(parser, as_and_satisfies_expressions)
{
  Parsed p("const a = x as unknown as T[]; const b = y satisfies Foo<Bar>; const c = [1] "
           "as const;");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(AsExpression"), size_t(3));
  CHECK(p.text().find("(SatisfiesExpression") != std::string::npos);
}

TEST(parser, as_after_line_break_ends_the_expression)
{
  Parsed p("let x = y\nas(1);");
  CHECK(p.ok());
  CHECK(p.text().find("(AsExpression") == std::string::npos);
  CHECK(p.text().find("(CallExpression") != std::string::npos);
}

TEST(parser, type_arguments_on_calls_and_new)
{
  Parsed p("f<number>(1); new Set<string>(); a.b<T, U>`x`; g<Array<Array<T>>>();");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(ExpressionWithTypeArguments"), size_t(4));
  CHECK(p.text().find("(TaggedTemplateExpression") != std::string::npos);
  Parsed q("const b = a < b > c; const d = a < b;");
  CHECK(q.ok());
  CHECK(q.text().find("(ExpressionWithTypeArguments") == std::string::npos);
}

TEST(parser, regular_expression_literal)
{
  Parsed p("const r = /a'b/g.test(s); const q = x / y / z;");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(RegularExpressionLiteral"), size_t(1));
  CHECK_EQ(count(p.text(), "(BinaryExpression \"/\""), size_t(2));
}

TEST(parser, generic_arrow_functions)
{
  Parsed p("const f = <T>(x: T): T => x; const g = async <T>(x: T) => x; const h = (a: "
           "(b: number) => void) => a;");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(ArrowFunction"), size_t(3));
  CHECK(p.text().find("(FunctionType") != std::string::npos);
}

TEST(parser, function_and_constructor_types)
{
  Parsed p(
      "type A = (this: Foo, ...args: unknown[]) => void; type B = new (x: number) => "
      "Foo; "
      "type C = abstract new () => Foo; type D = <T>(x: T) => T; type E = (number);");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(FunctionType"), size_t(2));
  CHECK_EQ(count(p.text(), "(ConstructorType"), size_t(2));
  CHECK(p.text().find("(ConstructorType : abstract") != std::string::npos);
  CHECK(p.text().find("(ParenthesizedType") != std::string::npos);
  CHECK(p.text().find("(KeywordType") != std::string::npos);
}

TEST(parser, conditional_and_indexed_types)
{
  Parsed p("type A<T> = T extends (infer U)[] ? U : never; type B = T[K][]; "
           "type C<T> = T extends [infer H extends string, ...infer R] ? H : T;");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(ConditionalType"), size_t(2));
  CHECK(p.text().find("(IndexedAccessType") != std::string::npos);
  CHECK(p.text().find("(ArrayType") != std::string::npos);
  CHECK_EQ(count(p.text(), "(InferType"), size_t(3));
}

TEST(parser, type_member_modifiers_and_computed_names)
{
  Parsed p("interface I { readonly a: number; [key: string]: unknown; "
           "[Symbol.iterator](): Iterator<T>; "
           "b?(): void; new (): I; }");
  CHECK(p.ok());
  CHECK(p.text().find("(PropertySignature : readonly") != std::string::npos);
  CHECK(p.text().find("(IndexSignature") != std::string::npos);
  CHECK(p.text().find("(ConstructSignature") != std::string::npos);
  CHECK_EQ(count(p.text(), "(MethodSignature"), size_t(2));
}

TEST(parser, class_members_async_overloads_and_heritage_arguments)
{
  Parsed p("class C<T> extends Base<T> implements I<T> { async load(): Promise<void> {} "
           "get(x: number): void; get(x: string): void; get(x: unknown) {} }");
  CHECK(p.ok());
  CHECK(p.text().find("(MethodDeclaration : async") != std::string::npos);
  CHECK_EQ(count(p.text(), "(MethodDeclaration"), size_t(4));
  CHECK_EQ(count(p.text(), "(Block : missing"), size_t(2));
}

TEST(parser, keyword_member_access)
{
  Parsed p("a.default.delete(); import.meta.url; obj.if;");
  CHECK(p.ok());
  // `import.meta` is a MetaProperty; the other four are member accesses.
  CHECK_EQ(count(p.text(), "(PropertyAccessExpression"), size_t(4));
  CHECK(p.text().find("(MetaProperty") != std::string::npos);
}

TEST(parser, using_declarations)
{
  Parsed p("using a = open();\n"
           "await using b = open();\n"
           "for (using x of xs) {}\n"
           "for (await using y of ys) {}\n"
           "for await (const z of zs) {}\n");
  CHECK(p.ok());
  // A `using` list is a VariableDeclarationList carrying the flag, not a
  // separate node kind; `await using` adds the await flag.
  CHECK_EQ(count(p.text(), "(VariableDeclarationList : using \"using\""), size_t(2));
  CHECK_EQ(count(p.text(), "(VariableDeclarationList : using await \"await\" \"using\""),
           size_t(2));
  CHECK_EQ(count(p.text(), "(VariableStatement \";\""), size_t(2));
  CHECK_EQ(count(p.text(), "(ForOfStatement"), size_t(3));
  // The list of a `using` loop head is a child of the loop.
  size_t loop = p.text().find("(ForOfStatement");
  size_t list = p.text().find("(VariableDeclarationList : using \"using\"", loop);
  CHECK(list != std::string::npos);
  CHECK(p.text().find("(Identifier \"x\"", list) < p.text().find("(Identifier \"xs\""));
  // `for await` flags the loop itself rather than its declaration list.
  CHECK_EQ(count(p.text(), "(ForOfStatement : await \"for\" \"await\""), size_t(1));
  CHECK(p.text().find("(VariableDeclarationList : const \"const\"") != std::string::npos);

  // `using` stays an identifier when no binding name follows on the line.
  Parsed q("using = 1; using(x); using\nz = 2; for (using of xs) {} for (using in o) {}");
  CHECK(q.ok());
  CHECK(q.text().find(": using") == std::string::npos);
  CHECK_EQ(count(q.text(), "(Identifier \"using\""), size_t(5));
  CHECK_EQ(count(q.text(), "(ExpressionStatement"), size_t(4));
  CHECK_EQ(count(q.text(), "(ForOfStatement"), size_t(1));
  CHECK_EQ(count(q.text(), "(ForInStatement"), size_t(1));

  // `await` followed by anything but `using` is an await expression.
  Parsed r("async function f() { await using; await x; }");
  CHECK(r.ok());
  CHECK_EQ(count(r.text(), "(AwaitExpression"), size_t(2));
  CHECK(r.text().find("(VariableDeclarationList") == std::string::npos);
}

TEST(parser, class_decorators)
{
  Parsed p("@dec class A {}\n"
           "@dec() class B {}\n"
           "@a.b.c(1)(2) class C {}\n"
           "@(expr) class D {}\n"
           "@x @y class E {}\n");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(ClassDeclaration"), size_t(5));
  CHECK_EQ(count(p.text(), "(Decorator"), size_t(6));
  // Decorators precede the class name among the class's children.
  size_t classA = p.text().find("(ClassDeclaration");
  size_t decoratorA = p.text().find("(Decorator \"@\"", classA);
  size_t nameA = p.text().find("(Identifier \"A\"");
  CHECK(classA < decoratorA);
  CHECK(decoratorA < nameA);
  // `@dec()`: the decorator's child is a call.
  size_t classB = p.text().find("(ClassDeclaration", nameA);
  size_t callB = p.text().find("(CallExpression", classB);
  CHECK(callB < p.text().find("(Identifier \"B\""));
  // `@a.b.c(1)(2)`: a call chain over a member chain.
  size_t classC = p.text().find("(ClassDeclaration", callB);
  size_t nameC = p.text().find("(Identifier \"C\"");
  CHECK_EQ(count(p.text().substr(classC, nameC - classC), "(CallExpression"), size_t(2));
  CHECK_EQ(count(p.text().substr(classC, nameC - classC), "(PropertyAccessExpression"),
           size_t(2));
  // `@(expr)`: any parenthesized expression.
  size_t classD = p.text().find("(ClassDeclaration", nameC);
  CHECK(p.text().find("(ParenthesizedExpression", classD) <
        p.text().find("(Identifier \"D\""));
  // `@x @y`: both decorators land on the same class, in order.
  size_t classE = p.text().find("(ClassDeclaration", classD + 1);
  size_t decoratorX = p.text().find("(Identifier \"x\"", classE);
  size_t decoratorY = p.text().find("(Identifier \"y\"", classE);
  CHECK(decoratorX < decoratorY);
  CHECK(decoratorY < p.text().find("(Identifier \"E\""));

  // A class expression takes decorators too.
  Parsed q("const K = @dec class {};");
  CHECK(q.ok());
  CHECK(q.text().find("(ClassExpression") != std::string::npos);
  CHECK(q.text().find("(Decorator") != std::string::npos);
}

TEST(parser, decorators_around_export)
{
  Parsed p("export @dec class F {}\n"
           "@dec export class G {}\n"
           "export default @dec class H {}\n"
           "@dec export default abstract class I {}\n");
  CHECK(p.ok());
  // Whichever side of `export` the decorators sit on, the class nests in
  // the ExportDeclaration and owns its decorators.
  CHECK_EQ(count(p.text(), "(ExportDeclaration : exported"), size_t(4));
  CHECK_EQ(count(p.text(), "(ExportDeclaration : exported default"), size_t(2));
  CHECK_EQ(count(p.text(), "(ClassDeclaration"), size_t(4));
  CHECK_EQ(count(p.text(), "(Decorator"), size_t(4));
  for (std::string_view name : {"\"F\"", "\"G\"", "\"H\"", "\"I\""}) {
    size_t at = p.text().find(std::string("(Identifier ") + std::string(name));
    CHECK(at != std::string::npos);
    size_t classAt = p.text().rfind("(ClassDeclaration", at);
    size_t exportAt = p.text().rfind("(ExportDeclaration", at);
    size_t decoratorAt = p.text().rfind("(Decorator", at);
    CHECK(exportAt < classAt);
    CHECK(classAt < decoratorAt);
  }
  // `@dec export`: the `export` keyword is a loose token of the class node.
  CHECK(p.text().find("(ClassDeclaration \"export\" \"class\"") != std::string::npos);
  CHECK(
      p.text().find("(ClassDeclaration : abstract \"export\" \"default\" \"abstract\"") !=
      std::string::npos);
}

TEST(parser, member_and_parameter_decorators)
{
  Parsed p("class M {\n"
           "  @log method() {}\n"
           "  @observable prop = 1;\n"
           "  @bound accessor acc = 2;\n"
           "  @a @b static get g() { return 1; }\n"
           "  @readonly private readonly ro = 3;\n"
           "  constructor(@inject(T) private x: number, @opt y?: string) {}\n"
           "}\n");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(Decorator"), size_t(8));
  // Each member owns its decorators, which precede the modifiers and name.
  auto decoratedBy = [&](std::string_view member, std::string_view decorator) {
    size_t at = p.text().find(member);
    CHECK(at != std::string::npos);
    size_t next = p.text().find("\n    (", at + 1); // next member at class depth
    size_t dec = p.text().find(std::string("(Decorator \"@\"\n        (Identifier \"") +
                                   std::string(decorator) + "\"",
                               at);
    CHECK(dec != std::string::npos);
    CHECK(dec < next);
  };
  decoratedBy("(MethodDeclaration", "log");
  decoratedBy("(PropertyDeclaration \"=\"", "observable");
  decoratedBy("(PropertyDeclaration : accessor", "bound");
  decoratedBy("(GetAccessor : static", "a");
  decoratedBy("(GetAccessor : static", "b");
  decoratedBy("(PropertyDeclaration : readonly private", "readonly");
  // Parameter decorators sit inside the Parameter node.
  size_t paramX = p.text().find("(Parameter : private");
  CHECK(paramX != std::string::npos);
  size_t inject = p.text().find("(Decorator \"@\" \"inject\"", paramX);
  CHECK(inject != std::string::npos);
  CHECK(inject < p.text().find("(Identifier \"x\""));
  CHECK(p.text().find("(CallExpression", inject) < p.text().find("(Identifier \"x\""));
  size_t paramY = p.text().find("(Parameter : optional");
  CHECK(paramY != std::string::npos);
  CHECK(p.text().find("(Identifier \"opt\"", paramY) <
        p.text().find("(Identifier \"y\""));
}

TEST(parser, enum_declarations)
{
  Parsed p("enum E { A, B = 2, C = A | B, 'd', [k], }\n"
           "const enum F { X = 'x' }\n"
           "declare enum G { Y }\n"
           "declare const enum H { Z }\n");
  CHECK(p.ok());
  // Members are direct children of the declaration, after the name.
  CHECK_EQ(count(p.text(), "(EnumDeclaration"), size_t(4));
  CHECK_EQ(count(p.text(), "(EnumMember"), size_t(8));
  CHECK(p.text().find(
            "(EnumDeclaration \"enum\" \"{\" \",\" \",\" \",\" \",\" \",\" \"}\"\n"
            "    (Identifier \"E\"") != std::string::npos);
  size_t memberA = p.text().find("(EnumMember\n      (Identifier \"A\"");
  size_t memberB = p.text().find("(EnumMember \"=\"\n      (Identifier \"B\"");
  size_t memberC = p.text().find("(EnumMember \"=\"\n      (Identifier \"C\"");
  CHECK(memberA < memberB);
  CHECK(memberB < memberC);
  CHECK(p.text().find("(BinaryExpression \"|\"", memberC) != std::string::npos);
  CHECK(p.text().find("(EnumMember\n      (StringLiteral \"'d'\"") != std::string::npos);
  CHECK(p.text().find("(EnumMember\n      (ComputedPropertyName \"[\" \"]\"\n"
                      "        (Identifier \"k\"") != std::string::npos);
  // Modifiers are flags and their tokens belong to the declaration.
  CHECK(p.text().find("(EnumDeclaration : const \"const\" \"enum\"") !=
        std::string::npos);
  CHECK(p.text().find("(EnumDeclaration : ambient \"declare\" \"enum\"") !=
        std::string::npos);
  CHECK(
      p.text().find("(EnumDeclaration : ambient const \"declare\" \"const\" \"enum\"") !=
      std::string::npos);
  CHECK(p.text().find("(SourceFile\n") == 0);
}

TEST(parser, namespace_and_module_declarations)
{
  Parsed p("namespace N { export const a = 1; function f() {} export class C {} }\n"
           "namespace A.B.C { export let x; }\n"
           "module Legacy { const y = 1; }\n"
           "declare namespace D { const z: number; }\n"
           "declare module 'm' { export function g(): void; }\n"
           "declare global { interface Window { fl: number; } }\n");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(ModuleDeclaration"), size_t(6));
  // Runtime members inside a namespace body parse as ordinary statements.
  size_t nsN = p.text().find("(ModuleDeclaration \"namespace\"\n    (Identifier \"N\"");
  CHECK(nsN != std::string::npos);
  size_t nsA = p.text().find("(ModuleDeclaration \"namespace\" \".\" \".\"");
  CHECK(nsA != std::string::npos);
  std::string bodyN = p.text().substr(nsN, nsA - nsN);
  CHECK_EQ(count(bodyN, "(ExportDeclaration : exported \"export\""), size_t(2));
  CHECK_EQ(count(bodyN, "(VariableStatement \";\""), size_t(1));
  CHECK_EQ(count(bodyN, "(FunctionDeclaration"), size_t(1));
  CHECK_EQ(count(bodyN, "(ClassDeclaration"), size_t(1));
  // A dotted name lists each segment before the body.
  size_t segmentA = p.text().find("(Identifier \"A\"", nsA);
  size_t segmentB = p.text().find("(Identifier \"B\"", nsA);
  size_t segmentC = p.text().find("(Identifier \"C\"", nsA);
  size_t bodyA = p.text().find("(Block", nsA);
  CHECK(segmentA < segmentB);
  CHECK(segmentB < segmentC);
  CHECK(segmentC < bodyA);
  CHECK(p.text().find("(ModuleDeclaration \"module\"\n    (Identifier \"Legacy\"") !=
        std::string::npos);
  CHECK(p.text().find("(ModuleDeclaration : ambient \"declare\" \"namespace\"") !=
        std::string::npos);
  CHECK(p.text().find("(ModuleDeclaration : ambient \"declare\" \"module\"\n"
                      "    (StringLiteral \"'m'\"") != std::string::npos);
  CHECK(p.text().find(
            "(ModuleDeclaration : ambient \"declare\"\n    (Identifier \"global\"") !=
        std::string::npos);
  CHECK(p.text().find("(InterfaceDeclaration") != std::string::npos);

  // `namespace`/`module` as plain identifiers.
  Parsed q("namespace = 1; module.exports = x; namespace\nfoo();");
  CHECK(q.ok());
  CHECK(q.text().find("(ModuleDeclaration") == std::string::npos);
  CHECK_EQ(count(q.text(), "(ExpressionStatement"), size_t(4));
}

TEST(parser, parameter_properties)
{
  Parsed p(
      "class P {\n"
      "  constructor(public a: number, private readonly b = 1, protected c?: string,\n"
      "              override readonly d: T, public override e: U, f: number) {}\n"
      "}\n");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(Parameter"), size_t(6));
  // Modifiers are flags on the Parameter, with their tokens owned by it.
  CHECK(
      p.text().find("(Parameter : public \"public\" \":\"\n        (Identifier \"a\"") !=
      std::string::npos);
  CHECK(p.text().find("(Parameter : readonly private \"private\" \"readonly\" \"=\"\n"
                      "        (Identifier \"b\"\n") != std::string::npos);
  CHECK(p.text().find("(NumericLiteral \"1\"", p.text().find("(Identifier \"b\"")) !=
        std::string::npos);
  CHECK(p.text().find("(Parameter : optional protected \"protected\" \"?\" \":\"\n"
                      "        (Identifier \"c\"") != std::string::npos);
  CHECK(p.text().find("(Parameter : readonly override \"override\" \"readonly\" \":\"\n"
                      "        (Identifier \"d\"") != std::string::npos);
  CHECK(p.text().find("(Parameter : override public \"public\" \"override\" \":\"\n"
                      "        (Identifier \"e\"") != std::string::npos);
  CHECK(p.text().find("(Parameter \":\"\n        (Identifier \"f\"") !=
        std::string::npos);

  // The modifier words are ordinary parameter names when no binding follows.
  Parsed q("function f(readonly, override: number, readonly = 1) {}");
  CHECK(q.ok());
  CHECK(q.text().find(" : ") == std::string::npos);
  CHECK_EQ(count(q.text(), "(Parameter"), size_t(3));
  CHECK_EQ(count(q.text(), "(Identifier \"readonly\""), size_t(2));
  CHECK(q.text().find("(Identifier \"override\"") != std::string::npos);
}

TEST(parser, import_and_export_assignments)
{
  Parsed p("import fs = require('fs');\n"
           "import X = A.B.C;\n"
           "export import Y = A.B;\n"
           "export = foo;\n"
           "export as namespace Lib;\n");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(ImportEqualsDeclaration \"import\" \"=\" \";\""), size_t(3));
  // `require(…)` is an ExternalModuleReference holding the specifier.
  size_t fs = p.text().find("(Identifier \"fs\"");
  size_t require = p.text().find("(ExternalModuleReference \"require\" \"(\" \")\"\n"
                                 "      (StringLiteral \"'fs'\"");
  CHECK(fs < require);
  // An entity name alias is a qualified name.
  size_t x = p.text().find("(Identifier \"X\"");
  size_t abc = p.text().find("(QualifiedName \".\" \".\"\n      (Identifier \"A\"");
  CHECK(x < abc);
  CHECK(abc < p.text().find("(Identifier \"Y\""));
  // `export import` nests the alias in the export.
  CHECK(p.text().find("(ExportDeclaration : exported \"export\"\n    "
                      "(ImportEqualsDeclaration") != std::string::npos);
  CHECK(p.text().find(
            "(ExportAssignment \"export\" \"=\" \";\"\n    (Identifier \"foo\"") !=
        std::string::npos);
  CHECK(
      p.text().find("(NamespaceExportDeclaration \"export\" \"as\" \"namespace\" \";\"\n"
                    "    (Identifier \"Lib\"") != std::string::npos);
  // None of these leave stray tokens on the file node.
  CHECK(p.text().find("(SourceFile\n") == 0);

  // `export default` of an expression is an ExportDeclaration, not an assignment.
  Parsed q("export default foo;");
  CHECK(q.ok());
  CHECK(q.text().find("(ExportDeclaration : exported default") != std::string::npos);
  CHECK(q.text().find("(ExportAssignment") == std::string::npos);
}

TEST(parser, type_assertions_and_other_non_erasable_syntax)
{
  Parsed p("const x = <T>y;\n"
           "const z = <T[]>(y);\n"
           "const w = <const>['a'];\n"
           "abstract class Q { abstract m(): void; abstract p: number; }\n"
           "declare abstract class R {}\n");
  CHECK(p.ok());
  // The angle-bracket assertion owns its type and operand.
  CHECK_EQ(count(p.text(), "(TypeAssertionExpression \"<\" \">\""), size_t(3));
  CHECK(p.text().find("(TypeAssertionExpression \"<\" \">\"\n          (TypeReference\n"
                      "            (Identifier \"T\"\n            )\n          )\n"
                      "          (Identifier \"y\"") != std::string::npos);
  CHECK(p.text().find("(TypeAssertionExpression \"<\" \">\"\n          (ArrayType") !=
        std::string::npos);
  // `<const>` is a reference to the `const` marker type, as in `as const`.
  CHECK(p.text().find("(TypeAssertionExpression \"<\" \">\"\n          (TypeReference\n"
                      "            (Identifier \"const\"") != std::string::npos);
  // Abstract classes and members carry the flag and own the keyword.
  CHECK(p.text().find("(ClassDeclaration : abstract \"abstract\" \"class\"") !=
        std::string::npos);
  CHECK(p.text().find("(MethodDeclaration : abstract \"abstract\"") != std::string::npos);
  CHECK(p.text().find("(PropertyDeclaration : abstract \"abstract\"") !=
        std::string::npos);
  CHECK(p.text().find("(ClassDeclaration : ambient abstract \"declare\" \"abstract\" "
                      "\"class\"") != std::string::npos);
  CHECK(p.text().find("(SourceFile\n") == 0);

  // `<T>` before an arrow parameter list is a generic arrow, not an assertion.
  Parsed q("const f = <T>(a: T) => a;");
  CHECK(q.ok());
  CHECK(q.text().find("(ArrowFunction") != std::string::npos);
  CHECK(q.text().find("(TypeAssertionExpression") == std::string::npos);
}

TEST(parser, export_star_forms)
{
  Parsed p(
      "export * from './a';\nexport * as ns from './b';\nimport * as m from './c';\n");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(ExportDeclaration : exported \"export\""), size_t(2));
  // `export *` without an alias owns its star; only aliased forms get a node.
  CHECK(p.text().find("(ExportDeclaration : exported \"export\" \"*\" \"from\"") !=
        std::string::npos);
  CHECK_EQ(count(p.text(), "(NamespaceExport"), size_t(1));
  CHECK(p.text().find("(NamespaceExport \"*\" \"as\"\n      (Identifier \"ns\"") !=
        std::string::npos);
  CHECK(p.text().find("(NamespaceImport \"*\" \"as\"\n        (Identifier \"m\"") !=
        std::string::npos);
}

TEST(parser, tuple_element_forms)
{
  Parsed p("type A = [string, number?];\n"
           "type B = [(number | undefined)?, ...string[]];\n"
           "type C = [p: T, q?: U, ...rest: V[]];\n");
  CHECK(p.ok());
  // Postfix `?` wraps the element; a leading name makes a NamedTupleMember.
  CHECK_EQ(count(p.text(), "(OptionalType \"?\""), size_t(2));
  CHECK(p.text().find("(OptionalType \"?\"\n        (ParenthesizedType") !=
        std::string::npos);
  CHECK_EQ(count(p.text(), "(RestType \"...\""), size_t(1));
  CHECK_EQ(count(p.text(), "(NamedTupleMember"), size_t(3));
  CHECK(p.text().find(
            "(NamedTupleMember : optional \"?\" \":\"\n        (Identifier \"q\"") !=
        std::string::npos);
  CHECK(p.text().find(
            "(NamedTupleMember : rest \"...\" \":\"\n        (Identifier \"rest\"") !=
        std::string::npos);
}

TEST(parser, bigint_keyword_and_literals)
{
  Parsed p("let a: bigint = 1n;\n"
           "function bigint<T = unknown>(x: T) {}\n"
           "type S = `${bigint}.${bigint}`;\n"
           "type L = 2n | -1n;\n");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(KeywordType \"bigint\""), size_t(3));
  CHECK_EQ(count(p.text(), "(BigIntLiteral \"1n\""), size_t(1));
  CHECK(p.text().find("(FunctionDeclaration \"function\"") != std::string::npos);
  CHECK(p.text().find("(Identifier \"bigint\"") != std::string::npos);
  CHECK(p.text().find("(TemplateLiteralType") != std::string::npos);
  CHECK_EQ(count(p.text(), "(LiteralType"), size_t(2));
}

TEST(parser, class_member_names_after_modifiers)
{
  Parsed p("class C {\n"
           "  get [Symbol.toStringTag](): string { return ''; }\n"
           "  set [k](v: number) {}\n"
           "  reduce?(values: number[]): number;\n"
           "  static [k]: number;\n"
           "  readonly 'quoted' = 1;\n"
           "  private #secret = 2;\n"
           "  static\n"
           "  x = 3;\n"
           "  declare accessor: number;\n"
           "}\n");
  CHECK(p.ok());
  CHECK(p.text().find("(GetAccessor \"get\"") != std::string::npos);
  CHECK(p.text().find("(SetAccessor \"set\"") != std::string::npos);
  CHECK(p.text().find("(MethodDeclaration : optional \"?\"") != std::string::npos);
  CHECK(p.text().find("(PropertyDeclaration : static \"static\" \":\" \";\"\n"
                      "      (ComputedPropertyName") != std::string::npos);
  CHECK(p.text().find("(PropertyDeclaration : readonly \"readonly\"") !=
        std::string::npos);
  CHECK(p.text().find("(PropertyDeclaration : private \"private\" \"=\" \";\"\n"
                      "      (PrivateIdentifier") != std::string::npos);
  // `static` followed by a line break still modifies the next name.
  CHECK(p.text().find("(PropertyDeclaration : static \"static\" \"=\" \";\"\n"
                      "      (Identifier \"x\"") != std::string::npos);
  // `declare accessor: number` names a property `accessor`.
  CHECK(p.text().find("(PropertyDeclaration : ambient \"declare\" \":\" \";\"\n"
                      "      (Identifier \"accessor\"") != std::string::npos);
  CHECK_EQ(count(p.text(), "(ComputedPropertyName"), size_t(3));
}

TEST(parser, import_types_with_qualifiers_and_arguments)
{
  Parsed p("type T = import('./x').Y<2>;\n"
           "type U = import('./x').Y.Z<string, number>;\n"
           "type V = typeof import('./x').default;\n"
           "type W = import('./x');\n");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(ImportType"), size_t(4));
  CHECK(p.text().find("(ImportType \"import\" \"(\" \")\" \".\"\n      (LiteralType\n"
                      "        (StringLiteral \"'./x'\"\n        )\n      )\n"
                      "      (Identifier \"Y\"\n      )\n"
                      "      (TypeArguments \"<\" \">\"") != std::string::npos);
  CHECK(p.text().find("(QualifiedName \".\"\n        (Identifier \"Y\"\n        )\n"
                      "        (Identifier \"Z\"") != std::string::npos);
  CHECK(p.text().find("(Identifier \"default\"") != std::string::npos);
  CHECK(p.text().find("(ImportType \"typeof\" \"import\" \"(\" \")\"") !=
        std::string::npos);
  CHECK(p.text().find("(ImportType \"import\" \"(\" \")\"\n      (LiteralType\n"
                      "        (StringLiteral \"'./x'\"\n        )\n      )\n    )") !=
        std::string::npos);
}

TEST(parser, global_augmentation_in_ambient_module)
{
  Parsed p("declare module 'm' {\n  global {\n    interface W { fl: number; }\n  }\n}\n");
  CHECK(p.ok());
  CHECK(p.text().find("(ModuleDeclaration\n        (Identifier \"global\"") !=
        std::string::npos);
  CHECK(p.text().find("(InterfaceDeclaration") != std::string::npos);
}

TEST(parser, object_literal_async_methods)
{
  Parsed p("const o = { async run(props, ctx) { await x; }, async *gen() { yield 1; },"
           " async: 1, async() {}, async [k]() {}, get async() { return 1; } };");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(MethodDeclaration : async \"async\""), size_t(2));
  CHECK(p.text().find("(MethodDeclaration : async generator \"async\" \"*\"") !=
        std::string::npos);
  CHECK(p.text().find("(AwaitExpression") != std::string::npos);
  CHECK(p.text().find("(YieldExpression") != std::string::npos);
  // `async` as a plain name: a property, a method and a getter.
  CHECK(p.text().find("(PropertyAssignment \":\"\n            (Identifier \"async\"") !=
        std::string::npos);
  CHECK(p.text().find(
            "(MethodDeclaration \"(\" \")\"\n            (Identifier \"async\"") !=
        std::string::npos);
  CHECK(p.text().find("(GetAccessor \"get\"") != std::string::npos);
  CHECK_EQ(count(p.text(), "(MethodDeclaration"), size_t(4));
}

TEST(parser, mapped_type_modifiers)
{
  Parsed p("type M = { [k in K | (string & {})]?: string };\n"
           "type N = { -readonly [P in keyof T]: T[P] };\n"
           "type O = { +readonly [P in keyof T]+?: T[P] };\n"
           "type P = { readonly [P in keyof T as `get${P}`]-?: T[P]; };\n");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(MappedType"), size_t(4));
  // The key parameter owns `in`; the mapped type owns the brackets and modifiers.
  CHECK(p.text().find("(MappedType : optional \"{\" \"[\" \"]\" \"?\" \":\" \"}\"") !=
        std::string::npos);
  CHECK(p.text().find("(MappedType : readonly \"{\" \"-\" \"readonly\"") !=
        std::string::npos);
  CHECK(p.text().find(
            "(MappedType : readonly optional \"{\" \"+\" \"readonly\" \"[\" \"]\" "
            "\"+\" \"?\" \":\" \"}\"") != std::string::npos);
  CHECK(p.text().find(
            "(MappedType : readonly optional \"{\" \"readonly\" \"[\" \"as\" \"]\" "
            "\"-\" \"?\" \":\" \";\" \"}\"") != std::string::npos);
  CHECK_EQ(count(p.text(), "(TypeParameter \"in\"\n        (Identifier \"P\""),
           size_t(3));
  CHECK(p.text().find("(TemplateLiteralType") != std::string::npos);
  CHECK_EQ(count(p.text(), "(TypeParameter"), size_t(4));
}

TEST(parser, type_only_imports_and_accessor_name)
{
  Parsed p("import type resolve = require('../index.js');\n"
           "import type { A } from './a';\n"
           "import type B from './b';\n"
           "import type from './c';\n"
           "export type { A };\n"
           "declare function accessor(): boolean;\n"
           "function f(accessor: number) { const { accessor: a } = x; }\n");
  CHECK(p.ok());
  CHECK(p.text().find("(ImportEqualsDeclaration : type-only \"import\" \"type\" \"=\"") !=
        std::string::npos);
  CHECK_EQ(count(p.text(), "(ImportDeclaration : type-only"), size_t(2));
  // `import type from './c'` binds a default import named `type`.
  CHECK(p.text().find("(ImportDeclaration \"import\" \"from\" \";\"\n    (ImportClause\n"
                      "      (Identifier \"type\"") != std::string::npos);
  CHECK(p.text().find("(ExportDeclaration : exported type-only") != std::string::npos);
  CHECK(
      p.text().find("(FunctionDeclaration : ambient \"declare\" \"function\" \"(\" \")\" "
                    "\":\" \";\"\n    (Identifier \"accessor\"") != std::string::npos);
  CHECK_EQ(count(p.text(), "(Identifier \"accessor\""), size_t(3));
}

TEST(parser, call_and_construct_signatures_in_type_literals)
{
  Parsed p("declare const makeDir: {\n"
           "  (path: string, options?: number): Promise<string>;\n"
           "  <T>(x: T): T,\n"
           "  new (x: number): I;\n"
           "  new <T>(): T;\n"
           "  sync(path: string): string;\n"
           "};\n"
           "interface I { (minor?: boolean): void; new (x: number): I; }\n");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(CallSignature"), size_t(3));
  CHECK_EQ(count(p.text(), "(ConstructSignature"), size_t(3));
  CHECK_EQ(count(p.text(), "(MethodSignature"), size_t(1));
  CHECK(p.text().find(
            "(CallSignature \"(\" \")\" \":\" \",\"\n            (TypeParameters") !=
        std::string::npos);
  CHECK(p.text().find("(ConstructSignature \"new\" \"(\" \")\" \":\" \";\"\n            "
                      "(TypeParameters") != std::string::npos);
}

TEST(parser, new_target_and_export_default_interface)
{
  Parsed p("function f() { return new.target; }\n"
           "export default interface X { a: number; }\n"
           "const y = new.target.name;\n");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(MetaProperty \"new\" \".\""), size_t(2));
  CHECK(p.text().find("(MetaProperty \"new\" \".\"\n          (Identifier \"target\"") !=
        std::string::npos);
  CHECK(p.text().find("(ExportDeclaration : exported default \"export\" \"default\"\n    "
                      "(InterfaceDeclaration") != std::string::npos);
  CHECK(p.text().find("(PropertyAccessExpression \".\"\n          (MetaProperty") !=
        std::string::npos);
}

TEST(parser, conditional_return_type_inside_extends_operand)
{
  // The extends operand may not itself be conditional, but a function type's
  // return type inside it may.
  Parsed p("type Eq<T, U> = (<V>() => V extends T ? 1 : 2) extends "
           "<V>() => V extends U ? 1 : 2 ? true : false;\n");
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(ConditionalType"), size_t(3));
  CHECK_EQ(count(p.text(), "(FunctionType"), size_t(2));
  size_t outer = p.text().find("(ConditionalType");
  size_t trueBranch = p.text().find("(LiteralType\n          (TrueLiteral");
  CHECK(outer < trueBranch);
}

// ------------------------------------------------------------------- JSX

static Parser::Options jsxOptions()
{
  Parser::Options options;
  options.jsx = true;
  return options;
}

TEST(parser, jsx_element_attributes_and_children)
{
  Parsed p("const a = <div className=\"x\" {...p} b={1} c>hi {x} <br/></div>;\n",
           jsxOptions());
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(JsxElement"), size_t(1));
  CHECK_EQ(count(p.text(), "(JsxOpeningElement"), size_t(1));
  CHECK_EQ(count(p.text(), "(JsxClosingElement"), size_t(1));
  CHECK_EQ(count(p.text(), "(JsxAttribute "), size_t(2)); // the `=` ones
  CHECK_EQ(count(p.text(), "(JsxAttribute\n"), size_t(1));
  CHECK_EQ(count(p.text(), "(JsxSpreadAttribute"), size_t(1));
  CHECK_EQ(count(p.text(), "(JsxSelfClosingElement"), size_t(1));
  // "hi ", " " between `{x}` and `<br/>`: whitespace-only runs are kept.
  CHECK_EQ(count(p.text(), "(JsxText"), size_t(2));
  CHECK_EQ(count(p.text(), "(JsxExpression"), size_t(2));
  // The self-closing `<br/>` still owns an (empty) attribute list.
  CHECK_EQ(count(p.text(), "(JsxAttributes"), size_t(2));
}

TEST(parser, jsx_fragment_names_and_type_arguments)
{
  Parsed p("const a = <><A.B<T> d=\"1\"/><ns:tag/><this.x/></>;\n", jsxOptions());
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(JsxFragment"), size_t(1));
  CHECK_EQ(count(p.text(), "(JsxOpeningFragment"), size_t(1));
  CHECK_EQ(count(p.text(), "(JsxClosingFragment"), size_t(1));
  CHECK_EQ(count(p.text(), "(JsxNamespacedName"), size_t(1));
  CHECK_EQ(count(p.text(), "(PropertyAccessExpression"), size_t(2));
  CHECK_EQ(count(p.text(), "(ThisExpression"), size_t(1));
  CHECK_EQ(count(p.text(), "(TypeArguments"), size_t(1));
}

TEST(parser, jsx_generic_arrow_versus_element)
{
  Parsed p("const f = <T,>(x: T) => x;\nconst g = <T extends U>(x: T) => x;\n"
           "const h = <T>text</T>;\n",
           jsxOptions());
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(ArrowFunction"), size_t(2));
  CHECK_EQ(count(p.text(), "(JsxElement"), size_t(1));
  CHECK_EQ(count(p.text(), "(TypeAssertionExpression"), size_t(0));
}

TEST(parser, jsx_multiline_attribute_string_and_hyphenated_names)
{
  Parsed p("const a = <my-el data-x=\"\nfoo: 1\n\" />;\n", jsxOptions());
  CHECK(p.ok());
  CHECK_EQ(count(p.text(), "(JsxSelfClosingElement"), size_t(1));
  CHECK_EQ(count(p.text(), "(StringLiteral"), size_t(1));
}

TEST(parser, jsx_unclosed_element_reports_and_stops)
{
  Parsed p("const a = <div>text", jsxOptions());
  CHECK(!p.ok());
  CHECK_EQ(count(p.text(), "(JsxElement"), size_t(1));
  CHECK_EQ(count(p.text(), "(JsxText"), size_t(1));
}
