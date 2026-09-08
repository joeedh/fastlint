// no-unreachable: code after a statement that cannot complete normally
// (docs/rules/no-unreachable.md). Completion analysis is in rules/flow.h.

#include "fastlint/rules/flow.h"
#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;

constexpr Message kMessages[] = {
    {"unreachableCode", "Unreachable code."},
};

/** Whether an unreachable `stmt` is worth a report: hoisted and type-only ones are not.
 */
bool reportable(Node *stmt)
{
  switch (stmt->kind) {
  case NodeKind::FunctionDeclaration:
  case NodeKind::TSDeclareFunction:
  case NodeKind::EmptyStatement:
  case NodeKind::ImportDeclaration:
  case NodeKind::TSInterfaceDeclaration:
  case NodeKind::TSTypeAliasDeclaration:
  case NodeKind::TSModuleDeclaration:
  case NodeKind::TSImportEqualsDeclaration:
    return false;
  case NodeKind::VariableDeclaration: {
    ast::VariableDeclaration view(stmt);
    if (view.kind() != ast::VariableKind::Var) {
      return true;
    }
    for (Node *declarator : view.declarations()) {
      if (ast::VariableDeclarator(declarator).init()) {
        return true;
      }
    }
    return false;
  }
  default:
    return true;
  }
}

void checkList(RuleContext &ctx, span<Node *> list)
{
  size_t exit = list.size();
  for (size_t i = 0; i < list.size(); i++) {
    if (list[i] && !completesNormally(list[i])) {
      exit = i;
      break;
    }
  }
  // Consecutive unreachable statements make one report.
  Node *first = nullptr;
  Node *last = nullptr;
  auto flush = [&]() {
    if (first) {
      Report r;
      r.node = first;
      r.at(first->start, last->end);
      r.messageId = "unreachableCode";
      ctx.report(std::move(r));
    }
    first = last = nullptr;
  };
  for (size_t i = exit + 1; i < list.size(); i++) {
    Node *stmt = list[i];
    if (!stmt || !reportable(stmt)) {
      flush();
      continue;
    }
    if (!first) {
      first = stmt;
    }
    last = stmt;
  }
  flush();
}

void create(RuleContext &ctx)
{
  ctx.on(NodeKind::Program,
         [&ctx](Node *node) { checkList(ctx, ast::Program(node).body()); });
  ctx.on(NodeKind::BlockStatement,
         [&ctx](Node *node) { checkList(ctx, ast::BlockStatement(node).body()); });
  ctx.on(NodeKind::SwitchCase,
         [&ctx](Node *node) { checkList(ctx, ast::SwitchCase(node).consequent()); });
  ctx.on(NodeKind::StaticBlock,
         [&ctx](Node *node) { checkList(ctx, ast::StaticBlock(node).body()); });
  ctx.on(NodeKind::TSModuleBlock,
         [&ctx](Node *node) { checkList(ctx, ast::TSModuleBlock(node).body()); });
}

} // namespace

const RuleDef kNoUnreachable{
    {
        "no-unreachable",
        "Disallow unreachable code after `return`, `throw`, `continue`, and `break` "
        "statements",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/no-unreachable.md",
        /*recommended=*/true,
        /*fixable=*/false,
        /*hasSuggestions=*/false,
        /*typeAware=*/false,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
