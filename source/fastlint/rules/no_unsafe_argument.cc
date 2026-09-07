// no-unsafe-argument: disallow calling a function with a value with type `any`
// (docs/rules/no-unsafe-argument.md). typescript-eslint's rule over `TypeFacts`.

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

constexpr Message kMessages[] = {
    {"unsafeArgument",
     "Unsafe argument of type {{sender}} assigned to a parameter of type {{receiver}}."},
    {"unsafeArraySpread", "Unsafe spread of an {{sender}} array type."},
    {"unsafeSpread", "Unsafe spread of an {{sender}} type."},
    {"unsafeTupleSpread",
     "Unsafe spread of a tuple type. The argument is {{sender}} and is assigned to a "
     "parameter of type {{receiver}}."},
};

/** Walks a resolved signature's parameter types as arguments consume them. */
class Parameters {
public:
  Parameters(TypeFacts &facts, const types::Signature &signature)
  {
    int count = int(signature.parameters.size());
    for (int i = 0; i < count; i++) {
      TypeId type = facts.typeOfSymbol(signature.parameters[i]);
      if (i == count - 1 && signature.hasRest()) {
        TypeId rest = unsafe::constrained(facts, type);
        m_restIndex = i;
        if (facts.isTuple(rest)) {
          for (TypeId element : facts.typeArguments(rest)) {
            m_restTuple.append(element);
          }
          m_restKind = Rest::Tuple;
        } else if (TypeId element = facts.numberIndexType(rest)) {
          m_restType = element;
          m_restKind = Rest::Array;
        } else {
          m_restType = rest;
          m_restKind = Rest::Other;
        }
        break;
      }
      m_types.append(type);
    }
  }

  /** After a spread with a rest element, every later argument lands on the rest. */
  void consumeRemaining()
  {
    m_consumed = true;
  }

  /** The parameter type the next argument is assigned to, or 0 past the last. */
  TypeId next()
  {
    int index = m_next++;
    if (index < int(m_types.size()) && !m_consumed) {
      return m_types[index];
    }
    switch (m_restKind) {
    case Rest::None:
      return 0;
    case Rest::Tuple: {
      int last = int(m_restTuple.size()) - 1;
      if (last < 0) {
        return 0;
      }
      int offset = index - m_restIndex;
      return m_consumed || offset >= int(m_restTuple.size()) ? m_restTuple[last]
                                                             : m_restTuple[offset];
    }
    case Rest::Array:
    case Rest::Other:
      return m_restType;
    }
    return 0;
  }

private:
  enum class Rest { None, Tuple, Array, Other };

  Vector<TypeId, 4> m_types;
  Rest m_restKind = Rest::None;
  int m_restIndex = 0;
  TypeId m_restType = 0;
  Vector<TypeId, 4> m_restTuple;
  int m_next = 0;
  bool m_consumed = false;
};

class Checker {
public:
  explicit Checker(RuleContext *ctx) : m_ctx(*ctx)
  {
  }

  void checkArguments(litestl::util::span<Node *> args, Node *callee, Node *call)
  {
    if (args.size() == 0) {
      return;
    }
    // An `any` callee is no-unsafe-call's finding.
    if (facts().isAny(facts().typeOf(callee))) {
      return;
    }
    types::Signature signature;
    if (!facts().resolvedSignature(call, signature)) {
      return;
    }
    Parameters parameters(facts(), signature);
    if (call->kind == NodeKind::TaggedTemplateExpression) {
      // The tag's first parameter takes the `TemplateStringsArray`.
      parameters.next();
    }
    for (Node *argument : args) {
      if (!argument) {
        continue;
      }
      if (argument->kind == NodeKind::SpreadElement) {
        checkSpread(argument, parameters);
        continue;
      }
      TypeId parameter = parameters.next();
      if (!parameter) {
        continue;
      }
      TypeId argumentType = facts().typeOf(argument);
      unsafe::UnsafePair pair;
      if (unsafe::isUnsafeAssignment(facts(), argumentType, parameter, argument, pair)) {
        m_ctx.report(argument,
                     "unsafeArgument",
                     {{"sender", m_texts.describe(facts(), argumentType)},
                      {"receiver", m_texts.describe(facts(), parameter)}});
      }
    }
  }

private:
  TypeFacts &facts()
  {
    return *m_ctx.types();
  }

  void checkSpread(Node *spread, Parameters &parameters)
  {
    TypeId type = facts().typeOf(ast::SpreadElement(spread).argument());
    if (!type) {
      return;
    }
    if (facts().isAny(type)) {
      m_ctx.report(spread, "unsafeSpread", {{"sender", m_texts.describe(facts(), type)}});
      return;
    }
    if (unsafe::isAnyArray(facts(), type)) {
      m_ctx.report(spread, "unsafeArraySpread", {{"sender", describeArray(type)}});
      return;
    }
    if (!facts().isTuple(type)) {
      // Spreading any other iterable would need the element type; left alone.
      return;
    }
    for (TypeId element : facts().typeArguments(type)) {
      TypeId parameter = parameters.next();
      if (!parameter) {
        continue;
      }
      // The elements come from a spread variable, so `new Map()` cannot be recognised.
      unsafe::UnsafePair pair;
      if (unsafe::isUnsafeAssignment(facts(), element, parameter, nullptr, pair)) {
        m_ctx.report(spread,
                     "unsafeTupleSpread",
                     {{"sender", describeTupleElement(element)},
                      {"receiver", m_texts.describe(facts(), parameter)}});
      }
    }
    // The checker does not expose the tuple's element flags, so a rest element is read
    // off the type's text.
    string text = facts().typeToString(type);
    if (std::string_view(text.c_str(), text.size()).find("...") != std::string_view::npos)
    {
      parameters.consumeRemaining();
    }
  }

  string_view describeArray(TypeId type)
  {
    litestl::util::span<const TypeId> args = facts().typeArguments(type);
    if (args.size() > 0 && facts().isErrorType(args[0])) {
      return "error";
    }
    return m_texts.describe(facts(), type);
  }

  string_view describeTupleElement(TypeId type)
  {
    if (facts().isErrorType(type)) {
      return "error typed";
    }
    string text;
    text += "of type `";
    text += facts().typeToString(type);
    text += '`';
    return m_texts.intern(text);
  }

  RuleContext &m_ctx;
  unsafe::Texts m_texts;
};

void create(RuleContext &ctx)
{
  Checker *checker = ctx.state<Checker>(&ctx);
  ctx.on(NodeKind::CallExpression, [checker](Node *node) {
    ast::CallExpression view(node);
    checker->checkArguments(view.arguments(), view.callee(), node);
  });
  ctx.on(NodeKind::NewExpression, [checker](Node *node) {
    ast::NewExpression view(node);
    checker->checkArguments(view.arguments(), view.callee(), node);
  });
  ctx.on(NodeKind::TaggedTemplateExpression, [checker](Node *node) {
    ast::TaggedTemplateExpression view(node);
    Vector<Node *, 4> expressions;
    for (Node *part : ast::TemplateLiteral(view.quasi()).parts()) {
      if (part->kind != NodeKind::TemplateElement) {
        expressions.append(part);
      }
    }
    checker->checkArguments(
        litestl::util::span<Node *>(expressions.data(), expressions.size()),
        view.tag(),
        node);
  });
}

} // namespace

const RuleDef kNoUnsafeArgument{
    {
        "no-unsafe-argument",
        "Disallow calling a function with a value with type `any`",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/no-unsafe-argument.md",
        /*recommended=*/true,
        /*fixable=*/false,
        /*hasSuggestions=*/false,
        /*typeAware=*/true,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
