#include "fastlint/ast/fixpoint.h"
#include "fastlint/ast/generated/views.h"
#include "fastlint/ast/template.h"
#include "testing/test.h"

#include <string>

using namespace fastlint;
using namespace fastlint::ast;

namespace {

std::string sv(const litestl::util::string &s)
{
  return std::string(s.c_str(), s.size());
}

/** Proposes `apply` for every node of `kind` that `when` accepts. */
template <typename When, typename Apply>
void propose(Pass &pass, NodeKind kind, When when, Apply apply)
{
  for (const PreorderEntry &e : pass.file.preorder()) {
    Node *n = e.node;
    if (n->kind == kind && when(n)) {
      pass.fixes.append(Fix{n, [n, apply](Fixer &fixer) { apply(fixer, n); }});
    }
  }
}

} // namespace

TEST(ast_fixpoint, converges_after_one_fixing_pass)
{
  FixpointReport r = runToFixpoint("var a = 1;\nvar b = 2;", [](Pass &pass) {
    propose(
        pass,
        NodeKind::VariableDeclaration,
        [](Node *n) { return VariableKind(n->dataByte(0)) == VariableKind::Var; },
        [&](Fixer &fixer, Node *n) {
          Node *declarator = n->children[0];
          Node *fresh = Template::compile("let $id = $init;")
                            ->instantiate(pass.file,
                                          {{"id", declarator->children[0]},
                                           {"init", declarator->children[1]}});
          fixer.replace(n, fresh);
        });
  });
  CHECK_EQ(sv(r.text), "let a = 1;\nlet b = 2;");
  CHECK_EQ(r.passes, 2);
  CHECK_EQ(r.applied, 2);
  CHECK_EQ(r.deferred, 0);
  CHECK(r.converged);
  CHECK(!r.reverted);
  CHECK(!r.syntaxErrors);
}

TEST(ast_fixpoint, deferred_fix_lands_in_the_next_pass)
{
  FixpointReport r = runToFixpoint("foo();", [](Pass &pass) {
    // Two rules target the same call; the second waits for the reprint.
    propose(
        pass,
        NodeKind::CallExpression,
        [](Node *n) { return n->children[0]->isIdentifier("foo"); },
        [&](Fixer &fixer, Node *n) {
          fixer.replace(n, Template::compile("bar()")->instantiate(pass.file, {}));
        });
    propose(
        pass,
        NodeKind::CallExpression,
        [](Node *n) { return n->children.size() == 2; },
        [](Fixer &fixer, Node *n) { fixer.append(n, fixer.numberLiteral("1")); });
  });
  CHECK_EQ(sv(r.text), "bar(1);");
  CHECK_EQ(r.passes, 3);
  CHECK_EQ(r.applied, 2);
  CHECK_EQ(r.deferred, 1);
  CHECK(r.converged);
}

TEST(ast_fixpoint, stops_at_the_pass_bound)
{
  FixpointOptions options;
  options.maxPasses = 3;
  FixpointReport r = runToFixpoint(
      "a + b;",
      [](Pass &pass) {
        propose(
            pass,
            NodeKind::BinaryExpression,
            [](Node *) { return true; },
            [&](Fixer &fixer, Node *n) {
              Node *fresh = Template::compile("$r + $l")->instantiate(
                  pass.file, {{"l", n->children[0]}, {"r", n->children[1]}});
              fixer.replace(n, fresh);
            });
      },
      options);
  CHECK_EQ(sv(r.text), "b + a;");
  CHECK_EQ(r.passes, 3);
  CHECK_EQ(r.applied, 3);
  CHECK(!r.converged);
}

TEST(ast_fixpoint, unparsable_output_is_reverted)
{
  FixpointReport r = runToFixpoint("a;", [](Pass &pass) {
    propose(
        pass,
        NodeKind::Identifier,
        [](Node *n) { return n->isIdentifier("a"); },
        [](Fixer &fixer, Node *n) { fixer.replace(n, fixer.identifier("if")); });
  });
  CHECK_EQ(sv(r.text), "a;");
  CHECK_EQ(r.passes, 1);
  CHECK_EQ(r.applied, 0);
  CHECK(r.reverted);
  CHECK(!r.converged);
}

TEST(ast_fixpoint, syntax_errors_run_one_pass_without_fixing)
{
  int calls = 0;
  FixpointReport r = runToFixpoint("a +;", [&](Pass &pass) {
    calls++;
    propose(
        pass,
        NodeKind::Identifier,
        [](Node *) { return true; },
        [](Fixer &fixer, Node *n) { fixer.replace(n, fixer.identifier("b")); });
  });
  CHECK_EQ(sv(r.text), "a +;");
  CHECK_EQ(calls, 1);
  CHECK_EQ(r.applied, 0);
  CHECK(r.syntaxErrors);
  CHECK(!r.converged);
}

TEST(ast_fixpoint, passes_see_fresh_bindings)
{
  int declarations = -1;
  int lastIndex = -1;
  FixpointReport r = runToFixpoint("let x = 1;", [&](Pass &pass) {
    lastIndex = pass.index;
    declarations = pass.bindings.declarationCount();
    CHECK(pass.bindings.moduleScope() != nullptr);
    if (pass.index == 0) {
      Node *declarator = pass.file.root()->children[0]->children[0];
      Node *id = declarator->children[0];
      pass.fixes.append(Fix{id, [&pass, declarator](Fixer &fixer) {
                              Node *fresh =
                                  Template::compile("let $a = $init, y = 2;")
                                      ->instantiate(pass.file,
                                                    {{"a", declarator->children[0]},
                                                     {"init", declarator->children[1]}});
                              fixer.replace(declarator->parent, fresh);
                            }});
    }
  });
  CHECK_EQ(sv(r.text), "let x = 1, y = 2;");
  CHECK_EQ(lastIndex, 1);
  CHECK_EQ(declarations, 2);
  CHECK(r.converged);
}
