// no-constant-condition: disallow constant expressions in conditions
// (docs/rules/no-constant-condition.md). A port of ESLint's `isConstant`.

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;

constexpr Message kMessages[] = {
    {"unexpected", "Unexpected constant condition."},
};

enum class CheckLoops : uint8_t { All, AllExceptWhileTrue, None };

struct State {
  CheckLoops checkLoops = CheckLoops::AllExceptWhileTrue;
  /** Loops with a constant test in the current function; a `yield` clears them. */
  Vector<Node *, 4> loops;
  Vector<Vector<Node *, 4>, 4> stack;
};

bool isConstant(RuleContext &ctx, Node *node, bool inBoolean);

/** Whether a template chunk's cooked text has characters; line continuations cook to
 * nothing. */
bool cookedNonEmpty(string_view raw)
{
  for (size_t i = 0; i < raw.size(); i++) {
    if (raw[i] == '\\' && i + 1 < raw.size() &&
        (raw[i + 1] == '\n' || raw[i + 1] == '\r'))
    {
      i++;
      if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n') {
        i++;
      }
      continue;
    }
    return true;
  }
  return false;
}

/** Whether `node` alone decides a logical `op`: a truthy left of `||`, a falsy left of
 * `&&`. */
bool isLogicalIdentity(RuleContext &ctx, Node *node, ast::LogicalOperator op)
{
  switch (node->kind) {
  case NodeKind::Literal: {
    bool truthy = literalTruthy(node);
    return (op == ast::LogicalOperator::Or && truthy) ||
           (op == ast::LogicalOperator::And && !truthy);
  }
  case NodeKind::UnaryExpression:
    return ast::UnaryExpression(node).op() == ast::UnaryOperator::Void &&
           op == ast::LogicalOperator::And;
  case NodeKind::LogicalExpression: {
    ast::LogicalExpression logical(node);
    return logical.op() == op && (isLogicalIdentity(ctx, logical.left(), op) ||
                                  isLogicalIdentity(ctx, logical.right(), op));
  }
  case NodeKind::AssignmentExpression: {
    ast::AssignmentExpression assignment(node);
    ast::AssignmentOperator aop = assignment.op();
    if ((aop == ast::AssignmentOperator::OrAssign && op == ast::LogicalOperator::Or) ||
        (aop == ast::AssignmentOperator::AndAssign && op == ast::LogicalOperator::And))
    {
      return isLogicalIdentity(ctx, assignment.right(), op);
    }
    return false;
  }
  default:
    return false;
  }
}

bool isConstant(RuleContext &ctx, Node *node, bool inBoolean)
{
  if (!node) {
    return true;
  }
  switch (node->kind) {
  case NodeKind::Literal:
  case NodeKind::ArrowFunctionExpression:
  case NodeKind::FunctionExpression:
  case NodeKind::ClassExpression:
  case NodeKind::ObjectExpression:
    return true;
  case NodeKind::TemplateLiteral: {
    bool hasText = false;
    bool allConstant = true;
    for (Node *part : ast::TemplateLiteral(node).parts()) {
      if (part->kind == NodeKind::TemplateElement) {
        hasText = hasText || cookedNonEmpty(part->text);
      } else if (!isConstant(ctx, part, false)) {
        allConstant = false;
      }
    }
    return (inBoolean && hasText) || allConstant;
  }
  case NodeKind::ArrayExpression: {
    if (inBoolean) {
      return true;
    }
    for (Node *element : ast::ArrayExpression(node).elements()) {
      if (!isConstant(ctx, element, false)) {
        return false;
      }
    }
    return true;
  }
  case NodeKind::UnaryExpression: {
    ast::UnaryExpression unary(node);
    ast::UnaryOperator op = unary.op();
    if (op == ast::UnaryOperator::Void || (op == ast::UnaryOperator::Typeof && inBoolean))
    {
      return true;
    }
    return isConstant(ctx, unary.argument(), op == ast::UnaryOperator::Not);
  }
  case NodeKind::BinaryExpression: {
    ast::BinaryExpression binary(node);
    return binary.op() != ast::BinaryOperator::In &&
           isConstant(ctx, binary.left(), false) &&
           isConstant(ctx, binary.right(), false);
  }
  case NodeKind::LogicalExpression: {
    ast::LogicalExpression logical(node);
    bool leftConstant = isConstant(ctx, logical.left(), inBoolean);
    bool rightConstant = isConstant(ctx, logical.right(), inBoolean);
    bool leftShortCircuit =
        leftConstant && isLogicalIdentity(ctx, logical.left(), logical.op());
    bool rightShortCircuit = inBoolean && rightConstant &&
                             isLogicalIdentity(ctx, logical.right(), logical.op());
    return (leftConstant && rightConstant) || leftShortCircuit || rightShortCircuit;
  }
  case NodeKind::NewExpression:
    return inBoolean;
  case NodeKind::AssignmentExpression: {
    ast::AssignmentExpression assignment(node);
    ast::AssignmentOperator op = assignment.op();
    if (op == ast::AssignmentOperator::Assign) {
      return isConstant(ctx, assignment.right(), inBoolean);
    }
    if (inBoolean && (op == ast::AssignmentOperator::OrAssign ||
                      op == ast::AssignmentOperator::AndAssign))
    {
      return isLogicalIdentity(ctx,
                               assignment.right(),
                               op == ast::AssignmentOperator::OrAssign
                                   ? ast::LogicalOperator::Or
                                   : ast::LogicalOperator::And);
    }
    return false;
  }
  case NodeKind::SequenceExpression: {
    span<Node *> expressions = ast::SequenceExpression(node).expressions();
    return expressions.size() > 0 &&
           isConstant(ctx, expressions[expressions.size() - 1], inBoolean);
  }
  case NodeKind::SpreadElement:
    return isConstant(ctx, ast::SpreadElement(node).argument(), inBoolean);
  case NodeKind::CallExpression: {
    ast::CallExpression call(node);
    Node *callee = call.callee();
    if (callee->isIdentifier("Boolean") && isGlobalReference(ctx, callee)) {
      span<Node *> arguments = call.arguments();
      return arguments.size() == 0 || isConstant(ctx, arguments[0], true);
    }
    return false;
  }
  case NodeKind::Identifier:
    return node->text == "undefined" && isGlobalReference(ctx, node);
  default:
    return false;
  }
}

void create(RuleContext &ctx)
{
  State *state = ctx.state<State>();
  if (const JsonValue *options = ctx.option(0)) {
    if (const JsonValue *check = options->get("checkLoops")) {
      if (check->kind == tsgo::JsonKind::Bool) {
        state->checkLoops = check->asBool() ? CheckLoops::All : CheckLoops::None;
      } else if (check->asString() == "all") {
        state->checkLoops = CheckLoops::All;
      } else if (check->asString() == "none") {
        state->checkLoops = CheckLoops::None;
      }
    }
  }

  auto reportIfConstant = [&ctx](Node *test) {
    if (test && isConstant(ctx, test, true)) {
      ctx.report(test, "unexpected");
    }
  };
  ctx.on(NodeKind::IfStatement, [reportIfConstant](Node *node) {
    reportIfConstant(ast::IfStatement(node).test());
  });
  ctx.on(NodeKind::ConditionalExpression, [reportIfConstant](Node *node) {
    reportIfConstant(ast::ConditionalExpression(node).test());
  });

  auto trackLoop = [&ctx, state](Node *loop, Node *test) {
    if (state->checkLoops == CheckLoops::None || !test) {
      return;
    }
    if (isConstant(ctx, test, true)) {
      state->loops.append(loop);
    }
  };
  auto exitLoop = [&ctx, state](Node *loop, Node *test) {
    if (state->loops.contains(loop)) {
      state->loops.remove(loop);
      ctx.report(test, "unexpected");
    }
  };
  ctx.on(NodeKind::WhileStatement, [state, trackLoop](Node *node) {
    Node *test = ast::WhileStatement(node).test();
    if (state->checkLoops == CheckLoops::AllExceptWhileTrue &&
        test->kind == NodeKind::Literal && test->text == "true")
    {
      return;
    }
    trackLoop(node, test);
  });
  ctx.onExit(NodeKind::WhileStatement, [exitLoop](Node *node) {
    exitLoop(node, ast::WhileStatement(node).test());
  });
  ctx.on(NodeKind::DoWhileStatement, [trackLoop](Node *node) {
    trackLoop(node, ast::DoWhileStatement(node).test());
  });
  ctx.onExit(NodeKind::DoWhileStatement, [exitLoop](Node *node) {
    exitLoop(node, ast::DoWhileStatement(node).test());
  });
  ctx.on(NodeKind::ForStatement,
         [trackLoop](Node *node) { trackLoop(node, ast::ForStatement(node).test()); });
  ctx.onExit(NodeKind::ForStatement,
             [exitLoop](Node *node) { exitLoop(node, ast::ForStatement(node).test()); });

  // A generator's loop may be left through `yield`, so loops are tracked per
  // function and forgotten at the first yield.
  ctx.on<ast::FunctionLike>([state](Node *) {
    state->stack.append(std::move(state->loops));
    state->loops = Vector<Node *, 4>();
  });
  ctx.onExit<ast::FunctionLike>([state](Node *) {
    if (!state->stack.isEmpty()) {
      state->loops = state->stack.pop_back();
    }
  });
  ctx.on(NodeKind::YieldExpression, [state](Node *node) {
    // A yield in a `for` head runs before the loop's test, so that loop stays.
    Vector<Node *, 4> kept;
    for (Node *loop : state->loops) {
      Node *init =
          loop->kind == NodeKind::ForStatement ? ast::ForStatement(loop).init() : nullptr;
      if (init && node->start >= init->start && node->end <= init->end) {
        kept.append(loop);
      }
    }
    state->loops = std::move(kept);
  });
}

} // namespace

const RuleDef kNoConstantCondition{
    {
        "no-constant-condition",
        "Disallow constant expressions in conditions",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/"
        "no-constant-condition.md",
        /*recommended=*/true,
        /*fixable=*/false,
        /*hasSuggestions=*/false,
        /*typeAware=*/false,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
