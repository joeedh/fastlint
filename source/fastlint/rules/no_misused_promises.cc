// no-misused-promises: disallow a Promise where a non-Promise value is expected
// (docs/rules/no-misused-promises.md). typescript-eslint's rule over `TypeFacts`.
// The conditional, spread, void-return-argument and void-return-variable checks are
// ported; the property, return-value, JSX-attribute, inherited-method and `strict`
// flag-unions paths are not (see the docs).

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"
#include "fastlint/tsgo/generated/enums.h"
#include "fastlint/types/type_facts.h"

#include "util/set.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;
using litestl::util::Set;
using litestl::util::Vector;
using types::Signature;
using types::TypeFacts;
using types::TypeId;

constexpr Message kMessages[] = {
    {"conditional", "Expected non-Promise value in a boolean conditional."},
    {"spread", "Expected a non-Promise value to be spread in an object."},
    {"voidReturnArgument",
     "Promise returned in function argument where a void return was expected."},
    {"voidReturnVariable",
     "Promise-returning function provided to variable where a void return was "
     "expected."},
};

class Checker {
public:
  explicit Checker(RuleContext *ctx) : m_ctx(*ctx)
  {
    const JsonValue *opt = ctx->option(0);
    parseConditionals(opt ? opt->get("checksConditionals") : nullptr);
    const JsonValue *spreads = opt ? opt->get("checksSpreads") : nullptr;
    m_checksSpreads =
        spreads && spreads->kind == tsgo::JsonKind::Bool ? spreads->asBool() : true;
    parseVoidReturn(opt ? opt->get("checksVoidReturn") : nullptr);
  }

  void checkTest(Node *test)
  {
    if (m_checksConditionals && test) {
      checkConditional(test, true);
    }
  }

  void checkUnary(Node *node)
  {
    if (!m_checksConditionals) {
      return;
    }
    ast::UnaryExpression view(node);
    if (view.op() == ast::UnaryOperator::Not) {
      checkConditional(view.argument(), true);
    }
  }

  void checkLogicalTop(Node *node)
  {
    if (m_checksConditionals) {
      checkConditional(node, false);
    }
  }

  void checkSpread(Node *node)
  {
    if (!m_checksSpreads) {
      return;
    }
    Node *argument = ast::SpreadElement(node).argument();
    if (isSometimesThenable(facts().typeOf(argument))) {
      report(argument, "spread");
    }
  }

  void checkArguments(Node *call)
  {
    if (!m_voidArguments) {
      return;
    }
    span<Node *> args = call->kind == NodeKind::NewExpression
                            ? ast::NewExpression(call).arguments()
                            : ast::CallExpression(call).arguments();
    if (args.size() == 0) {
      return;
    }
    for (Node *arg : args) {
      // A spread argument breaks the positional mapping to the signature, so the
      // call is left unchecked rather than guessed at.
      if (arg && arg->kind == NodeKind::SpreadElement) {
        return;
      }
    }
    if (call->kind == NodeKind::CallExpression && isPromiseFinally(call)) {
      return;
    }
    Signature signature;
    if (!facts().resolvedSignature(call, signature)) {
      return;
    }
    int count = int(signature.parameters.size());
    for (int i = 0; i < int(args.size()) && i < count; i++) {
      TypeId paramType = facts().typeOfSymbol(signature.parameters[i]);
      if (isVoidReturningFunctionType(paramType) && returnsThenable(args[i])) {
        report(args[i], "voidReturnArgument");
      }
    }
  }

  void checkVariable(Node *node)
  {
    if (!m_voidVariables) {
      return;
    }
    ast::VariableDeclarator view(node);
    Node *id = view.id();
    Node *init = view.init();
    if (!init || !id || id->kind != NodeKind::Identifier ||
        !ast::Identifier(id).typeAnnotation())
    {
      return;
    }
    // A binding identifier has no type of its own; the declared type reaches the
    // initializer as its contextual type.
    if (isVoidReturningFunctionType(facts().contextualTypeOf(init)) &&
        returnsThenable(init))
    {
      report(init, "voidReturnVariable");
    }
  }

  void checkAssignment(Node *node)
  {
    if (!m_voidVariables) {
      return;
    }
    ast::AssignmentExpression view(node);
    if (isVoidReturningFunctionType(facts().typeOf(view.left())) &&
        returnsThenable(view.right()))
    {
      report(view.right(), "voidReturnVariable");
    }
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
    m_ctx.report(std::move(r));
  }

  void checkConditional(Node *node, bool isTestExpr)
  {
    if (!node || !m_seen.add(node)) {
      return;
    }
    if (node->kind == NodeKind::LogicalExpression) {
      ast::LogicalExpression view(node);
      // The left operand of `??` outside a test is not itself a condition; the
      // right operand of any logical outside a test runs for side effects only.
      if (view.op() != ast::LogicalOperator::Nullish || isTestExpr) {
        checkConditional(view.left(), isTestExpr);
      }
      if (isTestExpr) {
        checkConditional(view.right(), isTestExpr);
      }
      return;
    }
    TypeId type = facts().typeOf(node);
    if (isAlwaysThenable(type)) {
      report(node, "conditional");
      return;
    }
    if (m_flagUnionsAll && isSometimesThenable(type)) {
      report(node, "conditional");
    }
  }

  /** Every member of the apparent type is thenable, so the value is a Promise in
   * all branches of a union. */
  bool isAlwaysThenable(TypeId type)
  {
    if (!type) {
      return false;
    }
    Vector<TypeId, 4> members;
    parts(facts().apparentType(type), members);
    for (TypeId member : members) {
      if (!facts().isThenable(member, 1)) {
        return false;
      }
    }
    return true;
  }

  bool isSometimesThenable(TypeId type)
  {
    return type && facts().isThenable(type, 1);
  }

  /** The node's type is a function whose return type is thenable. */
  bool returnsThenable(Node *node)
  {
    TypeId type = facts().typeOf(node);
    if (!type) {
      return false;
    }
    Vector<Signature> signatures;
    facts().callSignatures(type, signatures);
    for (const Signature &sig : signatures) {
      if (facts().isThenable(sig.returnType, 1)) {
        return true;
      }
    }
    return false;
  }

  /** A function type with a `void`-returning call signature and no thenable-returning
   * one; a parameter accepting both admits a Promise, so it does not count. */
  bool isVoidReturningFunctionType(TypeId type)
  {
    if (!type) {
      return false;
    }
    Vector<Signature> signatures;
    facts().callSignatures(type, signatures);
    bool hadVoid = false;
    for (const Signature &sig : signatures) {
      if (facts().isThenable(sig.returnType, 1)) {
        return false;
      }
      if (facts().flags(sig.returnType) & tsgo::TypeFlags::Void) {
        hadVoid = true;
      }
    }
    return hadVoid;
  }

  /** The call is `promise.finally(cb)`, whose callback is allowed to be async. */
  bool isPromiseFinally(Node *call)
  {
    Node *callee = ast::CallExpression(call).callee();
    if (!callee || callee->kind != NodeKind::MemberExpression) {
      return false;
    }
    ast::MemberExpression member(callee);
    Node *property = member.property();
    if (member.isComputed() || !property || property->kind != NodeKind::Identifier ||
        property->text != "finally")
    {
      return false;
    }
    return isSometimesThenable(facts().typeOf(member.object()));
  }

  void parts(TypeId type, Vector<TypeId, 4> &out)
  {
    out.clear();
    span<const TypeId> members = facts().unionMembers(type);
    if (members.size() == 0) {
      out.append(type);
      return;
    }
    for (TypeId member : members) {
      out.append(member);
    }
  }

  void parseConditionals(const JsonValue *value)
  {
    if (!value) {
      return;
    }
    if (value->kind == tsgo::JsonKind::Bool) {
      m_checksConditionals = value->asBool();
      return;
    }
    if (value->isObject()) {
      m_checksConditionals = true;
      m_flagUnionsAll = value->getString("flagUnions") == "all";
    }
  }

  void parseVoidReturn(const JsonValue *value)
  {
    if (!value) {
      return;
    }
    if (value->kind == tsgo::JsonKind::Bool) {
      m_voidArguments = value->asBool();
      m_voidVariables = value->asBool();
      return;
    }
    if (value->isObject()) {
      m_voidArguments = value->getBool("arguments", true);
      m_voidVariables = value->getBool("variables", true);
    }
  }

  RuleContext &m_ctx;
  Set<Node *> m_seen;
  bool m_checksConditionals = true;
  bool m_flagUnionsAll = false;
  bool m_checksSpreads = true;
  bool m_voidArguments = true;
  bool m_voidVariables = true;
};

void create(RuleContext &ctx)
{
  Checker *checker = ctx.state<Checker>(&ctx);
  ctx.on(NodeKind::IfStatement,
         [checker](Node *node) { checker->checkTest(ast::IfStatement(node).test()); });
  ctx.on(NodeKind::WhileStatement,
         [checker](Node *node) { checker->checkTest(ast::WhileStatement(node).test()); });
  ctx.on(NodeKind::DoWhileStatement, [checker](Node *node) {
    checker->checkTest(ast::DoWhileStatement(node).test());
  });
  ctx.on(NodeKind::ForStatement,
         [checker](Node *node) { checker->checkTest(ast::ForStatement(node).test()); });
  ctx.on(NodeKind::ConditionalExpression, [checker](Node *node) {
    checker->checkTest(ast::ConditionalExpression(node).test());
  });
  ctx.on(NodeKind::UnaryExpression, [checker](Node *node) { checker->checkUnary(node); });
  ctx.on(NodeKind::LogicalExpression,
         [checker](Node *node) { checker->checkLogicalTop(node); });
  ctx.on(NodeKind::SpreadElement, [checker](Node *node) { checker->checkSpread(node); });
  ctx.on(NodeKind::CallExpression,
         [checker](Node *node) { checker->checkArguments(node); });
  ctx.on(NodeKind::NewExpression,
         [checker](Node *node) { checker->checkArguments(node); });
  ctx.on(NodeKind::VariableDeclarator,
         [checker](Node *node) { checker->checkVariable(node); });
  ctx.on(NodeKind::AssignmentExpression,
         [checker](Node *node) { checker->checkAssignment(node); });
}

} // namespace

const RuleDef kNoMisusedPromises{
    {
        "no-misused-promises",
        "Disallow Promises in places not designed to handle them",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/"
        "no-misused-promises.md",
        /*recommended=*/true,
        /*fixable=*/false,
        /*hasSuggestions=*/false,
        /*typeAware=*/true,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
