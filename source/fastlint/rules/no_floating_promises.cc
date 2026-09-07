// no-floating-promises: require Promise-like statements to be handled
// (docs/rules/no-floating-promises.md). typescript-eslint's rule over `TypeFacts`.

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"
#include "fastlint/types/type_facts.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;
using types::TypeFacts;
using types::TypeId;

constexpr const char *kBase =
    "Promises must be awaited, end with a call to .catch, or end with a call to .then "
    "with a rejection handler.";
constexpr const char *kBaseVoid =
    "Promises must be awaited, end with a call to .catch, end with a call to .then with "
    "a "
    "rejection handler or be explicitly marked as ignored with the `void` operator.";

constexpr Message kMessages[] = {
    {"floating", kBase},
    {"floatingFixAwait", "Add await operator."},
    {"floatingFixVoid", "Add void operator to ignore."},
    {"floatingPromiseArray",
     "An array of Promises may be unintentional. Consider handling the promises' "
     "fulfillment or rejection with Promise.all or similar."},
    {"floatingPromiseArrayVoid",
     "An array of Promises may be unintentional. Consider handling the promises' "
     "fulfillment or rejection with Promise.all or similar, or explicitly marking the "
     "expression as ignored with the `void` operator."},
    {"floatingUselessRejectionHandler",
     "Promises must be awaited, end with a call to .catch, or end with a call to .then "
     "with a rejection handler. A rejection handler that is not a function will be "
     "ignored."},
    {"floatingUselessRejectionHandlerVoid",
     "Promises must be awaited, end with a call to .catch, end with a call to .then with "
     "a "
     "rejection handler or be explicitly marked as ignored with the `void` operator. A "
     "rejection handler that is not a function will be ignored."},
    {"floatingVoid", kBaseVoid},
};

struct Options {
  bool ignoreVoid = true;
  bool ignoreIIFE = false;
  bool checkThenables = false;
};

void readOptions(RuleContext &ctx, Options &o)
{
  if (const JsonValue *opt = ctx.option()) {
    o.ignoreVoid = opt->getBool("ignoreVoid", true);
    o.ignoreIIFE = opt->getBool("ignoreIIFE", false);
    o.checkThenables = opt->getBool("checkThenables", false);
  }
}

struct Verdict {
  bool unhandled = false;
  bool nonFunctionHandler = false;
  bool promiseArray = false;
};

/** The syntactic shape of a `.then`/`.catch`/`.finally` call on `call`'s callee. */
struct HandlerCall {
  bool matched = false;
  Node *object = nullptr;
  Node *onRejected = nullptr;
};

HandlerCall parseHandlerCall(Node *call, string_view method)
{
  HandlerCall result;
  ast::CallExpression view(call);
  Node *callee = view.callee();
  if (callee->kind != NodeKind::MemberExpression || staticMemberName(callee) != method) {
    return result;
  }
  result.matched = true;
  result.object = ast::MemberExpression(callee).object();
  litestl::util::span<Node *> args = view.arguments();
  auto notSpread = [](Node *n) { return n && n->kind != NodeKind::SpreadElement; };
  if (method == "catch" && args.size() >= 1 && notSpread(args[0])) {
    result.onRejected = args[0];
  } else if (method == "then" && args.size() >= 2 && notSpread(args[0]) &&
             notSpread(args[1]))
  {
    result.onRejected = args[1];
  }
  return result;
}

bool isVoid(const Node *n)
{
  return n->kind == NodeKind::UnaryExpression &&
         ast::UnaryExpression(const_cast<Node *>(n)).op() == ast::UnaryOperator::Void;
}

class Checker {
public:
  Checker(RuleContext *ctx, const Options *options) : m_ctx(*ctx), m_options(*options)
  {
  }

  void checkStatement(Node *statement)
  {
    if (m_options.ignoreIIFE && isAsyncIife(statement)) {
      return;
    }
    check(statement, ast::ExpressionStatement(statement).expression());
  }

  void checkArrowBody(Node *arrow)
  {
    Node *body = ast::ArrowFunctionExpression(arrow).body();
    if (body && body->kind == NodeKind::UnaryExpression) {
      check(body, body);
    }
  }

private:
  TypeFacts &facts()
  {
    return *m_ctx.types();
  }

  /** Reports `node` when `expression` leaves a promise unhandled. */
  void check(Node *node, Node *expression)
  {
    Verdict verdict = isUnhandled(expression);
    if (!verdict.unhandled) {
      return;
    }
    Report r;
    r.node = node;
    if (verdict.promiseArray) {
      r.messageId =
          m_options.ignoreVoid ? "floatingPromiseArrayVoid" : "floatingPromiseArray";
      m_ctx.report(std::move(r));
      return;
    }
    if (m_options.ignoreVoid) {
      r.messageId = verdict.nonFunctionHandler ? "floatingUselessRejectionHandlerVoid"
                                               : "floatingVoid";
      Suggestion voidFix;
      voidFix.messageId = "floatingFixVoid";
      voidFix.fix = [expression](ast::Fixer &fixer) {
        wrap(fixer, expression, [&](Node *inner) {
          return fixer.unary(ast::UnaryOperator::Void, inner);
        });
      };
      r.suggestions.append(std::move(voidFix));
    } else {
      r.messageId =
          verdict.nonFunctionHandler ? "floatingUselessRejectionHandler" : "floating";
    }
    Suggestion awaitFix;
    awaitFix.messageId = "floatingFixAwait";
    awaitFix.fix = [expression](ast::Fixer &fixer) { addAwait(fixer, expression); };
    r.suggestions.append(std::move(awaitFix));
    m_ctx.report(std::move(r));
  }

  /** Replaces `expression` in its parent's slot with `build(expression)`. */
  template <typename Build>
  static void wrap(ast::Fixer &fixer, Node *expression, Build build)
  {
    Node *parent = expression->parent;
    int slot = -1;
    for (int i = 0; parent && i < int(parent->children.size()); i++) {
      if (parent->children[i] == expression) {
        slot = i;
      }
    }
    if (slot < 0) {
      return;
    }
    fixer.detach(expression);
    fixer.set(parent, slot, build(expression));
  }

  static void addAwait(ast::Fixer &fixer, Node *expression)
  {
    if (isVoid(expression)) {
      Node *argument = ast::UnaryExpression(expression).argument();
      fixer.detach(argument);
      fixer.replace(expression, fixer.awaitExpression(argument));
      return;
    }
    wrap(fixer, expression, [&](Node *inner) { return fixer.awaitExpression(inner); });
  }

  static bool isAsyncIife(Node *statement)
  {
    Node *expression = ast::ExpressionStatement(statement).expression();
    if (expression->kind != NodeKind::CallExpression) {
      return false;
    }
    NodeKind callee = ast::CallExpression(expression).callee()->kind;
    return callee == NodeKind::ArrowFunctionExpression ||
           callee == NodeKind::FunctionExpression;
  }

  Verdict isUnhandled(Node *node)
  {
    if (node->kind == NodeKind::AssignmentExpression) {
      return {};
    }
    // Every operand of a comma expression may hold a promise, whatever the whole
    // expression's type.
    if (node->kind == NodeKind::SequenceExpression) {
      for (Node *item : ast::SequenceExpression(node).expressions()) {
        Verdict v = isUnhandled(item);
        if (v.unhandled) {
          return v;
        }
      }
      return {};
    }
    if (!m_options.ignoreVoid && isVoid(node)) {
      return isUnhandled(ast::UnaryExpression(node).argument());
    }
    if (isPromiseArray(node)) {
      return {true, false, true};
    }
    // The checker types `await (p as Promise<T> & T)` as the intersection, not `T`.
    if (node->kind == NodeKind::AwaitExpression) {
      return {};
    }
    if (!isPromiseLike(facts().typeOf(node))) {
      return {};
    }
    if (node->kind == NodeKind::CallExpression) {
      HandlerCall handler = parseHandlerCall(node, "catch");
      if (!handler.matched) {
        handler = parseHandlerCall(node, "then");
      }
      if (handler.matched) {
        if (!handler.onRejected) {
          return {true, false, false};
        }
        if (facts().isCallable(facts().typeOf(handler.onRejected))) {
          return {};
        }
        return {true, true, false};
      }
      HandlerCall finally = parseHandlerCall(node, "finally");
      if (finally.matched) {
        return isUnhandled(finally.object);
      }
      return {true, false, false};
    }
    if (node->kind == NodeKind::ConditionalExpression) {
      ast::ConditionalExpression view(node);
      Verdict alternate = isUnhandled(view.alternate());
      return alternate.unhandled ? alternate : isUnhandled(view.consequent());
    }
    if (node->kind == NodeKind::LogicalExpression) {
      ast::LogicalExpression view(node);
      Verdict left = isUnhandled(view.left());
      return left.unhandled ? left : isUnhandled(view.right());
    }
    return {true, false, false};
  }

  /** Union members of `type`, or `type` itself. */
  void parts(TypeId type, Vector<TypeId, 4> &out)
  {
    out.clear();
    litestl::util::span<const TypeId> members = facts().unionMembers(type);
    if (members.size() == 0) {
      out.append(type);
      return;
    }
    for (TypeId member : members) {
      out.append(member);
    }
  }

  bool isPromiseArray(Node *node)
  {
    TypeId type = facts().typeOf(node);
    if (!type) {
      return false;
    }
    Vector<TypeId, 4> members;
    parts(type, members);
    for (TypeId member : members) {
      TypeId apparent = facts().apparentType(member);
      if (facts().isArray(apparent)) {
        litestl::util::span<const TypeId> args = facts().typeArguments(apparent);
        if (args.size() > 0 && isPromiseLike(args[0])) {
          return true;
        }
      }
      if (facts().isTuple(apparent)) {
        for (TypeId element : facts().typeArguments(apparent)) {
          if (isPromiseLike(element)) {
            return true;
          }
        }
      }
    }
    return false;
  }

  bool isPromiseLike(TypeId type)
  {
    if (!type) {
      return false;
    }
    Vector<TypeId, 4> members;
    parts(facts().apparentType(type), members);
    for (TypeId member : members) {
      if (facts().isBuiltin(member, "Promise")) {
        return true;
      }
    }
    // Only a thenable that can be rejected through a second callback counts.
    return m_options.checkThenables && facts().isThenable(type, 2);
  }

  RuleContext &m_ctx;
  const Options &m_options;
};

void create(RuleContext &ctx)
{
  Options *options = ctx.state<Options>();
  readOptions(ctx, *options);
  Checker *checker = ctx.state<Checker>(&ctx, options);
  ctx.on(NodeKind::ExpressionStatement,
         [checker](Node *node) { checker->checkStatement(node); });
  ctx.on(NodeKind::ArrowFunctionExpression,
         [checker](Node *node) { checker->checkArrowBody(node); });
}

} // namespace

const RuleDef kNoFloatingPromises{
    {
        "no-floating-promises",
        "Require Promise-like statements to be handled appropriately",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/"
        "no-floating-promises.md",
        /*recommended=*/true,
        /*fixable=*/false,
        /*hasSuggestions=*/true,
        /*typeAware=*/true,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
