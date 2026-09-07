// no-duplicate-case: disallow duplicate case labels (docs/rules/no-duplicate-case.md).

#include "fastlint/ast/template.h"
#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"

namespace fastlint::rules {

namespace {

using namespace lint;

constexpr Message kMessages[] = {
    {"unexpected", "Duplicate case label."},
};

void create(RuleContext &ctx)
{
  ctx.on(ast::NodeKind::SwitchStatement, [&ctx](ast::Node *node) {
    Vector<ast::Node *, 8> seen;
    for (ast::Node *switchCase : ast::SwitchStatement(node).cases()) {
      ast::Node *test = ast::SwitchCase(switchCase).test();
      if (!test) {
        continue;
      }
      bool duplicate = false;
      for (ast::Node *previous : seen) {
        if (ast::equivalent(previous, test)) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) {
        ctx.report(switchCase, "unexpected");
      } else {
        seen.append(test);
      }
    }
  });
}

} // namespace

const RuleDef kNoDuplicateCase{
    {
        "no-duplicate-case",
        "Disallow duplicate case labels",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/no-duplicate-case.md",
        /*recommended=*/true,
        /*fixable=*/false,
        /*hasSuggestions=*/false,
        /*typeAware=*/false,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
