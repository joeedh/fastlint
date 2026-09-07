// no-non-null-assertion: disallow the `!` postfix operator
// (docs/rules/no-non-null-assertion.md), suggesting `?.` where one fits.

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;

constexpr Message kMessages[] = {
    {"noNonNull", "Forbidden non-null assertion."},
    {"suggestOptionalChain",
     "Consider using the optional chain operator `?.` instead. This operator includes "
     "runtime "
     "checks, so it is safer than the compile-only non-null assertion operator."},
};

bool isAssignee(const Node *node)
{
  const Node *parent = node->parent;
  if (!parent) {
    return false;
  }
  switch (parent->kind) {
  case NodeKind::AssignmentExpression:
    return parent->children[0] == node;
  case NodeKind::UnaryExpression:
    return ast::UnaryExpression(const_cast<Node *>(parent)).op() ==
           ast::UnaryOperator::Delete;
  case NodeKind::UpdateExpression:
    return true;
  case NodeKind::ArrayPattern:
  case NodeKind::RestElement:
  case NodeKind::ForInStatement:
  case NodeKind::ForOfStatement:
    return parent->kind != NodeKind::ForInStatement &&
                   parent->kind != NodeKind::ForOfStatement
               ? true
               : parent->children[0] == node;
  case NodeKind::Property:
    return parent->parent && parent->parent->kind == NodeKind::ObjectPattern &&
           parent->children[1] == node;
  case NodeKind::AssignmentPattern:
    return parent->children[0] == node;
  default:
    return false;
  }
}

/** Replaces `x!` by `x`, and makes the parent optional when `optional` is set. */
FixFn unwrap(Node *node, bool optional)
{
  return [node, optional](ast::Fixer &fixer) {
    Node *parent = node->parent;
    Node *inner = ast::TSNonNullExpression(node).expression();
    fixer.detach(inner);
    if (!fixer.replace(node, inner)) {
      return;
    }
    if (optional && parent) {
      fixer.setFlag(parent, ast::Flag::Optional, true);
    }
  };
}

void create(RuleContext &ctx)
{
  ctx.on(NodeKind::TSNonNullExpression, [&ctx](Node *node) {
    Report r;
    r.node = node;
    r.messageId = "noNonNull";
    Node *parent = node->parent;
    if (parent && parent->kind == NodeKind::MemberExpression &&
        parent->children[0] == node && !isAssignee(parent))
    {
      // `x!.y` becomes `x?.y`; `x!?.y` only drops the `!`.
      bool optional = ast::MemberExpression(parent).isOptional();
      r.suggestions.append(
          Suggestion{"suggestOptionalChain", {}, unwrap(node, !optional)});
    } else if (parent && parent->kind == NodeKind::CallExpression &&
               parent->children[0] == node)
    {
      bool optional = ast::CallExpression(parent).isOptional();
      r.suggestions.append(
          Suggestion{"suggestOptionalChain", {}, unwrap(node, !optional)});
    }
    ctx.report(std::move(r));
  });
}

} // namespace

const RuleDef kNoNonNullAssertion{
    {
        "no-non-null-assertion",
        "Disallow non-null assertions using the `!` postfix operator",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/"
        "no-non-null-assertion.md",
        /*recommended=*/false,
        /*fixable=*/false,
        /*hasSuggestions=*/true,
        /*typeAware=*/false,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
