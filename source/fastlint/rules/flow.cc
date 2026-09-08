#include "fastlint/rules/flow.h"

#include "fastlint/ast/generated/views.h"

namespace fastlint::rules {

using namespace ast;

namespace {

bool constantTrue(const Node *test)
{
  return test && test->kind == NodeKind::Literal && test->text == "true";
}

bool isLoop(NodeKind kind)
{
  switch (kind) {
  case NodeKind::ForStatement:
  case NodeKind::ForInStatement:
  case NodeKind::ForOfStatement:
  case NodeKind::WhileStatement:
  case NodeKind::DoWhileStatement:
    return true;
  default:
    return false;
  }
}

/** An unlabelled `break` under `node` that targets the loop or switch whose body
 * `node` is part of. A nested loop or switch captures its own, so those subtrees
 * are skipped; a function body is another scope. */
bool hasUnlabelledBreak(const Node *node)
{
  if (!node) {
    return false;
  }
  if (node->kind == NodeKind::BreakStatement) {
    return BreakStatement(const_cast<Node *>(node)).label() == nullptr;
  }
  if (isLoop(node->kind) || node->kind == NodeKind::SwitchStatement ||
      FunctionLike::matches(node->kind))
  {
    return false;
  }
  for (const Node *child : node->children) {
    if (hasUnlabelledBreak(child)) {
      return true;
    }
  }
  return false;
}

/** An unlabelled `continue` targeting the loop whose body `node` is part of. A
 * nested loop captures it, but a switch does not, so switch bodies are walked. */
bool hasUnlabelledContinue(const Node *node)
{
  if (!node) {
    return false;
  }
  if (node->kind == NodeKind::ContinueStatement) {
    return ContinueStatement(const_cast<Node *>(node)).label() == nullptr;
  }
  if (isLoop(node->kind) || FunctionLike::matches(node->kind)) {
    return false;
  }
  for (const Node *child : node->children) {
    if (hasUnlabelledContinue(child)) {
      return true;
    }
  }
  return false;
}

/** A `break` (or `continue`) to `label` under `node`. Labels cross nesting, so
 * only a function body stops the walk. */
bool hasLabelledJump(const Node *node, string_view label, bool wantContinue)
{
  if (!node) {
    return false;
  }
  NodeKind kind = node->kind;
  if (!wantContinue && kind == NodeKind::BreakStatement) {
    Node *target = BreakStatement(const_cast<Node *>(node)).label();
    if (target && target->text == label) {
      return true;
    }
  } else if (wantContinue && kind == NodeKind::ContinueStatement) {
    Node *target = ContinueStatement(const_cast<Node *>(node)).label();
    if (target && target->text == label) {
      return true;
    }
  }
  if (FunctionLike::matches(kind)) {
    return false;
  }
  for (const Node *child : node->children) {
    if (hasLabelledJump(child, label, wantContinue)) {
      return true;
    }
  }
  return false;
}

bool switchCompletes(const Node *stmt)
{
  SwitchStatement view(const_cast<Node *>(stmt));
  span<Node *> cases = view.cases();
  if (cases.empty()) {
    return true;
  }
  bool hasDefault = false;
  for (Node *clause : cases) {
    if (!SwitchCase(clause).test()) {
      hasDefault = true;
    }
    if (hasUnlabelledBreak(clause)) {
      return true;
    }
  }
  // A value matching no clause falls past a switch without a default.
  if (!hasDefault) {
    return true;
  }
  // Clauses fall into each other, so only the last one running off its end
  // reaches past the switch.
  return sequenceCompletesNormally(SwitchCase(cases[cases.size() - 1]).consequent());
}

} // namespace

bool completesNormally(const ast::Node *stmt)
{
  if (!stmt) {
    return true;
  }
  switch (stmt->kind) {
  case NodeKind::ReturnStatement:
  case NodeKind::ThrowStatement:
  case NodeKind::BreakStatement:
  case NodeKind::ContinueStatement:
    return false;
  case NodeKind::BlockStatement:
    return sequenceCompletesNormally(BlockStatement(const_cast<Node *>(stmt)).body());
  case NodeKind::StaticBlock:
    return sequenceCompletesNormally(StaticBlock(const_cast<Node *>(stmt)).body());
  case NodeKind::IfStatement: {
    IfStatement view(const_cast<Node *>(stmt));
    if (!view.alternate()) {
      return true;
    }
    return completesNormally(view.consequent()) || completesNormally(view.alternate());
  }
  case NodeKind::LabeledStatement: {
    LabeledStatement view(const_cast<Node *>(stmt));
    if (completesNormally(view.body())) {
      return true;
    }
    string_view label = view.label() ? view.label()->text : string_view();
    return hasLabelledJump(view.body(), label, /*wantContinue=*/false);
  }
  case NodeKind::SwitchStatement:
    return switchCompletes(stmt);
  case NodeKind::WhileStatement: {
    WhileStatement view(const_cast<Node *>(stmt));
    if (!constantTrue(view.test())) {
      return true;
    }
    return hasUnlabelledBreak(view.body());
  }
  case NodeKind::DoWhileStatement: {
    DoWhileStatement view(const_cast<Node *>(stmt));
    if (hasUnlabelledBreak(view.body())) {
      return true;
    }
    if (constantTrue(view.test())) {
      return false;
    }
    // The test can be false, but only if control reaches it.
    return completesNormally(view.body()) || hasUnlabelledContinue(view.body());
  }
  case NodeKind::ForStatement: {
    ForStatement view(const_cast<Node *>(stmt));
    const Node *test = view.test();
    if (test && !constantTrue(test)) {
      return true;
    }
    return hasUnlabelledBreak(view.body());
  }
  case NodeKind::ForInStatement:
  case NodeKind::ForOfStatement:
    // The iterable may be empty, so the loop can complete without entering.
    return true;
  case NodeKind::WithStatement:
    return completesNormally(WithStatement(const_cast<Node *>(stmt)).body());
  case NodeKind::TryStatement: {
    TryStatement view(const_cast<Node *>(stmt));
    if (view.finalizer() && !completesNormally(view.finalizer())) {
      return false;
    }
    bool blockOk = completesNormally(view.block());
    bool handlerOk =
        view.handler() && completesNormally(CatchClause(view.handler()).body());
    return blockOk || handlerOk;
  }
  default:
    return true;
  }
}

bool sequenceCompletesNormally(litestl::util::span<ast::Node *> list)
{
  for (ast::Node *stmt : list) {
    if (stmt && !completesNormally(stmt)) {
      return false;
    }
  }
  return true;
}

} // namespace fastlint::rules
