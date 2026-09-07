// prefer-nullish-coalescing: prefer `??` over `||` and `||=` when the left side is
// nullable (docs/rules/prefer-nullish-coalescing.md). typescript-eslint's rule over
// `TypeFacts`. Only the logical-or forms are ported; the ternary and if-statement forms
// are not (see the docs).

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
    {"noStrictNullCheck",
     "This rule requires the `strictNullChecks` compiler option to be turned on to "
     "function correctly."},
    {"preferNullishOverOr",
     "Prefer using nullish coalescing operator (`??{{equals}}`) instead of a logical "
     "{{description}} (`||{{equals}}`), as it is a safer operator."},
    {"suggestNullish", "Fix to nullish coalescing operator (`??{{equals}}`)."},
};

/** The primitive kinds `ignorePrimitives` can exclude, as type-flag masks. */
struct Ignorable {
  bool bigint = false;
  bool boolean = false;
  bool number = false;
  bool string = false;

  uint32_t mask() const
  {
    uint32_t flags = 0;
    if (bigint) {
      flags |= tsgo::TypeFlags::BigIntLike;
    }
    if (boolean) {
      flags |= tsgo::TypeFlags::BooleanLike;
    }
    if (number) {
      flags |= tsgo::TypeFlags::NumberLike;
    }
    if (string) {
      flags |= tsgo::TypeFlags::StringLike;
    }
    return flags;
  }
};

class Checker {
public:
  explicit Checker(RuleContext *ctx) : m_ctx(*ctx)
  {
    const JsonValue *opt = ctx->option(0);
    m_ignoreConditionalTests = opt ? opt->getBool("ignoreConditionalTests", true) : true;
    m_ignoreBooleanCoercion = opt ? opt->getBool("ignoreBooleanCoercion", false) : false;
    m_ignoreMixed = opt ? opt->getBool("ignoreMixedLogicalExpressions", false) : false;
    m_allowNoStrict =
        opt ? opt->getBool("allowRuleToRunWithoutStrictNullChecksIKnowWhatIAmDoing",
                           false)
            : false;
    readIgnorePrimitives(opt ? opt->get("ignorePrimitives") : nullptr);
    m_strictNullChecks = facts().strictOption("strictNullChecks");
  }

  void checkProgram(Node *)
  {
    // The rule needs `strictNullChecks` to reason about nullability at all.
    if (!m_strictNullChecks && !m_allowNoStrict && !m_warned) {
      m_warned = true;
      Report r;
      r.at(0, 0);
      r.messageId = "noStrictNullCheck";
      m_ctx.report(std::move(r));
    }
  }

  void checkLogical(Node *node)
  {
    ast::LogicalExpression view(node);
    if (view.op() != ast::LogicalOperator::Or) {
      return;
    }
    report(node, view.left(), view.right(), "or", "", "||");
  }

  void checkAssignment(Node *node)
  {
    ast::AssignmentExpression view(node);
    if (view.op() != ast::AssignmentOperator::OrAssign) {
      return;
    }
    report(node, view.left(), view.right(), "assignment", "=", "||=");
  }

private:
  TypeFacts &facts()
  {
    return *m_ctx.types();
  }

  void report(Node *node,
              Node *left,
              Node *right,
              const char *description,
              const char *equals,
              string_view opText)
  {
    if (!eligible(node, left)) {
      return;
    }
    if (m_ignoreMixed && isMixedLogical(node, left, right)) {
      return;
    }
    string_view source = m_ctx.source();
    uint32_t at = findToken(source, left->end, right->start, opText);
    Report r;
    r.node = node;
    r.at(at, at + uint32_t(opText.size()));
    r.messageId = "preferNullishOverOr";
    r.data.append({"description", description});
    r.data.append({"equals", equals});
    Suggestion suggestion;
    suggestion.messageId = "suggestNullish";
    suggestion.data.append({"equals", equals});
    bool logical = node->kind == NodeKind::LogicalExpression;
    suggestion.fix = [node, logical](ast::Fixer &fixer) {
      fixer.setData(node,
                    0,
                    logical ? uint8_t(ast::LogicalOperator::Nullish)
                            : uint8_t(ast::AssignmentOperator::NullishAssign));
    };
    r.suggestions.append(std::move(suggestion));
    m_ctx.report(std::move(r));
  }

  bool eligible(Node *node, Node *testNode)
  {
    if (m_ignoreConditionalTests && isConditionalTest(node)) {
      return false;
    }
    if (m_ignoreBooleanCoercion && isBooleanContext(node)) {
      return false;
    }
    return isTypeEligible(facts().typeOf(testNode));
  }

  bool isTypeEligible(TypeId type)
  {
    if (!type || !(unionFlags(type) & kNullish)) {
      return false;
    }
    uint32_t ignorable = m_ignore.mask();
    if (ignorable == 0) {
      return true;
    }
    // `any` and `unknown` could hold any primitive, so an ignored primitive keeps them
    // out of the rule.
    if (facts().isAnyLike(type)) {
      return false;
    }
    for (TypeId part : constituents(type, tsgo::TypeFlags::Union)) {
      for (TypeId inner : constituents(part, tsgo::TypeFlags::Intersection)) {
        if (facts().flags(inner) & ignorable) {
          return false;
        }
      }
    }
    return true;
  }

  /** The flags of `type`, or the union of its members' flags. */
  uint32_t unionFlags(TypeId type)
  {
    uint32_t f = facts().flags(type);
    if (!(f & tsgo::TypeFlags::Union)) {
      return f;
    }
    uint32_t all = 0;
    for (TypeId member : facts().unionMembers(type)) {
      all |= facts().flags(member);
    }
    return all;
  }

  /** The members of a union or intersection, or the type itself. */
  Vector<TypeId, 4> constituents(TypeId type, uint32_t kind)
  {
    Vector<TypeId, 4> out;
    if (facts().flags(type) & kind) {
      for (TypeId member : facts().unionMembers(type)) {
        out.append(member);
      }
    }
    if (out.isEmpty()) {
      out.append(type);
    }
    return out;
  }

  /** `node` is the test of an `if`, loop or ternary, reached through logical, ternary
   * branch, sequence or `!` parents. */
  static bool isConditionalTest(Node *node)
  {
    Node *parent = node->parent;
    if (!parent) {
      return false;
    }
    switch (parent->kind) {
    case NodeKind::LogicalExpression:
      return isConditionalTest(parent);
    case NodeKind::ConditionalExpression: {
      ast::ConditionalExpression view(parent);
      if (view.consequent() == node || view.alternate() == node) {
        return isConditionalTest(parent);
      }
      return view.test() == node;
    }
    case NodeKind::SequenceExpression: {
      span<Node *> parts = ast::SequenceExpression(parent).expressions();
      return parts.size() > 0 && parts[parts.size() - 1] == node &&
             isConditionalTest(parent);
    }
    case NodeKind::UnaryExpression:
      return ast::UnaryExpression(parent).op() == ast::UnaryOperator::Not &&
             isConditionalTest(parent);
    case NodeKind::IfStatement:
      return ast::IfStatement(parent).test() == node;
    case NodeKind::WhileStatement:
      return ast::WhileStatement(parent).test() == node;
    case NodeKind::DoWhileStatement:
      return ast::DoWhileStatement(parent).test() == node;
    case NodeKind::ForStatement:
      return ast::ForStatement(parent).test() == node;
    default:
      return false;
    }
  }

  /** The `||` is next to a `&&`, so switching to `??` would change precedence. */
  static bool isMixedLogical(Node *node, Node *left, Node *right)
  {
    Vector<Node *, 8> queue;
    Vector<Node *, 8> seen;
    queue.append(node->parent);
    queue.append(left);
    queue.append(right);
    for (int i = 0; i < int(queue.size()); i++) {
      Node *current = queue[i];
      if (!current) {
        continue;
      }
      bool visited = false;
      for (Node *s : seen) {
        if (s == current) {
          visited = true;
          break;
        }
      }
      if (visited) {
        continue;
      }
      seen.append(current);
      if (current->kind != NodeKind::LogicalExpression) {
        continue;
      }
      ast::LogicalExpression view(current);
      if (view.op() == ast::LogicalOperator::And) {
        return true;
      }
      if (view.op() == ast::LogicalOperator::Or) {
        queue.append(current->parent);
        queue.append(view.left());
        queue.append(view.right());
      }
    }
    return false;
  }

  /** `node` is coerced to boolean by an enclosing `Boolean(...)` call. */
  static bool isBooleanContext(Node *node)
  {
    Node *parent = node->parent;
    if (!parent) {
      return false;
    }
    if (parent->kind == NodeKind::LogicalExpression) {
      return isBooleanContext(parent);
    }
    if (parent->kind == NodeKind::ConditionalExpression) {
      ast::ConditionalExpression view(parent);
      if (view.consequent() == node || view.alternate() == node) {
        return isBooleanContext(parent);
      }
    }
    if (parent->kind == NodeKind::SequenceExpression) {
      span<Node *> parts = ast::SequenceExpression(parent).expressions();
      if (parts.size() > 0 && parts[parts.size() - 1] == node) {
        return isBooleanContext(parent);
      }
    }
    if (parent->kind == NodeKind::CallExpression) {
      ast::CallExpression call(parent);
      Node *callee = call.callee();
      return callee && callee->kind == NodeKind::Identifier &&
             callee->text == "Boolean" && call.arguments().size() > 0;
    }
    return false;
  }

  void readIgnorePrimitives(const JsonValue *value)
  {
    if (!value) {
      return;
    }
    if (value->kind == tsgo::JsonKind::Bool) {
      bool all = value->asBool();
      m_ignore = {all, all, all, all};
      return;
    }
    if (value->isObject()) {
      m_ignore.bigint = value->getBool("bigint", false);
      m_ignore.boolean = value->getBool("boolean", false);
      m_ignore.number = value->getBool("number", false);
      m_ignore.string = value->getBool("string", false);
    }
  }

  static constexpr uint32_t kNullish = tsgo::TypeFlags::Any | tsgo::TypeFlags::Unknown |
                                       tsgo::TypeFlags::Null |
                                       tsgo::TypeFlags::Undefined | tsgo::TypeFlags::Void;

  RuleContext &m_ctx;
  bool m_ignoreConditionalTests = true;
  bool m_ignoreBooleanCoercion = false;
  bool m_ignoreMixed = false;
  bool m_allowNoStrict = false;
  bool m_strictNullChecks = true;
  bool m_warned = false;
  Ignorable m_ignore;
};

void create(RuleContext &ctx)
{
  Checker *checker = ctx.state<Checker>(&ctx);
  ctx.on(NodeKind::Program, [checker](Node *node) { checker->checkProgram(node); });
  ctx.on(NodeKind::LogicalExpression,
         [checker](Node *node) { checker->checkLogical(node); });
  ctx.on(NodeKind::AssignmentExpression,
         [checker](Node *node) { checker->checkAssignment(node); });
}

} // namespace

const RuleDef kPreferNullishCoalescing{
    {
        "prefer-nullish-coalescing",
        "Enforce using the nullish coalescing operator instead of logical chaining",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/"
        "prefer-nullish-coalescing.md",
        /*recommended=*/false,
        /*fixable=*/false,
        /*hasSuggestions=*/true,
        /*typeAware=*/true,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
