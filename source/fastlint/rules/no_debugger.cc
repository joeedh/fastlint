// no-debugger: disallow `debugger` statements (docs/rules/no-debugger.md).
// Same semantics as ESLint's rule, plus a fix that removes the statement.

#include "fastlint/rules/rules.h"

namespace fastlint::rules {

namespace {

using namespace lint;

constexpr Message kMessages[] = {
    {"unexpected", "Unexpected 'debugger' statement."},
};

void create(RuleContext &ctx)
{
  ctx.on(ast::NodeKind::DebuggerStatement, [&ctx](ast::Node *node) {
    ctx.report(node, "unexpected", [node](ast::Fixer &fixer) { fixer.remove(node); });
  });
}

} // namespace

const RuleDef kNoDebugger{
    {
        "no-debugger",
        "Disallow the use of `debugger`",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/no-debugger.md",
        /*recommended=*/true,
        /*fixable=*/true,
        /*hasSuggestions=*/false,
        /*typeAware=*/false,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
