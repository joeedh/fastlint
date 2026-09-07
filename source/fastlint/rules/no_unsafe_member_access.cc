// no-unsafe-member-access: disallow member access on a value with type `any`
// (docs/rules/no-unsafe-member-access.md). typescript-eslint's rule over `TypeFacts`.

#include "fastlint/rules/rules.h"
#include "fastlint/rules/unsafe.h"
#include "fastlint/rules/util.h"
#include "fastlint/types/type_facts.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;
using litestl::util::Map;
using types::TypeFacts;
using types::TypeId;

constexpr Message kMessages[] = {
    {"errorComputedMemberAccess",
     "The type of computed name {{property}} cannot be resolved."},
    {"errorMemberExpression",
     "Unsafe member access {{property}} on a type that cannot be resolved."},
    {"errorThisMemberExpression",
     "Unsafe member access {{property}}. The type of `this` cannot be resolved."
     "\nYou can try to fix this by turning on the `noImplicitThis` compiler option, or "
     "adding a `this` parameter to the function."},
    {"unsafeComputedMemberAccess",
     "Computed name {{property}} resolves to an `any` value."},
    {"unsafeMemberExpression", "Unsafe member access {{property}} on an `any` value."},
    {"unsafeThisMemberExpression",
     "Unsafe member access {{property}} on an `any` value. `this` is typed as `any`."
     "\nYou can try to fix this by turning on the `noImplicitThis` compiler option, or "
     "adding a `this` parameter to the function."},
};

enum class State : uint8_t { Unsafe = 1, Safe, Chained };

class Checker {
public:
  explicit Checker(RuleContext *ctx) : m_ctx(*ctx)
  {
    if (const JsonValue *opt = ctx->option()) {
      m_allowOptionalChaining = opt->getBool("allowOptionalChaining", false);
    }
    m_noImplicitThis = facts().strictOption("noImplicitThis");
  }

  void checkMember(Node *node)
  {
    if (inHeritageClause(node)) {
      return;
    }
    checkObject(node);
    ast::MemberExpression view(node);
    if (view.isComputed()) {
      checkComputedKey(view.property(), view.isOptional());
    }
  }

private:
  TypeFacts &facts()
  {
    return *m_ctx.types();
  }

  /** `class B implements FG.A` and `interface B extends FG.A` name types, not values.
   */
  static bool inHeritageClause(Node *node)
  {
    Node *parent = node->parent;
    while (parent && parent->kind == NodeKind::MemberExpression) {
      parent = parent->parent;
    }
    return parent && (parent->kind == NodeKind::TSClassImplements ||
                      parent->kind == NodeKind::TSInterfaceHeritage);
  }

  // The innermost `any` object is reported; every access after it is unsafe too and
  // stays silent.
  State checkObject(Node *node)
  {
    ast::MemberExpression view(node);
    if (m_allowOptionalChaining && view.isOptional()) {
      m_states.add_overwrite(node, State::Chained);
      return State::Chained;
    }
    if (State *cached = m_states.lookup_ptr(node)) {
      return *cached;
    }
    Node *object = view.object();
    if (object->kind == NodeKind::MemberExpression) {
      State inner = checkObject(object);
      if (inner == State::Unsafe) {
        m_states.add_overwrite(node, inner);
        return inner;
      }
    }
    TypeId type = facts().typeOf(object);
    State state = facts().isAny(type) ? State::Unsafe : State::Safe;
    m_states.add_overwrite(node, state);
    if (state != State::Unsafe) {
      return state;
    }
    const char *messageId = nullptr;
    if (!m_noImplicitThis) {
      if (Node *self = unsafe::thisExpressionOf(node)) {
        TypeId selfType = unsafe::constrained(facts(), facts().typeOf(self));
        if (facts().isAny(selfType)) {
          messageId = facts().isErrorType(selfType) ? "errorThisMemberExpression"
                                                    : "unsafeThisMemberExpression";
        }
      }
    }
    if (!messageId) {
      messageId =
          facts().isErrorType(type) ? "errorMemberExpression" : "unsafeMemberExpression";
    }
    Node *property = view.property();
    m_ctx.report(
        property, messageId, {{"property", propertyText(property, view.isComputed())}});
    return state;
  }

  void checkComputedKey(Node *key, bool optional)
  {
    if (m_allowOptionalChaining && optional) {
      return;
    }
    // A literal is never `any`, and an update expression is always `number`.
    if (key->kind == NodeKind::Literal || key->kind == NodeKind::UpdateExpression) {
      return;
    }
    TypeId type = facts().typeOf(key);
    if (!facts().isAny(type)) {
      return;
    }
    // The key's own span carries any wrapping parentheses; upstream reports the
    // expression inside them.
    uint32_t start = key->start;
    uint32_t end = key->end;
    string_view source = m_ctx.source();
    while (start < end && source[start] == '(' && source[end - 1] == ')') {
      start = firstNonSpaceAt(source, start + 1);
      end = lastNonSpaceBefore(source, end - 1) + 1;
    }
    string text;
    text += '[';
    for (uint32_t i = start; i < end; i++) {
      text += source[i];
    }
    text += ']';
    Report r;
    r.node = key;
    r.at(start, end);
    r.messageId = facts().isErrorType(type) ? "errorComputedMemberAccess"
                                            : "unsafeComputedMemberAccess";
    r.data.append({"property", m_texts.intern(text)});
    m_ctx.report(std::move(r));
  }

  string_view propertyText(Node *property, bool computed)
  {
    string text;
    text += computed ? '[' : '.';
    for (char c : m_ctx.textOf(property)) {
      text += c;
    }
    if (computed) {
      text += ']';
    }
    return m_texts.intern(text);
  }

  RuleContext &m_ctx;
  bool m_allowOptionalChaining = false;
  bool m_noImplicitThis = true;
  Map<Node *, State> m_states;
  unsafe::Texts m_texts;
};

void create(RuleContext &ctx)
{
  Checker *checker = ctx.state<Checker>(&ctx);
  ctx.on(NodeKind::MemberExpression,
         [checker](Node *node) { checker->checkMember(node); });
}

} // namespace

const RuleDef kNoUnsafeMemberAccess{
    {
        "no-unsafe-member-access",
        "Disallow member access on a value with type `any`",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/"
        "no-unsafe-member-access.md",
        /*recommended=*/true,
        /*fixable=*/false,
        /*hasSuggestions=*/false,
        /*typeAware=*/true,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
