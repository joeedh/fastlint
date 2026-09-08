// no-unnecessary-type-assertion: report a `!` non-null assertion whose operand is
// already non-nullable, or is nullable where the surrounding context accepts the
// nullable type (docs/rules/no-unnecessary-type-assertion.md). Only the non-null
// assertion form is ported; the `as`/`<T>` cast forms are not (see the docs).

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"
#include "fastlint/tsgo/generated/enums.h"
#include "fastlint/types/type_facts.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;
using types::TypeFacts;
using types::TypeId;

constexpr Message kMessages[] = {
    {"contextuallyUnnecessary",
     "This assertion is unnecessary since the receiver accepts the original type of "
     "the expression."},
    {"unnecessaryAssertion",
     "This assertion is unnecessary since it does not change the type of the "
     "expression."},
};

/** Replaces `x!` by `x`, dropping the non-null assertion. */
FixFn removeBang(Node *node)
{
  return [node](ast::Fixer &fixer) {
    Node *inner = ast::TSNonNullExpression(node).expression();
    fixer.detach(inner);
    fixer.replace(node, inner);
  };
}

class Checker {
public:
  explicit Checker(RuleContext *ctx) : m_ctx(*ctx)
  {
    m_strictNullChecks = facts().strictOption("strictNullChecks");
  }

  void checkNonNull(Node *node)
  {
    ast::TSNonNullExpression view(node);
    Node *expression = view.expression();
    Node *parent = node->parent;

    // `x! = y`: the assertion on the assignment target never changes the value's
    // type, so it is always unnecessary. Other `=` targets are left alone, since a
    // `!` there can still narrow the type flow of later code.
    if (parent && parent->kind == NodeKind::AssignmentExpression) {
      ast::AssignmentExpression assign(parent);
      if (assign.op() == ast::AssignmentOperator::Assign) {
        if (assign.left() == node) {
          report(node, "contextuallyUnnecessary");
        }
        return;
      }
    }

    TypeId actual = facts().typeOf(expression);
    if (!actual) {
      return;
    }
    TypeId constrained = constrainedType(actual);

    if (!isNullableLike(constrained) && !isNullableLike(actual)) {
      if (expression->kind == NodeKind::Identifier &&
          isPossiblyUsedBeforeAssigned(expression))
      {
        return;
      }
      report(node, "unnecessaryAssertion");
      return;
    }

    // The operand is nullable, so the `!` is only redundant when the surrounding
    // context accepts each nullable member the operand carries.
    if (constrained != actual) {
      return;
    }
    TypeId contextual = contextualTypeAt(node);
    if (!contextual) {
      return;
    }
    uint32_t constrainedFlags = unionFlags(constrained);
    uint32_t contextualFlags = unionFlags(contextual);
    if ((constrainedFlags & tsgo::TypeFlags::Unknown) &&
        !(contextualFlags & tsgo::TypeFlags::Unknown))
    {
      return;
    }
    // Under strictNullChecks `null` and `undefined` are distinct, so the context
    // must admit whichever nullable members the operand actually has.
    if (!accepts(constrainedFlags, contextualFlags, tsgo::TypeFlags::Undefined) ||
        !accepts(constrainedFlags, contextualFlags, tsgo::TypeFlags::Null) ||
        !accepts(constrainedFlags, contextualFlags, tsgo::TypeFlags::Void))
    {
      return;
    }
    report(node, "contextuallyUnnecessary");
  }

private:
  TypeFacts &facts()
  {
    return *m_ctx.types();
  }

  void report(Node *node, const char *messageId)
  {
    Report r;
    r.node = node;
    r.messageId = messageId;
    r.fix = removeBang(node);
    m_ctx.report(std::move(r));
  }

  /** The base constraint of a type parameter, or the type itself. */
  TypeId constrainedType(TypeId type)
  {
    if (facts().isTypeParameter(type)) {
      if (TypeId constraint = facts().constraintOf(type)) {
        return constraint;
      }
    }
    return type;
  }

  /** Nullable in the sense the rule needs: `null`, `undefined`, `void`, or the
   * open `any`/`unknown` that could hold either. */
  bool isNullableLike(TypeId type)
  {
    return facts().isNullable(type) || facts().isAnyLike(type);
  }

  static bool accepts(uint32_t operand, uint32_t context, uint32_t flag)
  {
    return (operand & flag) ? (context & flag) != 0 : true;
  }

  /** The contextual type of the target `node` flows into, but only for the
   * positions that carry one: a call or `new` argument, the initializer of a
   * type-annotated variable or class field, and the right side of a plain `=`.
   * Every other position (a property value, an array element, a template span, a
   * return) yields no contextual type, matching typescript-eslint's
   * `getContextualType`. A `!` never appears as a bare property-value identifier,
   * so that identifier-only case is not reachable here. */
  TypeId contextualTypeAt(Node *node)
  {
    Node *parent = node->parent;
    if (!parent) {
      return 0;
    }
    switch (parent->kind) {
    case NodeKind::CallExpression:
      if (ast::CallExpression(parent).callee() == node) {
        return 0;
      }
      return facts().contextualTypeOf(node);
    case NodeKind::NewExpression:
      if (ast::NewExpression(parent).callee() == node) {
        return 0;
      }
      return facts().contextualTypeOf(node);
    case NodeKind::VariableDeclarator: {
      ast::VariableDeclarator declarator(parent);
      Node *id = declarator.id();
      if (declarator.init() == node && id && id->kind == NodeKind::Identifier &&
          ast::Identifier(id).typeAnnotation())
      {
        return facts().contextualTypeOf(node);
      }
      return 0;
    }
    case NodeKind::PropertyDefinition: {
      ast::PropertyDefinition prop(parent);
      if (prop.value() == node && prop.typeAnnotation()) {
        return facts().contextualTypeOf(node);
      }
      return 0;
    }
    case NodeKind::AssignmentExpression: {
      ast::AssignmentExpression assign(parent);
      if (assign.op() == ast::AssignmentOperator::Assign && assign.right() == node) {
        return facts().contextualTypeOf(node);
      }
      return 0;
    }
    default:
      return 0;
    }
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

  /** Skips an identifier that may be read before it is assigned, so removing the
   * `!` would not turn valid code into a use-before-assignment error. A variable
   * declared without an initializer and without a definite-assignment `!` holds no
   * value at its declaration, so its `!` is left in place. */
  bool isPossiblyUsedBeforeAssigned(Node *id)
  {
    if (!m_strictNullChecks) {
      return false;
    }
    ast::Reference *ref = m_ctx.bindings().referenceOf(id);
    if (!ref || !ref->resolved) {
      // The declaration is unknown, so assume the worst and skip.
      return true;
    }
    ast::Declaration *decl = ref->resolved;
    if (decl->isVariable() && decl->node &&
        decl->node->kind == NodeKind::VariableDeclarator)
    {
      ast::VariableDeclarator declarator(decl->node);
      if (!declarator.init() && !declarator.isDefinite()) {
        return true;
      }
    }
    return false;
  }

  RuleContext &m_ctx;
  bool m_strictNullChecks = true;
};

void create(RuleContext &ctx)
{
  Checker *checker = ctx.state<Checker>(&ctx);
  ctx.on(NodeKind::TSNonNullExpression,
         [checker](Node *node) { checker->checkNonNull(node); });
}

} // namespace

const RuleDef kNoUnnecessaryTypeAssertion{
    {
        "no-unnecessary-type-assertion",
        "Disallow type assertions that do not change the type of an expression",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/"
        "no-unnecessary-type-assertion.md",
        /*recommended=*/true,
        /*fixable=*/true,
        /*hasSuggestions=*/false,
        /*typeAware=*/true,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
