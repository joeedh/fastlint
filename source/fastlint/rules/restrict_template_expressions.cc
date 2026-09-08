// restrict-template-expressions: require a template literal's interpolations to be
// string-like (docs/rules/restrict-template-expressions.md). typescript-eslint's rule
// over `TypeFacts`.

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
using litestl::util::Vector;
using types::TypeFacts;
using types::TypeId;

constexpr Message kMessages[] = {
    {"invalidType", "Invalid type \"{{type}}\" of template literal expression."},
};

/** How deep a type is walked before it is accepted, so a self-referential type cannot
 * loop. */
constexpr int kMaxDepth = 8;

class Checker {
public:
  explicit Checker(RuleContext *ctx) : m_ctx(*ctx)
  {
    const JsonValue *opt = ctx->option(0);
    m_allowAny = opt ? opt->getBool("allowAny", true) : true;
    m_allowBoolean = opt ? opt->getBool("allowBoolean", true) : true;
    m_allowNullish = opt ? opt->getBool("allowNullish", true) : true;
    m_allowNumber = opt ? opt->getBool("allowNumber", true) : true;
    m_allowRegExp = opt ? opt->getBool("allowRegExp", true) : true;
    m_allowArray = opt ? opt->getBool("allowArray", false) : false;
    m_allowNever = opt ? opt->getBool("allowNever", false) : false;
    readAllow(opt ? opt->get("allow") : nullptr);
  }

  void checkTemplate(Node *node)
  {
    if (node->parent && node->parent->kind == NodeKind::TaggedTemplateExpression) {
      return;
    }
    for (Node *part : ast::TemplateLiteral(node).parts()) {
      if (!part || part->kind == NodeKind::TemplateElement) {
        continue;
      }
      TypeId type = constrained(facts().typeOf(part));
      if (!type) {
        continue;
      }
      if (!accepts(type, 0)) {
        m_ctx.report(
            part, "invalidType", {{"type", m_texts.intern(facts().typeToString(type))}});
      }
    }
  }

private:
  TypeFacts &facts()
  {
    return *m_ctx.types();
  }

  /** A type parameter stands for its base constraint here. */
  TypeId constrained(TypeId type)
  {
    if (type && facts().isTypeParameter(type)) {
      TypeId constraint = facts().constraintOf(type);
      return constraint ? constraint : type;
    }
    return type;
  }

  bool accepts(TypeId type, int depth)
  {
    // An unresolved or over-deep type is left alone rather than reported, so the rule
    // never invents a type name it could not print.
    if (!type || depth > kMaxDepth) {
      return true;
    }
    uint32_t f = facts().flags(type);
    if (f & tsgo::TypeFlags::Union) {
      for (TypeId member : facts().unionMembers(type)) {
        if (!accepts(member, depth + 1)) {
          return false;
        }
      }
      return true;
    }
    if (f & tsgo::TypeFlags::Intersection) {
      for (TypeId member : facts().unionMembers(type)) {
        if (accepts(member, depth + 1)) {
          return true;
        }
      }
      return false;
    }
    if (f & tsgo::TypeFlags::StringLike) {
      return true;
    }
    if (allowMatches(type)) {
      return true;
    }
    return testerAccepts(type, f, depth);
  }

  bool testerAccepts(TypeId type, uint32_t f, int depth)
  {
    if (m_allowAny && facts().isAny(type)) {
      return true;
    }
    if (m_allowBoolean && (f & tsgo::TypeFlags::BooleanLike)) {
      return true;
    }
    if (m_allowNullish && (f & (tsgo::TypeFlags::Null | tsgo::TypeFlags::Undefined))) {
      return true;
    }
    if (m_allowNumber &&
        (f & (tsgo::TypeFlags::NumberLike | tsgo::TypeFlags::BigIntLike)))
    {
      return true;
    }
    if (m_allowNever && (f & tsgo::TypeFlags::Never)) {
      return true;
    }
    if (m_allowRegExp && facts().isBuiltin(type, "RegExp")) {
      return true;
    }
    if (m_allowArray && (facts().isArray(type) || facts().isTuple(type))) {
      return accepts(facts().numberIndexType(type), depth + 1);
    }
    return false;
  }

  bool allowMatches(TypeId type)
  {
    for (const string &name : m_allow) {
      if (facts().isBuiltin(type, string_view(name.c_str(), name.size()))) {
        return true;
      }
    }
    return false;
  }

  void readAllow(const JsonValue *allow)
  {
    if (!allow || !allow->isArray()) {
      // The default allows the library error and URL types.
      m_allow.append(string("Error"));
      m_allow.append(string("URL"));
      m_allow.append(string("URLSearchParams"));
      return;
    }
    for (int i = 0; i < allow->size(); i++) {
      const JsonValue *specifier = allow->at(i);
      if (!specifier || specifier->getString("from") != "lib") {
        // A file or package specifier is not supported yet; see the docs.
        continue;
      }
      const JsonValue *name = specifier->get("name");
      if (!name) {
        continue;
      }
      if (name->isString()) {
        appendName(name->asString());
      } else if (name->isArray()) {
        for (int j = 0; j < name->size(); j++) {
          appendName(name->at(j)->asString());
        }
      }
    }
  }

  void appendName(string_view name)
  {
    string copy;
    for (char c : name) {
      copy += c;
    }
    m_allow.append(std::move(copy));
  }

  RuleContext &m_ctx;
  bool m_allowAny = true;
  bool m_allowBoolean = true;
  bool m_allowNullish = true;
  bool m_allowNumber = true;
  bool m_allowRegExp = true;
  bool m_allowArray = false;
  bool m_allowNever = false;
  Vector<string> m_allow;
  unsafe::Texts m_texts;
};

void create(RuleContext &ctx)
{
  Checker *checker = ctx.state<Checker>(&ctx);
  ctx.on(NodeKind::TemplateLiteral,
         [checker](Node *node) { checker->checkTemplate(node); });
}

const char kSchema[] =
    R"([{"type":"object","properties":{"allowAny":{"type":"boolean"},"allowBoolean":{"type":"boolean"},"allowNullish":{"type":"boolean"},"allowNumber":{"type":"boolean"},"allowRegExp":{"type":"boolean"},"allowArray":{"type":"boolean"},"allowNever":{"type":"boolean"},"allow":{"type":"array","items":{"oneOf":[{"type":"string"},{"type":"object","properties":{"from":{"enum":["file","lib","package"]},"name":{"oneOf":[{"type":"string"},{"type":"array","items":{"type":"string"}}]},"package":{"type":"string"}},"additionalProperties":false}]}},"additionalProperties":false}])";

} // namespace

const RuleDef kRestrictTemplateExpressions{
    {
        "restrict-template-expressions",
        "Enforce template literal expressions to be of `string` type",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/"
        "restrict-template-expressions.md",
        /*recommended=*/true,
        /*fixable=*/false,
        /*hasSuggestions=*/false,
        /*typeAware=*/true,
        messagesOf(kMessages),
        kSchema,
    },
    create,
};

} // namespace fastlint::rules
