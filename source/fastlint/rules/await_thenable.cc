// await-thenable: disallow awaiting a value that is not a Thenable
// (docs/rules/await-thenable.md). typescript-eslint's rule over `TypeFacts`.

#include "fastlint/rules/rules.h"
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
    {"await", "Unexpected `await` of a non-Promise (non-\"Thenable\") value."},
    {"awaitUsingOfNonAsyncDisposable",
     "Unexpected `await using` of a value that is not async disposable."},
    {"convertToOrdinaryFor", "Convert to an ordinary `for...of` loop."},
    {"forAwaitOfNonAsyncIterable",
     "Unexpected `for await...of` of a value that is not async iterable."},
    {"invalidPromiseAggregatorInput",
     "Unexpected iterable of non-Promise (non-\"Thenable\") values passed to promise "
     "aggregator."},
    {"removeAwait", "Remove unnecessary `await`."},
};

enum class Awaitable { Always, Never, May };

class Checker {
public:
  explicit Checker(RuleContext *ctx) : m_ctx(*ctx)
  {
  }

  void checkAwait(Node *node)
  {
    Node *argument = ast::AwaitExpression(node).argument();
    if (needsToBeAwaited(facts().typeOf(argument)) != Awaitable::Never) {
      return;
    }
    Report r;
    r.node = node;
    r.messageId = "await";
    Suggestion s;
    s.messageId = "removeAwait";
    s.fix = [node, argument](ast::Fixer &fixer) {
      fixer.detach(argument);
      fixer.replace(node, argument);
    };
    r.suggestions.append(std::move(s));
    m_ctx.report(std::move(r));
  }

  void checkForOf(Node *node)
  {
    ast::ForOfStatement view(node);
    if (!view.isAwait()) {
      return;
    }
    TypeId type = facts().typeOf(view.right());
    if (!type || isAny(type) || anyPartHasSymbol(type, "asyncIterator")) {
      return;
    }
    Report r;
    r.node = node;
    // The head runs from `for` to the parenthesis closing it.
    uint32_t close =
        findToken(m_ctx.source(), view.right()->end, view.body()->start, ")");
    r.at(node->start, close == UINT32_MAX ? view.right()->end : close + 1);
    r.messageId = "forAwaitOfNonAsyncIterable";
    Suggestion s;
    s.messageId = "convertToOrdinaryFor";
    s.fix = [node](ast::Fixer &fixer) { fixer.setFlag(node, ast::Flag::Await, false); };
    r.suggestions.append(std::move(s));
    m_ctx.report(std::move(r));
  }

  void checkAwaitUsing(Node *node)
  {
    ast::VariableDeclaration view(node);
    if (view.kind() != ast::VariableKind::AwaitUsing) {
      return;
    }
    litestl::util::span<Node *> declarations = view.declarations();
    for (Node *declarator : declarations) {
      Node *init = ast::VariableDeclarator(declarator).init();
      if (!init) {
        continue;
      }
      TypeId type = facts().typeOf(init);
      if (!type || isAny(type) || anyPartHasSymbol(type, "asyncDispose")) {
        continue;
      }
      Report r;
      r.node = init;
      r.messageId = "awaitUsingOfNonAsyncDisposable";
      // With several declarators the right fix is the user's call.
      if (declarations.size() == 1) {
        Suggestion s;
        s.messageId = "removeAwait";
        s.fix = [node](ast::Fixer &fixer) {
          fixer.setData(node, 0, uint8_t(ast::VariableKind::Using));
        };
        r.suggestions.append(std::move(s));
      }
      m_ctx.report(std::move(r));
    }
  }

  void checkCall(Node *node)
  {
    ast::CallExpression view(node);
    if (!isPromiseAggregator(view)) {
      return;
    }
    litestl::util::span<Node *> args = view.arguments();
    if (args.size() == 0) {
      return;
    }
    Node *argument = args[0];
    if (argument->kind == NodeKind::ArrayExpression) {
      for (Node *element : ast::ArrayExpression(argument).elements()) {
        if (!element) {
          continue;
        }
        TypeId type = constrained(facts().typeOf(element));
        if (type && everyPart(type, Awaitable::Never)) {
          m_ctx.report(element, "invalidPromiseAggregatorInput");
        }
      }
      return;
    }
    TypeId type = constrained(facts().typeOf(argument));
    if (type && isInvalidAggregatorInput(type)) {
      m_ctx.report(argument, "invalidPromiseAggregatorInput");
    }
  }

private:
  TypeFacts &facts()
  {
    return *m_ctx.types();
  }

  bool isAny(TypeId type)
  {
    return (facts().graph().type(type).flags & tsgo::TypeFlags::Any) != 0;
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

  bool anyPartHasSymbol(TypeId type, std::string_view name)
  {
    Vector<TypeId, 4> members;
    parts(type, members);
    for (TypeId member : members) {
      if (facts().hasWellKnownSymbolProperty(member, name)) {
        return true;
      }
    }
    return false;
  }

  /** A type parameter's base constraint, or the type itself. */
  TypeId constrained(TypeId type)
  {
    if (!facts().isTypeParameter(type)) {
      return type;
    }
    TypeId constraint = facts().constraintOf(type);
    return constraint ? constraint : type;
  }

  Awaitable needsToBeAwaited(TypeId type)
  {
    if (!type) {
      return Awaitable::May;
    }
    if (facts().isTypeParameter(type)) {
      // An unconstrained type parameter may be instantiated with a promise.
      type = facts().constraintOf(type);
      if (!type) {
        return Awaitable::May;
      }
    }
    if (facts().isAnyLike(type)) {
      return Awaitable::May;
    }
    return facts().isThenable(type, 1) ? Awaitable::Always : Awaitable::Never;
  }

  bool everyPart(TypeId type, Awaitable verdict)
  {
    Vector<TypeId, 4> members;
    parts(type, members);
    for (TypeId member : members) {
      if (needsToBeAwaited(member) != verdict) {
        return false;
      }
    }
    return true;
  }

  bool somePart(TypeId type, Awaitable verdict)
  {
    Vector<TypeId, 4> members;
    parts(type, members);
    for (TypeId member : members) {
      if (needsToBeAwaited(member) == verdict) {
        return true;
      }
    }
    return false;
  }

  bool isPromiseAggregator(ast::CallExpression call)
  {
    Node *callee = call.callee();
    if (callee->kind != NodeKind::MemberExpression) {
      return false;
    }
    string_view method = staticMemberName(callee);
    if (method != "all" && method != "allSettled" && method != "race" && method != "any")
    {
      return false;
    }
    TypeId type = constrained(facts().typeOf(ast::MemberExpression(callee).object()));
    return facts().isBuiltin(type, "PromiseConstructor");
  }

  /** Element types the aggregator would receive from one iterable type. */
  void valueTypes(TypeId type, Vector<TypeId, 4> &out)
  {
    out.clear();
    if (facts().isTuple(type)) {
      for (TypeId element : facts().typeArguments(type)) {
        out.append(element);
      }
      return;
    }
    if (facts().isArrayLike(type)) {
      if (TypeId element = facts().numberIndexType(type)) {
        out.append(element);
      }
      return;
    }
    litestl::util::span<const TypeId> args = facts().typeArguments(type);
    if (args.size() > 0) {
      out.append(args[0]);
    }
  }

  bool isInvalidAggregatorInput(TypeId type)
  {
    Vector<TypeId, 4> members;
    parts(type, members);
    // Anything but an iterable is already a type error.
    for (TypeId member : members) {
      if (!facts().hasWellKnownSymbolProperty(facts().apparentType(member), "iterator")) {
        return false;
      }
    }
    for (TypeId member : members) {
      Vector<TypeId, 4> values;
      valueTypes(member, values);
      for (TypeId value : values) {
        if (somePart(value, Awaitable::Never)) {
          return true;
        }
      }
    }
    return false;
  }

  RuleContext &m_ctx;
};

void create(RuleContext &ctx)
{
  Checker *checker = ctx.state<Checker>(&ctx);
  ctx.on(NodeKind::AwaitExpression, [checker](Node *node) { checker->checkAwait(node); });
  ctx.on(NodeKind::ForOfStatement, [checker](Node *node) { checker->checkForOf(node); });
  ctx.on(NodeKind::VariableDeclaration,
         [checker](Node *node) { checker->checkAwaitUsing(node); });
  ctx.on(NodeKind::CallExpression, [checker](Node *node) { checker->checkCall(node); });
}

} // namespace

const RuleDef kAwaitThenable{
    {
        "await-thenable",
        "Disallow awaiting a value that is not a Thenable",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/await-thenable.md",
        /*recommended=*/true,
        /*fixable=*/false,
        /*hasSuggestions=*/true,
        /*typeAware=*/true,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
