// prefer-as-const: `as const` over a literal type that repeats the value
// (docs/rules/prefer-as-const.md).

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;

constexpr Message kMessages[] = {
    {"preferConstAssertion", "Expected a `const` instead of a literal type assertion."},
    {"variableConstAssertion",
     "Expected a `const` assertion instead of a literal type annotation."},
    {"variableSuggest", "You should use `as const` instead of type annotation."},
};

Node *constType(ast::Fixer &fixer)
{
  return fixer.typeReference(fixer.identifier("const"));
}

/** Whether `type` is a literal type spelling the same token as `value`. */
bool sameLiteral(Node *value, Node *type)
{
  if (value->kind != NodeKind::Literal || type->kind != NodeKind::TSLiteralType) {
    return false;
  }
  Node *literal = ast::TSLiteralType(type).literal();
  return literal->kind == NodeKind::Literal && literal->text == value->text;
}

/**
 * Compares the value and type of an assertion (`canFix`) or an annotated
 * declaration; `owner` holds the type in `typeSlot`, and for a declaration
 * `valueOwner` holds the value in `valueSlot`.
 */
void compare(RuleContext &ctx,
             Node *value,
             Node *type,
             Node *owner,
             int typeSlot,
             bool canFix,
             Node *valueOwner,
             int valueSlot)
{
  if (!value || !type || !sameLiteral(value, type)) {
    return;
  }
  Report r;
  r.node = type;
  if (canFix) {
    r.messageId = "preferConstAssertion";
    r.fix = [owner, typeSlot](ast::Fixer &fixer) {
      fixer.set(owner, typeSlot, constType(fixer));
    };
  } else {
    r.messageId = "variableConstAssertion";
    Suggestion s;
    s.messageId = "variableSuggest";
    s.fix = [owner, typeSlot, value, valueOwner, valueSlot](ast::Fixer &fixer) {
      fixer.set(owner, typeSlot, nullptr);
      fixer.detach(value);
      Node *assertion = fixer.build(NodeKind::TSAsExpression, {value, constType(fixer)});
      fixer.set(valueOwner, valueSlot, assertion);
    };
    r.suggestions.append(std::move(s));
  }
  ctx.report(std::move(r));
}

void create(RuleContext &ctx)
{
  ctx.on(NodeKind::TSAsExpression, [&ctx](Node *node) {
    ast::TSAsExpression view(node);
    compare(ctx, view.expression(), view.typeAnnotation(), node, 1, true, nullptr, 0);
  });
  ctx.on(NodeKind::TSTypeAssertion, [&ctx](Node *node) {
    ast::TSTypeAssertion view(node);
    compare(ctx, view.expression(), view.typeAnnotation(), node, 0, true, nullptr, 0);
  });
  ctx.on(NodeKind::VariableDeclarator, [&ctx](Node *node) {
    ast::VariableDeclarator view(node);
    Node *id = view.id();
    if (!view.init()) {
      return;
    }
    // Identifiers and patterns keep their annotation in slot 0.
    Node *annotation = nullptr;
    switch (id->kind) {
    case NodeKind::Identifier:
      annotation = ast::Identifier(id).typeAnnotation();
      break;
    case NodeKind::ArrayPattern:
      annotation = ast::ArrayPattern(id).typeAnnotation();
      break;
    case NodeKind::ObjectPattern:
      annotation = ast::ObjectPattern(id).typeAnnotation();
      break;
    default:
      return;
    }
    compare(ctx, view.init(), annotation, id, 0, false, node, 1);
  });
  ctx.on(NodeKind::PropertyDefinition, [&ctx](Node *node) {
    ast::PropertyDefinition view(node);
    compare(ctx, view.value(), view.typeAnnotation(), node, 2, false, node, 3);
  });
}

} // namespace

const RuleDef kPreferAsConst{
    {
        "prefer-as-const",
        "Enforce the use of `as const` over literal type",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/prefer-as-const.md",
        /*recommended=*/true,
        /*fixable=*/true,
        /*hasSuggestions=*/true,
        /*typeAware=*/false,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
