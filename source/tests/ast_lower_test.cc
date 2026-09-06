#include "fastlint/ast/dump.h"
#include "fastlint/ast/file.h"
#include "fastlint/ast/generated/views.h"
#include "fastlint/ast/kind_info.h"
#include "fastlint/ast/lower.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "test_sources.h"
#include "testing/fixture.h"
#include "testing/snapshot.h"
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

  explicit Lowered(std::string_view source, syntax::Parser::Options options = {})
      : file(&tree)
  {
    syntax::Parser parser(source, options, diagnostics);
    parser.parseFile(tree);
    root = lower(tree, file);
  }

  std::string dump() const
  {
    litestl::util::string out;
    dumpAst(root, out);
    return std::string(out.c_str());
  }
};

/** Every structural rule a lowered node must satisfy; returns the first violation. */
std::string checkInvariants(const Node *n, const Node *parent)
{
  const KindInfo &info = kindInfo(n->kind);
  std::string where = std::string(info.name) + " @" + std::to_string(n->start) + "-" +
                      std::to_string(n->end) + ": ";
  if (n->parent != parent) {
    return where + "parent link is wrong";
  }
  size_t count = n->children.size();
  if (count < info.fixedChildren) {
    return where + "fewer children than fixed slots";
  }
  if (!info.hasList && count != info.fixedChildren) {
    return where + "extra children on a kind without a list";
  }
  if (n->end < n->start) {
    return where + "span ends before it starts";
  }
  for (size_t i = 0; i < count; i++) {
    const Node *c = n->children[int(i)];
    if (!c) {
      bool fixed = i < info.fixedChildren;
      bool required = fixed && (info.requiredMask & (1u << i)) != 0;
      if (required && !n->hasFlag(Flag::Incomplete)) {
        return where + "required slot " + info.childNames[i] + " is null";
      }
      if (!fixed && !info.nullableElements) {
        return where + "null element in a non-nullable list";
      }
      continue;
    }
    if (c->end > c->start && (c->start < n->start || c->end > n->end)) {
      return where + "child " + kindInfo(c->kind).name + " lies outside the parent span";
    }
    std::string inner = checkInvariants(c, n);
    if (!inner.empty()) {
      return inner;
    }
  }
  return {};
}

size_t countNodes(const Node *n)
{
  size_t total = 1;
  for (const Node *c : n->children) {
    if (c) {
      total += countNodes(c);
    }
  }
  return total;
}

} // namespace

TEST(ast_lower, fixtures)
{
  test::forEachFile("tests/fixtures/ast", ".ts", [&](const test::Fixture &fixture) {
    Lowered l(std::string_view(fixture.text.c_str(), fixture.text.size()));
    CHECK_EQ(checkInvariants(l.root, nullptr), std::string());
    SNAPSHOT(l.dump());
  });
}

TEST(ast_lower, spans_cover_annotations)
{
  Lowered l("function f(a?: number = 1, ...r: string[]): void {}");
  FunctionLike fn = l.root->children[0]->as<FunctionLike>();
  CHECK(bool(fn));
  CHECK_EQ(int(fn.params().size()), 2);
  // `a?: number = 1` is an AssignmentPattern whose left Identifier spans its annotation.
  AssignmentPattern def = fn.params()[0]->as<AssignmentPattern>();
  CHECK(bool(def));
  Identifier a = def.left()->as<Identifier>();
  CHECK(bool(a));
  CHECK_EQ(std::string(a.text()), "a");
  CHECK(a.isOptional());
  CHECK(a.typeAnnotation() != nullptr);
  CHECK_EQ(int(a.node()->start), 11);
  CHECK_EQ(int(a.node()->end), 21);
  RestElement rest = fn.params()[1]->as<RestElement>();
  CHECK(bool(rest));
  CHECK(rest.typeAnnotation() != nullptr);
  CHECK(fn.returnType() != nullptr);
  CHECK(fn.body() != nullptr);
}

TEST(ast_lower, call_ranges_and_optional_chains)
{
  Lowered l("a?.b?.(c);");
  CallExpression call = l.root->children[0]->children[0]->as<CallExpression>();
  CHECK(bool(call));
  CHECK(call.isOptional());
  CHECK_EQ(int(call.node()->start), 0);
  CHECK_EQ(int(call.node()->end), 9);
  MemberExpression member = call.callee()->as<MemberExpression>();
  CHECK(bool(member));
  CHECK(member.isOptional());
  CHECK(!member.isComputed());
  CHECK_EQ(int(call.arguments().size()), 1);
}

TEST(ast_lower, sequence_flattens_and_parens_are_a_flag)
{
  Lowered l("(a, b, c);");
  Node *expr = l.root->children[0]->children[0];
  CHECK(expr->kind == NodeKind::SequenceExpression);
  CHECK(expr->hasFlag(Flag::Parenthesized));
  CHECK_EQ(int(expr->children.size()), 3);
  CHECK_EQ(int(expr->start), 0);
  CHECK_EQ(int(expr->end), 9);
}

TEST(ast_lower, directives_and_source_type)
{
  Lowered script("'use strict';\nfoo();\n'not a directive';");
  CHECK(script.root->as<Program>().sourceType() == SourceType::Script);
  CHECK(script.root->children[0]->hasFlag(Flag::Directive));
  CHECK(!script.root->children[2]->hasFlag(Flag::Directive));

  Lowered module("export const x = 1;");
  CHECK(module.root->as<Program>().sourceType() == SourceType::Module);
}

TEST(ast_lower, recovery_lowers_to_error_leaves)
{
  Lowered l("x = ;\nif (x) ");
  CHECK_EQ(checkInvariants(l.root, nullptr), std::string());
  CHECK_EQ(int(l.root->children.size()), 2);
  AssignmentExpression assign =
      l.root->children[0]->children[0]->as<AssignmentExpression>();
  CHECK(bool(assign));
  CHECK(assign.right()->kind == NodeKind::Error);
  IfStatement ifs = l.root->children[1]->as<IfStatement>();
  CHECK(bool(ifs));
  CHECK(ifs.consequent()->kind == NodeKind::ExpressionStatement);
  CHECK(ifs.consequent()->children[0]->kind == NodeKind::Error);
  CHECK(ifs.alternate() == nullptr);
}

TEST(ast_lower, corpus_invariants)
{
  test::loadTestSources();
  for (const test::TestSource &source : test::tsTestSources) {
    syntax::Parser::Options options;
    std::string path(source.path.c_str());
    options.jsx = path.ends_with(".tsx");
    Lowered l(std::string_view(source.source.c_str(), source.source.size()), options);
    std::string violation = checkInvariants(l.root, nullptr);
    if (!violation.empty()) {
      violation = path + ": " + violation;
    }
    CHECK_EQ(violation, std::string());
    CHECK(countNodes(l.root) > 0);
  }
}
