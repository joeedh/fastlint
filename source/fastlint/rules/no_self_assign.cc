// no-self-assign: disallow assignments where both sides are the same
// (docs/rules/no-self-assign.md).

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;

constexpr Message kMessages[] = {
    {"selfAssignment", "'{{name}}' is assigned to itself."},
};

struct Reporter {
  RuleContext &ctx;
  bool props;

  void report(Node *node)
  {
    // The name is the source text with its spaces removed, as ESLint prints it.
    string *name = ctx.state<string>();
    for (char c : ctx.textOf(node)) {
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
        *name += c;
      }
    }
    ctx.report(
        node, "selfAssignment", {{"name", string_view(name->c_str(), name->size())}});
  }

  void each(Node *left, Node *right);

  void eachProperty(Node *left, Node *right)
  {
    if (left->kind != NodeKind::Property || right->kind != NodeKind::Property) {
      return;
    }
    ast::Property l(left);
    ast::Property r(right);
    if (r.kind() != ast::PropertyKind::Init || r.isMethod()) {
      return;
    }
    string_view leftName = staticPropertyName(l.key(), l.isComputed());
    if (!leftName.empty() && leftName == staticPropertyName(r.key(), r.isComputed())) {
      each(l.value(), r.value());
    }
  }
};

void Reporter::each(Node *left, Node *right)
{
  if (!left || !right) {
    return;
  }
  if (left->kind == NodeKind::Identifier && right->kind == NodeKind::Identifier) {
    if (left->text == right->text) {
      report(right);
    }
  } else if (left->kind == NodeKind::ArrayPattern &&
             right->kind == NodeKind::ArrayExpression)
  {
    span<Node *> l = ast::ArrayPattern(left).elements();
    span<Node *> r = ast::ArrayExpression(right).elements();
    size_t end = l.size() < r.size() ? l.size() : r.size();
    for (size_t i = 0; i < end; i++) {
      // A rest element before the last right element takes several; stop there.
      if (l[i] && l[i]->kind == NodeKind::RestElement && i + 1 < r.size()) {
        break;
      }
      each(l[i], r[i]);
      if (r[i] && r[i]->kind == NodeKind::SpreadElement) {
        break;
      }
    }
  } else if (left->kind == NodeKind::RestElement &&
             right->kind == NodeKind::SpreadElement)
  {
    each(ast::RestElement(left).argument(), ast::SpreadElement(right).argument());
  } else if (left->kind == NodeKind::ObjectPattern &&
             right->kind == NodeKind::ObjectExpression)
  {
    span<Node *> l = ast::ObjectPattern(left).properties();
    span<Node *> r = ast::ObjectExpression(right).properties();
    if (r.size() == 0) {
      return;
    }
    // Properties before a spread may be overwritten by it, so only those after count.
    size_t startJ = 0;
    for (size_t j = r.size(); j-- > 0;) {
      if (r[j]->kind == NodeKind::SpreadElement) {
        startJ = j + 1;
        break;
      }
    }
    for (size_t i = 0; i < l.size(); i++) {
      for (size_t j = startJ; j < r.size(); j++) {
        eachProperty(l[i], r[j]);
      }
    }
  } else if (props && left->kind == NodeKind::MemberExpression &&
             right->kind == NodeKind::MemberExpression && isSameReference(left, right))
  {
    report(right);
  }
}

void create(RuleContext &ctx)
{
  const JsonValue *options = ctx.option(0);
  bool props = !options || options->getBool("props", true);
  ctx.on(NodeKind::AssignmentExpression, [&ctx, props](Node *node) {
    ast::AssignmentExpression assignment(node);
    switch (assignment.op()) {
    case ast::AssignmentOperator::Assign:
    case ast::AssignmentOperator::AndAssign:
    case ast::AssignmentOperator::OrAssign:
    case ast::AssignmentOperator::NullishAssign:
      break;
    default:
      return;
    }
    Reporter reporter{ctx, props};
    reporter.each(assignment.left(), assignment.right());
  });
}

const char kSchema[] =
    R"([{"type":"object","properties":{"props":{"type":"boolean"}},"additionalProperties":false}])";

} // namespace

const RuleDef kNoSelfAssign{
    {
        "no-self-assign",
        "Disallow assignments where both sides are exactly the same",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/no-self-assign.md",
        /*recommended=*/true,
        /*fixable=*/false,
        /*hasSuggestions=*/false,
        /*typeAware=*/false,
        messagesOf(kMessages),
        kSchema,
    },
    create,
};

} // namespace fastlint::rules
