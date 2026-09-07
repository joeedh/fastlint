// no-console: disallow `console` member access (docs/rules/no-console.md).

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"

namespace fastlint::rules {

namespace {

using namespace lint;

constexpr Message kMessages[] = {
    {"unexpected", "Unexpected console statement."},
    {"limited",
     "Unexpected console statement. Only these console methods are allowed: "
     "{{allowed}}."},
    {"removeConsole", "Remove the console.{{propertyName}}()."},
    {"removeMethodCall", "Remove the console method call."},
};

struct Options {
  Vector<string_view, 4> allowed;
  string joined;
};

/** Whether removing the statement around `member` is a safe suggestion. */
ast::Node *removableStatement(ast::Node *member)
{
  ast::Node *call = member->parent;
  if (!call || call->kind != ast::NodeKind::CallExpression || call->children[0] != member)
  {
    return nullptr;
  }
  ast::Node *statement = call->parent;
  if (!statement || statement->kind != ast::NodeKind::ExpressionStatement ||
      !statement->parent || !isStatementListParent(statement->parent))
  {
    return nullptr;
  }
  return statement;
}

void create(RuleContext &ctx)
{
  Options *options = ctx.state<Options>();
  if (const JsonValue *allow = ctx.option(0) ? ctx.option(0)->get("allow") : nullptr) {
    for (int i = 0; i < allow->size(); i++) {
      string_view name = allow->at(i)->asString();
      options->allowed.append(name);
      if (i > 0) {
        options->joined += ", ";
      }
      for (char c : name) {
        options->joined += c;
      }
    }
  }

  ctx.on(ast::NodeKind::MemberExpression, [&ctx, options](ast::Node *node) {
    ast::MemberExpression member(node);
    ast::Node *object = member.object();
    if (!object->isIdentifier("console") || !isGlobalReference(ctx, object)) {
      return;
    }
    string_view property = staticMemberName(node);
    if (!property.empty() && options->allowed.contains(property)) {
      return;
    }
    Report r;
    r.node = node;
    r.messageId = options->allowed.isEmpty() ? "unexpected" : "limited";
    r.data.append(
        {"allowed", string_view(options->joined.c_str(), options->joined.size())});
    if (ast::Node *statement = removableStatement(node)) {
      Suggestion s;
      if (member.isComputed()) {
        s.messageId = "removeMethodCall";
      } else {
        s.messageId = "removeConsole";
        s.data.append({"propertyName", member.property()->text});
      }
      s.fix = [statement](ast::Fixer &fixer) { fixer.remove(statement); };
      r.suggestions.append(std::move(s));
    }
    ctx.report(std::move(r));
  });
}

} // namespace

const RuleDef kNoConsole{
    {
        "no-console",
        "Disallow the use of `console`",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/no-console.md",
        /*recommended=*/false,
        /*fixable=*/false,
        /*hasSuggestions=*/true,
        /*typeAware=*/false,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
