// strict-boolean-expressions: disallow non-boolean values in a boolean context
// (docs/rules/strict-boolean-expressions.md). typescript-eslint's rule over
// `TypeFacts`: each condition's type is classified into variant kinds and matched
// against a decision table. The suggestion fixes and the array-predicate and
// truthiness-assertion paths are not ported (see the docs).

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"
#include "fastlint/tsgo/generated/enums.h"
#include "fastlint/types/type_facts.h"

#include "util/set.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;
using litestl::util::Set;
using litestl::util::Vector;
using types::TypeFacts;
using types::TypeId;

constexpr Message kMessages[] = {
    {"conditionErrorAny",
     "Unexpected any value in {{context}}. An explicit comparison or type conversion "
     "is required."},
    {"conditionErrorNullableBoolean",
     "Unexpected nullable boolean value in {{context}}. Please handle the nullish case "
     "explicitly."},
    {"conditionErrorNullableEnum",
     "Unexpected nullable enum value in {{context}}. Please handle the "
     "nullish/zero/NaN cases explicitly."},
    {"conditionErrorNullableNumber",
     "Unexpected nullable number value in {{context}}. Please handle the "
     "nullish/zero/NaN cases explicitly."},
    {"conditionErrorNullableObject",
     "Unexpected nullable object value in {{context}}. An explicit null check is "
     "required."},
    {"conditionErrorNullableString",
     "Unexpected nullable string value in {{context}}. Please handle the nullish/empty "
     "cases explicitly."},
    {"conditionErrorNullish",
     "Unexpected nullish value in conditional. The condition is always false."},
    {"conditionErrorNumber",
     "Unexpected number value in {{context}}. An explicit zero/NaN check is required."},
    {"conditionErrorObject",
     "Unexpected object value in {{context}}. The condition is always true."},
    {"conditionErrorOther",
     "Unexpected value in conditional. A boolean expression is required."},
    {"conditionErrorString",
     "Unexpected string value in {{context}}. An explicit empty string check is "
     "required."},
    {"noStrictNullCheck",
     "This rule requires the `strictNullChecks` compiler option to be turned on to "
     "function correctly."},
};

/** The variant kinds a condition's type is sorted into, as a bit set. */
namespace variant {
constexpr uint32_t Any = 1u << 0;
constexpr uint32_t Boolean = 1u << 1;
constexpr uint32_t Enum = 1u << 2;
constexpr uint32_t Never = 1u << 3;
constexpr uint32_t Nullish = 1u << 4;
constexpr uint32_t Number = 1u << 5;
constexpr uint32_t Object = 1u << 6;
constexpr uint32_t String = 1u << 7;
constexpr uint32_t TruthyBoolean = 1u << 8;
constexpr uint32_t TruthyNumber = 1u << 9;
constexpr uint32_t TruthyString = 1u << 10;
} // namespace variant

struct Options {
  bool allowAny = false;
  bool allowNullableBoolean = false;
  bool allowNullableEnum = false;
  bool allowNullableNumber = false;
  bool allowNullableObject = true;
  bool allowNullableString = false;
  bool allowNumber = true;
  bool allowString = true;
  bool allowNoStrict = false;
};

class Checker {
public:
  explicit Checker(RuleContext *ctx) : m_ctx(*ctx)
  {
    const JsonValue *opt = ctx->option(0);
    if (opt) {
      m_opts.allowAny = opt->getBool("allowAny", m_opts.allowAny);
      m_opts.allowNullableBoolean =
          opt->getBool("allowNullableBoolean", m_opts.allowNullableBoolean);
      m_opts.allowNullableEnum =
          opt->getBool("allowNullableEnum", m_opts.allowNullableEnum);
      m_opts.allowNullableNumber =
          opt->getBool("allowNullableNumber", m_opts.allowNullableNumber);
      m_opts.allowNullableObject =
          opt->getBool("allowNullableObject", m_opts.allowNullableObject);
      m_opts.allowNullableString =
          opt->getBool("allowNullableString", m_opts.allowNullableString);
      m_opts.allowNumber = opt->getBool("allowNumber", m_opts.allowNumber);
      m_opts.allowString = opt->getBool("allowString", m_opts.allowString);
      m_opts.allowNoStrict =
          opt->getBool("allowRuleToRunWithoutStrictNullChecksIKnowWhatIAmDoing", false);
    }
    m_strictNullChecks = facts().strictOption("strictNullChecks");
  }

  void checkProgram(Node *)
  {
    if (!m_strictNullChecks && !m_opts.allowNoStrict && !m_warned) {
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
      traverseNode(test, true);
    }
  }

  void checkUnary(Node *node)
  {
    ast::UnaryExpression view(node);
    if (view.op() == ast::UnaryOperator::Not) {
      traverseNode(view.argument(), true);
    }
  }

  void checkLogical(Node *node)
  {
    ast::LogicalExpression view(node);
    if (view.op() != ast::LogicalOperator::Nullish) {
      // A bare logical expression is control flow, so its right operand runs for
      // side effects only and is not itself a condition.
      traverseLogical(node, false);
    }
  }

private:
  TypeFacts &facts()
  {
    return *m_ctx.types();
  }

  void traverseLogical(Node *node, bool isCondition)
  {
    ast::LogicalExpression view(node);
    traverseNode(view.left(), true);
    traverseNode(view.right(), isCondition);
  }

  void traverseNode(Node *node, bool isCondition)
  {
    if (!node || !m_seen.add(node)) {
      return;
    }
    if (node->kind == NodeKind::LogicalExpression &&
        ast::LogicalExpression(node).op() != ast::LogicalOperator::Nullish)
    {
      traverseLogical(node, isCondition);
      return;
    }
    if (!isCondition) {
      return;
    }
    checkNode(node);
  }

  void checkNode(Node *node)
  {
    TypeId type = constrainedType(facts().typeOf(node));
    if (!type) {
      return;
    }
    uint32_t set = inspectVariants(type);
    const char *messageId = determineReport(set);
    if (!messageId) {
      return;
    }
    Report r;
    r.node = node;
    r.messageId = messageId;
    r.data.append({"context", "conditional"});
    m_ctx.report(std::move(r));
  }

  const char *determineReport(uint32_t set) const
  {
    using namespace variant;
    if (set == Boolean || set == TruthyBoolean || set == Never) {
      return nullptr;
    }
    if (set == Nullish) {
      return "conditionErrorNullish";
    }
    if (set == (Nullish | TruthyBoolean)) {
      return nullptr;
    }
    if (set == (Nullish | Boolean)) {
      return m_opts.allowNullableBoolean ? nullptr : "conditionErrorNullableBoolean";
    }
    if ((m_opts.allowNumber && set == (Nullish | TruthyNumber)) ||
        (m_opts.allowString && set == (Nullish | TruthyString)))
    {
      return nullptr;
    }
    if (set == String || set == TruthyString) {
      return m_opts.allowString ? nullptr : "conditionErrorString";
    }
    if (set == (Nullish | String)) {
      return m_opts.allowNullableString ? nullptr : "conditionErrorNullableString";
    }
    if (set == Number || set == TruthyNumber) {
      return m_opts.allowNumber ? nullptr : "conditionErrorNumber";
    }
    if (set == (Nullish | Number)) {
      return m_opts.allowNullableNumber ? nullptr : "conditionErrorNullableNumber";
    }
    if (set == Object) {
      return "conditionErrorObject";
    }
    if (set == (Nullish | Object)) {
      return m_opts.allowNullableObject ? nullptr : "conditionErrorNullableObject";
    }
    if (set == (Nullish | Number | Enum) || set == (Nullish | String | Enum) ||
        set == (Nullish | TruthyNumber | Enum) ||
        set == (Nullish | TruthyString | Enum) ||
        set == (Nullish | TruthyNumber | TruthyString | Enum) ||
        set == (Nullish | TruthyNumber | String | Enum) ||
        set == (Nullish | TruthyString | Number | Enum) ||
        set == (Nullish | Number | String | Enum))
    {
      return m_opts.allowNullableEnum ? nullptr : "conditionErrorNullableEnum";
    }
    if (set == Any) {
      return m_opts.allowAny ? nullptr : "conditionErrorAny";
    }
    return "conditionErrorOther";
  }

  uint32_t inspectVariants(TypeId type)
  {
    Vector<TypeId, 8> parts;
    constituents(type, parts);
    uint32_t set = 0;

    constexpr uint32_t kNullish =
        tsgo::TypeFlags::Null | tsgo::TypeFlags::Undefined | tsgo::TypeFlags::Void;
    int booleanCount = 0;
    TypeId oneBoolean = 0;
    bool hasString = false, allTruthyString = true;
    bool hasNumber = false, allTruthyNumber = true;
    bool hasObject = false, brandedBoolean = false;

    for (TypeId part : parts) {
      uint32_t f = facts().flags(part);
      if (f & kNullish) {
        set |= variant::Nullish;
      }
      if (f & tsgo::TypeFlags::BooleanLike) {
        booleanCount++;
        oneBoolean = part;
      }
      if (f & tsgo::TypeFlags::StringLike) {
        hasString = true;
        if (!(f & tsgo::TypeFlags::StringLiteral) || facts().literalText(part).empty()) {
          allTruthyString = false;
        }
      }
      if (f & (tsgo::TypeFlags::NumberLike | tsgo::TypeFlags::BigIntLike)) {
        hasNumber = true;
        if (!(f & tsgo::TypeFlags::NumberLiteral) || facts().literalText(part) == "0") {
          allTruthyNumber = false;
        }
      }
      if (f & tsgo::TypeFlags::EnumLike) {
        set |= variant::Enum;
      }
      if (!(f & kExcludesObject)) {
        hasObject = true;
        if (isBrandedBoolean(part)) {
          brandedBoolean = true;
        }
      }
      if (f & (tsgo::TypeFlags::TypeParameter | tsgo::TypeFlags::Any |
               tsgo::TypeFlags::Unknown))
      {
        set |= variant::Any;
      }
      if (f & tsgo::TypeFlags::Never) {
        set |= variant::Never;
      }
    }

    if (booleanCount == 1) {
      set |= isTrueLiteral(oneBoolean) ? variant::TruthyBoolean : variant::Boolean;
    } else if (booleanCount >= 2) {
      set |= variant::Boolean;
    }
    if (hasString) {
      set |= allTruthyString ? variant::TruthyString : variant::String;
    }
    if (hasNumber) {
      set |= allTruthyNumber ? variant::TruthyNumber : variant::Number;
    }
    if (hasObject) {
      set |= brandedBoolean ? variant::Boolean : variant::Object;
    }
    return set;
  }

  bool isTrueLiteral(TypeId type)
  {
    return (facts().flags(type) & tsgo::TypeFlags::BooleanLiteral) &&
           facts().literalText(type) == "true";
  }

  /** A branded boolean is an intersection with a boolean member, e.g.
   * `boolean & { __brand: 'Foo' }`. */
  bool isBrandedBoolean(TypeId type)
  {
    if (!(facts().flags(type) & tsgo::TypeFlags::Intersection)) {
      return false;
    }
    for (TypeId member : facts().unionMembers(type)) {
      if (facts().flags(member) &
          (tsgo::TypeFlags::Boolean | tsgo::TypeFlags::BooleanLiteral))
      {
        return true;
      }
    }
    return false;
  }

  /** The members of a union, or the type itself; an intersection stays whole. */
  void constituents(TypeId type, Vector<TypeId, 8> &out)
  {
    if (facts().flags(type) & tsgo::TypeFlags::Union) {
      for (TypeId member : facts().unionMembers(type)) {
        out.append(member);
      }
    }
    if (out.isEmpty()) {
      out.append(type);
    }
  }

  TypeId constrainedType(TypeId type)
  {
    if (type && facts().isTypeParameter(type)) {
      if (TypeId constraint = facts().constraintOf(type)) {
        return constraint;
      }
    }
    return type;
  }

  // The flags that keep a constituent out of the `object` bucket: the primitives,
  // the nullish and boolean-like kinds, and the open type variables.
  static constexpr uint32_t kExcludesObject =
      tsgo::TypeFlags::Null | tsgo::TypeFlags::Undefined | tsgo::TypeFlags::Void |
      tsgo::TypeFlags::BooleanLike | tsgo::TypeFlags::StringLike |
      tsgo::TypeFlags::NumberLike | tsgo::TypeFlags::BigIntLike |
      tsgo::TypeFlags::TypeParameter | tsgo::TypeFlags::Any | tsgo::TypeFlags::Unknown |
      tsgo::TypeFlags::Never;

  RuleContext &m_ctx;
  Options m_opts;
  Set<Node *> m_seen;
  bool m_strictNullChecks = true;
  bool m_warned = false;
};

void create(RuleContext &ctx)
{
  Checker *checker = ctx.state<Checker>(&ctx);
  ctx.on(NodeKind::Program, [checker](Node *node) { checker->checkProgram(node); });
  ctx.on(NodeKind::IfStatement,
         [checker](Node *node) { checker->checkTest(ast::IfStatement(node).test()); });
  ctx.on(NodeKind::WhileStatement,
         [checker](Node *node) { checker->checkTest(ast::WhileStatement(node).test()); });
  ctx.on(NodeKind::DoWhileStatement, [checker](Node *node) {
    checker->checkTest(ast::DoWhileStatement(node).test());
  });
  ctx.on(NodeKind::ForStatement,
         [checker](Node *node) { checker->checkTest(ast::ForStatement(node).test()); });
  ctx.on(NodeKind::ConditionalExpression, [checker](Node *node) {
    checker->checkTest(ast::ConditionalExpression(node).test());
  });
  ctx.on(NodeKind::UnaryExpression, [checker](Node *node) { checker->checkUnary(node); });
  ctx.on(NodeKind::LogicalExpression,
         [checker](Node *node) { checker->checkLogical(node); });
}

const char kSchema[] =
    R"([{"type":"object","properties":{"allowAny":{"type":"boolean"},"allowNullableBoolean":{"type":"boolean"},"allowNullableEnum":{"type":"boolean"},"allowNullableNumber":{"type":"boolean"},"allowNullableObject":{"type":"boolean"},"allowNullableString":{"type":"boolean"},"allowNumber":{"type":"boolean"},"allowString":{"type":"boolean"},"allowRuleToRunWithoutStrictNullChecksIKnowWhatIAmDoing":{"type":"boolean"}},"additionalProperties":false}])";

} // namespace

const RuleDef kStrictBooleanExpressions{
    {
        "strict-boolean-expressions",
        "Disallow certain types in boolean expressions",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/"
        "strict-boolean-expressions.md",
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
