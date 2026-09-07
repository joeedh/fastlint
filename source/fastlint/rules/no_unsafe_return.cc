// no-unsafe-return: disallow returning a value with type `any` from a function
// (docs/rules/no-unsafe-return.md). typescript-eslint's rule over `TypeFacts`.

#include "fastlint/rules/rules.h"
#include "fastlint/rules/unsafe.h"
#include "fastlint/rules/util.h"
#include "fastlint/types/type_facts.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;
using types::TypeFacts;
using types::TypeId;
using unsafe::AnyKind;

constexpr Message kMessages[] = {
    {"unsafeReturn", "Unsafe return of a value of type {{type}}."},
    {"unsafeReturnAssignment",
     "Unsafe return of type `{{sender}}` from function with return type `{{receiver}}`."},
    {"unsafeReturnThis",
     "Unsafe return of a value of type `{{type}}`. `this` is typed as `any`."
     "\nYou can try to fix this by turning on the `noImplicitThis` compiler option, or "
     "adding a `this` parameter to the function."},
};

class Checker {
public:
  explicit Checker(RuleContext *ctx) : m_ctx(*ctx)
  {
    m_noImplicitThis = facts().strictOption("noImplicitThis");
  }

  void checkArrowBody(Node *arrow)
  {
    ast::ArrowFunctionExpression view(arrow);
    if (view.isExpression() && view.body()) {
      checkReturn(view.body(), view.body(), arrow);
    }
  }

  void checkReturnStatement(Node *node)
  {
    Node *argument = ast::ReturnStatement(node).argument();
    if (!argument) {
      return;
    }
    Node *function = node->parent;
    while (function && !ast::FunctionLike::matches(function->kind)) {
      function = function->parent;
    }
    if (function) {
      checkReturn(argument, node, function);
    }
  }

private:
  TypeFacts &facts()
  {
    return *m_ctx.types();
  }

  void checkReturn(Node *returned, Node *reporting, Node *function)
  {
    TypeId type = facts().typeOf(returned);
    if (!type) {
      return;
    }
    ast::FunctionLike view(function);
    AnyKind anyKind = unsafe::discriminateAny(facts(), type);
    TypeId functionType = functionTypeOf(function);
    Vector<types::Signature> signatures;
    facts().callSignatures(functionType, signatures);

    // An annotated return type that the returned value already has is intentional,
    // unsafe or not.
    if (view.returnType()) {
      for (const types::Signature &signature : signatures) {
        if (signature.returnType == type || facts().isAnyLike(signature.returnType)) {
          return;
        }
        if (view.isAsync()) {
          TypeId awaitedSignature = facts().awaitedType(signature.returnType);
          TypeId awaitedReturned = facts().awaitedType(type);
          if ((awaitedReturned && awaitedReturned == awaitedSignature) ||
              (awaitedSignature && facts().isAnyLike(awaitedSignature)))
          {
            return;
          }
        }
      }
    }

    if (anyKind != AnyKind::Safe) {
      // `unknown` and `unknown[]` return types take `any` and `any[]` on purpose.
      for (const types::Signature &signature : signatures) {
        TypeId declared = signature.returnType;
        if (anyKind == AnyKind::Any && facts().isUnknown(declared)) {
          return;
        }
        if (anyKind == AnyKind::AnyArray && unsafe::isUnknownArray(facts(), declared)) {
          return;
        }
        TypeId awaited = facts().awaitedType(declared);
        if (awaited && anyKind == AnyKind::PromiseAny && facts().isUnknown(awaited)) {
          return;
        }
      }
      if (anyKind == AnyKind::PromiseAny && !view.isAsync()) {
        return;
      }
      const char *messageId = "unsafeReturn";
      if (!m_noImplicitThis) {
        Node *self = unsafe::thisExpressionOf(returned);
        if (self && facts().isAny(unsafe::constrained(facts(), facts().typeOf(self)))) {
          messageId = "unsafeReturnThis";
        }
      }
      const char *text = "`any[]`";
      if (facts().isErrorType(unsafe::constrained(facts(), type))) {
        text = "error";
      } else if (anyKind == AnyKind::Any) {
        text = "`any`";
      } else if (anyKind == AnyKind::PromiseAny) {
        text = "`Promise<any>`";
      }
      m_ctx.report(reporting, messageId, {{"type", text}});
      return;
    }

    if (signatures.isEmpty()) {
      return;
    }
    unsafe::UnsafePair pair;
    if (!unsafe::isUnsafeAssignment(
            facts(), type, signatures[0].returnType, returned, pair))
    {
      return;
    }
    m_ctx.report(reporting,
                 "unsafeReturnAssignment",
                 {{"sender", m_texts.intern(facts().typeToString(pair.sender))},
                  {"receiver", m_texts.intern(facts().typeToString(pair.receiver))}});
  }

  /** The function's type as its receiver sees it: a function expression takes its
   * contextual type, since the checker types its own return from the body. */
  TypeId functionTypeOf(Node *function)
  {
    if (function->kind == NodeKind::FunctionExpression ||
        function->kind == NodeKind::ArrowFunctionExpression)
    {
      if (TypeId contextual = facts().contextualTypeOf(function)) {
        return contextual;
      }
    }
    return facts().typeOf(function);
  }

  RuleContext &m_ctx;
  bool m_noImplicitThis = true;
  unsafe::Texts m_texts;
};

void create(RuleContext &ctx)
{
  Checker *checker = ctx.state<Checker>(&ctx);
  ctx.on(NodeKind::ArrowFunctionExpression,
         [checker](Node *node) { checker->checkArrowBody(node); });
  ctx.on(NodeKind::ReturnStatement,
         [checker](Node *node) { checker->checkReturnStatement(node); });
}

} // namespace

const RuleDef kNoUnsafeReturn{
    {
        "no-unsafe-return",
        "Disallow returning a value with type `any` from a function",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/no-unsafe-return.md",
        /*recommended=*/true,
        /*fixable=*/false,
        /*hasSuggestions=*/false,
        /*typeAware=*/true,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
