// no-unsafe-call: disallow calling a value with type `any`
// (docs/rules/no-unsafe-call.md). typescript-eslint's rule over `TypeFacts`.

#include "fastlint/rules/rules.h"
#include "fastlint/rules/unsafe.h"
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
    {"errorCall", "Unsafe call of a type that could not be resolved."},
    {"errorCallThis", "Unsafe call of a `this` type that could not be resolved."},
    {"errorNew", "Unsafe construction of a type that could not be resolved."},
    {"errorTemplateTag",
     "Unsafe use of a template tag whose type could not be resolved."},
    {"unsafeCall", "Unsafe call of {{type}} typed value."},
    {"unsafeCallThis",
     "Unsafe call of {{type}} typed value. `this` is typed as {{type}}."
     "\nYou can try to fix this by turning on the `noImplicitThis` compiler option, or "
     "adding a `this` parameter to the function."},
    {"unsafeNew", "Unsafe construction of {{type}} typed value."},
    {"unsafeTemplateTag", "Unsafe use of {{type}} typed template tag."},
};

class Checker {
public:
  explicit Checker(RuleContext *ctx) : m_ctx(*ctx)
  {
    m_noImplicitThis = facts().strictOption("noImplicitThis");
  }

  void checkCall(Node *node, Node *reporting, const char *unsafeId, const char *errorId)
  {
    TypeId type = unsafe::constrained(facts(), facts().typeOf(node));
    if (!type) {
      return;
    }
    if (facts().isAny(type)) {
      if (!m_noImplicitThis) {
        Node *self = unsafe::thisExpressionOf(node);
        if (self && facts().isAny(unsafe::constrained(facts(), facts().typeOf(self)))) {
          unsafeId = "unsafeCallThis";
          errorId = "errorCallThis";
        }
      }
      m_ctx.report(reporting,
                   facts().isErrorType(type) ? errorId : unsafeId,
                   {{"type", "an `any`"}});
      return;
    }
    if (!facts().isBuiltin(type, "Function")) {
      return;
    }
    // A `Function` is safe to construct with a construct signature or a call signature
    // returning something other than `void`, and safe to call with either signature.
    Vector<types::Signature> signatures;
    if (facts().constructSignatures(type, signatures) && !signatures.isEmpty()) {
      return;
    }
    facts().callSignatures(type, signatures);
    if (string_view(unsafeId) == "unsafeNew") {
      for (const types::Signature &signature : signatures) {
        if (!(facts().flags(signature.returnType) & tsgo::TypeFlags::Void)) {
          return;
        }
      }
    } else if (!signatures.isEmpty()) {
      return;
    }
    m_ctx.report(reporting, unsafeId, {{"type", "a `Function`"}});
  }

private:
  TypeFacts &facts()
  {
    return *m_ctx.types();
  }

  RuleContext &m_ctx;
  bool m_noImplicitThis = true;
};

void create(RuleContext &ctx)
{
  Checker *checker = ctx.state<Checker>(&ctx);
  ctx.on(NodeKind::CallExpression, [checker](Node *node) {
    Node *callee = ast::CallExpression(node).callee();
    checker->checkCall(callee, callee, "unsafeCall", "errorCall");
  });
  ctx.on(NodeKind::NewExpression, [checker](Node *node) {
    checker->checkCall(ast::NewExpression(node).callee(), node, "unsafeNew", "errorNew");
  });
  ctx.on(NodeKind::TaggedTemplateExpression, [checker](Node *node) {
    Node *tag = ast::TaggedTemplateExpression(node).tag();
    checker->checkCall(tag, tag, "unsafeTemplateTag", "errorTemplateTag");
  });
}

} // namespace

const RuleDef kNoUnsafeCall{
    {
        "no-unsafe-call",
        "Disallow calling a value with type `any`",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/no-unsafe-call.md",
        /*recommended=*/true,
        /*fixable=*/false,
        /*hasSuggestions=*/false,
        /*typeAware=*/true,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
