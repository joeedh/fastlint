// no-unnecessary-condition: report a condition whose type is always truthy, always
// falsy, `never`, or (for `??`) always or never nullish
// (docs/rules/no-unnecessary-condition.md). typescript-eslint's rule over
// `TypeFacts`. The literal-comparison, optional-chain and array-predicate checks are
// not ported (see the docs).

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"
#include "fastlint/tsgo/generated/enums.h"
#include "fastlint/types/type_facts.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;
using litestl::util::Vector;
using types::TypeFacts;
using types::TypeId;

constexpr Message kMessages[] = {
    {"alwaysFalsy", "Unnecessary conditional, value is always falsy."},
    {"alwaysNullish",
     "Unnecessary conditional, left-hand side of `??` operator is always `null` or "
     "`undefined`."},
    {"alwaysTruthy", "Unnecessary conditional, value is always truthy."},
    {"never", "Unnecessary conditional, value is `never`."},
    {"neverNullish",
     "Unnecessary conditional, expected left-hand side of `??` operator to be possibly "
     "null or undefined."},
    {"noStrictNullCheck",
     "This rule requires the `strictNullChecks` compiler option to be turned on to "
     "function correctly."},
};

enum class LoopConstants { Never, Always, OnlyAllowedLiterals };

class Checker {
public:
  explicit Checker(RuleContext *ctx) : m_ctx(*ctx)
  {
    const JsonValue *opt = ctx->option(0);
    parseLoopConstants(opt ? opt->get("allowConstantLoopConditions") : nullptr);
    m_allowNoStrict =
        opt ? opt->getBool("allowRuleToRunWithoutStrictNullChecksIKnowWhatIAmDoing",
                           false)
            : false;
    m_strictNullChecks = facts().strictOption("strictNullChecks");
  }

  void checkProgram(Node *)
  {
    if (!m_strictNullChecks && !m_allowNoStrict && !m_warned) {
      m_warned = true;
      Report r;
      r.at(0, 0);
      r.messageId = "noStrictNullCheck";
      m_ctx.report(std::move(r));
    }
  }

  void checkTest(Node *test)
  {
    if (test) {
      checkNode(test, false, test);
    }
  }

  void checkLoop(Node *test)
  {
    if (!test) {
      return;
    }
    if (m_loopConstants == LoopConstants::Always && isTrueLiteral(constrained(test))) {
      return;
    }
    if (m_loopConstants == LoopConstants::OnlyAllowedLiterals &&
        isAllowedLoopLiteral(constrained(test)))
    {
      return;
    }
    checkNode(test, false, test);
  }

  void checkLogical(Node *node)
  {
    ast::LogicalExpression view(node);
    if (view.op() == ast::LogicalOperator::Nullish) {
      checkNodeForNullish(view.left());
    } else {
      checkNode(view.left(), false, view.left());
    }
  }

  void checkAssignment(Node *node)
  {
    ast::AssignmentExpression view(node);
    ast::AssignmentOperator op = view.op();
    if (op == ast::AssignmentOperator::OrAssign ||
        op == ast::AssignmentOperator::AndAssign)
    {
      checkNode(view.left(), false, view.left());
    } else if (op == ast::AssignmentOperator::NullishAssign) {
      checkNodeForNullish(view.left());
    }
  }

private:
  TypeFacts &facts()
  {
    return *m_ctx.types();
  }

  void checkNode(Node *expression, bool isUnaryNotArgument, Node *node)
  {
    if (expression->kind == NodeKind::UnaryExpression &&
        ast::UnaryExpression(expression).op() == ast::UnaryOperator::Not)
    {
      checkNode(ast::UnaryExpression(expression).argument(), !isUnaryNotArgument, node);
      return;
    }
    if (isArrayIndexExpression(expression)) {
      return;
    }
    // The right operand of a `&&`/`||` is only a condition when the whole
    // expression is used for truthiness; the left is checked on its own.
    if (expression->kind == NodeKind::LogicalExpression &&
        ast::LogicalExpression(expression).op() != ast::LogicalOperator::Nullish)
    {
      if (isOnlyUsedForTruthiness(expression)) {
        Node *right = ast::LogicalExpression(expression).right();
        checkNode(right, false, right);
      }
      return;
    }
    TypeId type = constrained(expression);
    if (!type || isConditionalAlwaysNecessary(type)) {
      return;
    }
    const char *messageId = nullptr;
    if (facts().flags(type) & tsgo::TypeFlags::Never) {
      messageId = "never";
    } else if (!isPossiblyTruthy(type)) {
      messageId = isUnaryNotArgument ? "alwaysTruthy" : "alwaysFalsy";
    } else if (!isPossiblyFalsy(type)) {
      messageId = isUnaryNotArgument ? "alwaysFalsy" : "alwaysTruthy";
    }
    if (messageId) {
      report(node, messageId);
    }
  }

  void checkNodeForNullish(Node *node)
  {
    TypeId type = constrained(node);
    if (!type) {
      return;
    }
    if (facts().flags(type) & (tsgo::TypeFlags::Any | tsgo::TypeFlags::Unknown |
                               tsgo::TypeFlags::TypeParameter))
    {
      return;
    }
    const char *messageId = nullptr;
    if (facts().flags(type) & tsgo::TypeFlags::Never) {
      messageId = "never";
    } else if (!isPossiblyNullish(type)) {
      if (!isArrayIndexExpression(node)) {
        messageId = "neverNullish";
      }
    } else if (isAlwaysNullish(type)) {
      messageId = "alwaysNullish";
    }
    if (messageId) {
      report(node, messageId);
    }
  }

  void report(Node *node, const char *messageId)
  {
    Report r;
    r.node = node;
    r.messageId = messageId;
    m_ctx.report(std::move(r));
  }

  /** Always necessary when a union member is `any`, `unknown` or a type variable,
   * since its truthiness cannot be decided. */
  bool isConditionalAlwaysNecessary(TypeId type)
  {
    for (TypeId part : unionParts(type)) {
      if (facts().flags(part) & (tsgo::TypeFlags::Any | tsgo::TypeFlags::Unknown |
                                 tsgo::TypeFlags::TypeVariable))
      {
        return true;
      }
    }
    return false;
  }

  bool isPossiblyTruthy(TypeId type)
  {
    for (TypeId u : unionParts(type)) {
      bool everyNotFalsy = true;
      for (TypeId i : intersectionParts(u)) {
        if (isFalsyType(i)) {
          everyNotFalsy = false;
          break;
        }
      }
      if (everyNotFalsy) {
        return true;
      }
    }
    return false;
  }

  bool isPossiblyFalsy(TypeId type)
  {
    for (TypeId u : unionParts(type)) {
      for (TypeId i : intersectionParts(u)) {
        if (isTruthyLiteral(i)) {
          continue;
        }
        if (facts().flags(i) & tsgo::TypeFlags::PossiblyFalsy) {
          return true;
        }
      }
    }
    return false;
  }

  bool isPossiblyNullish(TypeId type)
  {
    for (TypeId part : unionParts(type)) {
      if (facts().flags(part) &
          (tsgo::TypeFlags::Undefined | tsgo::TypeFlags::Null | tsgo::TypeFlags::Void))
      {
        return true;
      }
    }
    return false;
  }

  bool isAlwaysNullish(TypeId type)
  {
    for (TypeId part : unionParts(type)) {
      if (!(facts().flags(part) & (tsgo::TypeFlags::Undefined | tsgo::TypeFlags::Null))) {
        return false;
      }
    }
    return true;
  }

  bool isFalsyType(TypeId type)
  {
    uint32_t f = facts().flags(type);
    if (f & (tsgo::TypeFlags::Undefined | tsgo::TypeFlags::Null | tsgo::TypeFlags::Void))
    {
      return true;
    }
    if (f & tsgo::TypeFlags::BooleanLiteral) {
      return facts().literalText(type) == "false";
    }
    if (f & tsgo::TypeFlags::StringLiteral) {
      return facts().literalText(type).empty();
    }
    if (f & (tsgo::TypeFlags::NumberLiteral | tsgo::TypeFlags::BigIntLiteral)) {
      return isZeroText(facts().literalText(type));
    }
    return false;
  }

  bool isTruthyLiteral(TypeId type)
  {
    uint32_t f = facts().flags(type);
    if (f & tsgo::TypeFlags::BooleanLiteral) {
      return facts().literalText(type) == "true";
    }
    if (f & tsgo::TypeFlags::StringLiteral) {
      return !facts().literalText(type).empty();
    }
    if (f & (tsgo::TypeFlags::NumberLiteral | tsgo::TypeFlags::BigIntLiteral)) {
      return !isZeroText(facts().literalText(type));
    }
    return false;
  }

  bool isTrueLiteral(TypeId type)
  {
    return type && (facts().flags(type) & tsgo::TypeFlags::BooleanLiteral) &&
           facts().literalText(type) == "true";
  }

  bool isAllowedLoopLiteral(TypeId type)
  {
    if (!type) {
      return false;
    }
    uint32_t f = facts().flags(type);
    if (f & tsgo::TypeFlags::BooleanLiteral) {
      return true;
    }
    if (f & tsgo::TypeFlags::NumberLiteral) {
      string_view text = facts().literalText(type);
      return text == "0" || text == "1";
    }
    return false;
  }

  static bool isZeroText(string_view text)
  {
    return text == "0" || text == "-0";
  }

  bool isArrayIndexExpression(Node *node)
  {
    if (!node || node->kind != NodeKind::MemberExpression) {
      return false;
    }
    ast::MemberExpression member(node);
    if (!member.isComputed()) {
      return false;
    }
    TypeId objectType = constrained(member.object());
    for (TypeId part : unionParts(objectType)) {
      TypeId apparent = facts().apparentType(part);
      if (facts().isArray(apparent)) {
        return true;
      }
      // A literal index into a tuple keeps a sound type, so only a non-literal
      // index into a tuple is treated as an array access.
      if (facts().isTuple(apparent)) {
        Node *property = member.property();
        if (property && property->kind != NodeKind::Literal) {
          return true;
        }
      }
    }
    return false;
  }

  /** `node` feeds a truthiness test, walking up through `&&` left operands, `!` and
   * an enclosing truthiness context. */
  static bool isOnlyUsedForTruthiness(Node *node)
  {
    Node *parent = node->parent;
    if (!parent) {
      return false;
    }
    switch (parent->kind) {
    case NodeKind::ConditionalExpression:
      return ast::ConditionalExpression(parent).test() == node;
    case NodeKind::IfStatement:
      return ast::IfStatement(parent).test() == node;
    case NodeKind::WhileStatement:
      return ast::WhileStatement(parent).test() == node;
    case NodeKind::DoWhileStatement:
      return ast::DoWhileStatement(parent).test() == node;
    case NodeKind::ForStatement:
      return ast::ForStatement(parent).test() == node;
    case NodeKind::LogicalExpression: {
      ast::LogicalExpression view(parent);
      return (view.op() == ast::LogicalOperator::And && view.left() == node) ||
             isOnlyUsedForTruthiness(parent);
    }
    case NodeKind::UnaryExpression:
      return ast::UnaryExpression(parent).op() == ast::UnaryOperator::Not;
    default:
      return false;
    }
  }

  Vector<TypeId, 4> unionParts(TypeId type)
  {
    Vector<TypeId, 4> out;
    if (facts().flags(type) & tsgo::TypeFlags::Union) {
      for (TypeId member : facts().unionMembers(type)) {
        out.append(member);
      }
    }
    if (out.isEmpty()) {
      out.append(type);
    }
    return out;
  }

  Vector<TypeId, 4> intersectionParts(TypeId type)
  {
    Vector<TypeId, 4> out;
    if (facts().flags(type) & tsgo::TypeFlags::Intersection) {
      for (TypeId member : facts().unionMembers(type)) {
        out.append(member);
      }
    }
    if (out.isEmpty()) {
      out.append(type);
    }
    return out;
  }

  TypeId constrained(Node *node)
  {
    TypeId type = facts().typeOf(node);
    if (type && facts().isTypeParameter(type)) {
      if (TypeId constraint = facts().constraintOf(type)) {
        return constraint;
      }
    }
    return type;
  }

  void parseLoopConstants(const JsonValue *value)
  {
    if (!value) {
      return;
    }
    if (value->kind == tsgo::JsonKind::Bool) {
      m_loopConstants = value->asBool() ? LoopConstants::Always : LoopConstants::Never;
      return;
    }
    if (value->isString()) {
      string_view text = value->asString();
      if (text == "always") {
        m_loopConstants = LoopConstants::Always;
      } else if (text == "only-allowed-literals") {
        m_loopConstants = LoopConstants::OnlyAllowedLiterals;
      } else {
        m_loopConstants = LoopConstants::Never;
      }
    }
  }

  RuleContext &m_ctx;
  LoopConstants m_loopConstants = LoopConstants::Never;
  bool m_allowNoStrict = false;
  bool m_strictNullChecks = true;
  bool m_warned = false;
};

void create(RuleContext &ctx)
{
  Checker *checker = ctx.state<Checker>(&ctx);
  ctx.on(NodeKind::Program, [checker](Node *node) { checker->checkProgram(node); });
  ctx.on(NodeKind::IfStatement,
         [checker](Node *node) { checker->checkTest(ast::IfStatement(node).test()); });
  ctx.on(NodeKind::ConditionalExpression, [checker](Node *node) {
    checker->checkTest(ast::ConditionalExpression(node).test());
  });
  ctx.on(NodeKind::WhileStatement,
         [checker](Node *node) { checker->checkLoop(ast::WhileStatement(node).test()); });
  ctx.on(NodeKind::DoWhileStatement, [checker](Node *node) {
    checker->checkLoop(ast::DoWhileStatement(node).test());
  });
  ctx.on(NodeKind::ForStatement,
         [checker](Node *node) { checker->checkLoop(ast::ForStatement(node).test()); });
  ctx.on(NodeKind::LogicalExpression,
         [checker](Node *node) { checker->checkLogical(node); });
  ctx.on(NodeKind::AssignmentExpression,
         [checker](Node *node) { checker->checkAssignment(node); });
}

const char kSchema[] =
    R"([{"type":"object","properties":{"allowConstantLoopConditions":{"oneOf":[{"type":"boolean"},{"enum":["always","never","only-allowed-literals"]}]},"allowRuleToRunWithoutStrictNullChecksIKnowWhatIAmDoing":{"type":"boolean"}},"additionalProperties":false}])";

} // namespace

const RuleDef kNoUnnecessaryCondition{
    {
        "no-unnecessary-condition",
        "Disallow conditionals where the type is always truthy or always falsy",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/"
        "no-unnecessary-condition.md",
        /*recommended=*/false,
        /*fixable=*/false,
        /*hasSuggestions=*/false,
        /*typeAware=*/true,
        messagesOf(kMessages),
        kSchema,
    },
    create,
};

} // namespace fastlint::rules
