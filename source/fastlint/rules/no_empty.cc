// no-empty: disallow empty block statements (docs/rules/no-empty.md).

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"

namespace fastlint::rules {

namespace {

using namespace lint;

constexpr Message kMessages[] = {
    {"unexpected", "Empty {{type}} statement."},
};

void create(RuleContext &ctx)
{
  const JsonValue *options = ctx.option(0);
  bool allowEmptyCatch = options && options->getBool("allowEmptyCatch", false);
  const syntax::GrammarTree &tree = *ctx.file().grammar();

  ctx.on(ast::NodeKind::BlockStatement, [&ctx, &tree, allowEmptyCatch](ast::Node *node) {
    if (ast::BlockStatement(node).body().size() != 0) {
      return;
    }
    ast::Node *parent = node->parent;
    if (parent && ast::FunctionLike::matches(parent->kind)) {
      return;
    }
    if (allowEmptyCatch && parent && parent->kind == ast::NodeKind::CatchClause) {
      return;
    }
    if (hasCommentBetween(tree, node->start, node->end)) {
      return;
    }
    ctx.report(node, "unexpected", {{"type", "block"}});
  });

  ctx.on(ast::NodeKind::SwitchStatement, [&ctx, &tree](ast::Node *node) {
    ast::SwitchStatement view(node);
    if (view.cases().size() != 0) {
      return;
    }
    uint32_t open = findToken(ctx.source(), view.discriminant()->end, node->end, "{");
    if (hasCommentBetween(tree, open, node->end)) {
      return;
    }
    Report r;
    r.node = node;
    r.at(open, node->end);
    r.messageId = "unexpected";
    r.data.append({"type", "switch"});
    ctx.report(std::move(r));
  });
}

const char kSchema[] =
    R"([{"type":"object","properties":{"allowEmptyCatch":{"type":"boolean"}},"additionalProperties":false}])";

} // namespace

const RuleDef kNoEmpty{
    {
        "no-empty",
        "Disallow empty block statements",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/no-empty.md",
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
