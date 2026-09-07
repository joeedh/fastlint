// no-unsafe-assignment: disallow assigning a value with type `any` to variables and
// properties (docs/rules/no-unsafe-assignment.md). typescript-eslint's rule over
// `TypeFacts`.

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
    {"anyAssignment", "Unsafe assignment of an {{sender}} value."},
    {"anyAssignmentThis",
     "Unsafe assignment of an {{sender}} value. `this` is typed as `any`."
     "\nYou can try to fix this by turning on the `noImplicitThis` compiler option, or "
     "adding a `this` parameter to the function."},
    {"unsafeArrayPattern", "Unsafe array destructuring of an {{sender}} array value."},
    {"unsafeArrayPatternFromTuple",
     "Unsafe array destructuring of a tuple element with an {{sender}} value."},
    {"unsafeArraySpread", "Unsafe spread of an {{sender}} value in an array."},
    {"unsafeAssignment",
     "Unsafe assignment of type {{sender}} to a variable of type {{receiver}}."},
    {"unsafeObjectPattern",
     "Unsafe object destructuring of a property with an {{sender}} value."},
};

/** How the receiver's type is found for the `any` inside generics comparison. */
enum class Comparison {
  /** No comparison: an unannotated variable takes the sender's type. */
  None,
  /** The receiver's own type. */
  Basic,
  /** The sender's contextual type. */
  Contextual,
};

class Checker {
public:
  explicit Checker(RuleContext *ctx) : m_ctx(*ctx)
  {
    m_noImplicitThis = facts().strictOption("noImplicitThis");
  }

  void checkClassField(Node *node)
  {
    ast::ClassMember view(node);
    Node *value = view.value();
    if (!value) {
      return;
    }
    Node *annotation = node->kind == NodeKind::PropertyDefinition
                           ? ast::PropertyDefinition(node).typeAnnotation()
                           : ast::AccessorProperty(node).typeAnnotation();
    checkAssignment(
        view.key(), value, node, annotation ? Comparison::Basic : Comparison::None);
  }

  void checkPair(Node *node, Node *left, Node *right)
  {
    if (!checkAssignment(left, right, node, Comparison::Basic) &&
        !checkArrayDestructure(left, right))
    {
      checkObjectDestructure(left, right);
    }
  }

  void checkDeclarator(Node *node)
  {
    ast::VariableDeclarator view(node);
    Node *id = view.id();
    Node *init = view.init();
    if (!init) {
      return;
    }
    Node *annotation = nullptr;
    switch (id->kind) {
    case NodeKind::Identifier:
      annotation = ast::Identifier(id).typeAnnotation();
      break;
    case NodeKind::ObjectPattern:
      annotation = ast::ObjectPattern(id).typeAnnotation();
      break;
    case NodeKind::ArrayPattern:
      annotation = ast::ArrayPattern(id).typeAnnotation();
      break;
    default:
      break;
    }
    if (!checkAssignment(
            id, init, node, annotation ? Comparison::Basic : Comparison::None) &&
        !checkArrayDestructure(id, init))
    {
      checkObjectDestructure(id, init);
    }
  }

  void checkProperty(Node *node)
  {
    if (node->parent && node->parent->kind == NodeKind::ObjectPattern) {
      return;
    }
    ast::Property view(node);
    Node *value = view.value();
    if (!value || value->kind == NodeKind::AssignmentPattern ||
        value->kind == NodeKind::TSEmptyBodyFunctionExpression)
    {
      return;
    }
    checkAssignment(view.key(), value, node, Comparison::Contextual);
  }

  void checkArraySpread(Node *node)
  {
    if (!node->parent || node->parent->kind != NodeKind::ArrayExpression) {
      return;
    }
    TypeId type = facts().typeOf(ast::SpreadElement(node).argument());
    if (facts().isAny(type) || unsafe::isAnyArray(facts(), type)) {
      m_ctx.report(node, "unsafeArraySpread", {{"sender", anyText(type)}});
    }
  }

  void checkJsxAttribute(Node *node)
  {
    Node *value = ast::JSXAttribute(node).value();
    if (!value || value->kind != NodeKind::JSXExpressionContainer) {
      return;
    }
    Node *expression = ast::JSXExpressionContainer(value).expression();
    if (!expression || expression->kind == NodeKind::JSXEmptyExpression) {
      return;
    }
    checkAssignment(
        ast::JSXAttribute(node).name(), expression, expression, Comparison::Contextual);
  }

private:
  TypeFacts &facts()
  {
    return *m_ctx.types();
  }

  /** Reports on `reporting` when `sender` cannot safely land in `receiver`. */
  bool
  checkAssignment(Node *receiver, Node *sender, Node *reporting, Comparison comparison)
  {
    TypeId senderType = facts().typeOf(sender);
    if (!senderType) {
      return false;
    }
    TypeId receiverType = 0;
    if (comparison == Comparison::Contextual) {
      receiverType = facts().contextualTypeOf(sender);
    }
    if (!receiverType) {
      receiverType = facts().typeOf(receiver);
    }
    // The server gives a binding identifier no type of its own; its annotated type is
    // the initializer's contextual type.
    if (!receiverType) {
      receiverType = facts().contextualTypeOf(sender);
    }
    if (facts().isAny(senderType)) {
      // Assigning `any` to `unknown` is safe.
      if (facts().isUnknown(receiverType)) {
        return false;
      }
      const char *messageId = "anyAssignment";
      if (!m_noImplicitThis) {
        Node *self = unsafe::thisExpressionOf(sender);
        if (self && facts().isAny(unsafe::constrained(facts(), facts().typeOf(self)))) {
          messageId = "anyAssignmentThis";
        }
      }
      m_ctx.report(reporting, messageId, {{"sender", anyText(senderType)}});
      return true;
    }
    if (comparison == Comparison::None) {
      return false;
    }
    unsafe::UnsafePair pair;
    if (!unsafe::isUnsafeAssignment(facts(), senderType, receiverType, sender, pair)) {
      return false;
    }
    m_ctx.report(reporting,
                 "unsafeAssignment",
                 {{"sender", m_texts.quoted(facts(), pair.sender)},
                  {"receiver", m_texts.quoted(facts(), pair.receiver)}});
    return true;
  }

  bool checkArrayDestructure(Node *receiver, Node *sender)
  {
    if (receiver->kind != NodeKind::ArrayPattern) {
      return false;
    }
    return checkArrayPattern(receiver, facts().typeOf(sender));
  }

  bool checkArrayPattern(Node *pattern, TypeId senderType)
  {
    if (unsafe::isAnyArray(facts(), senderType)) {
      m_ctx.report(pattern, "unsafeArrayPattern", {{"sender", anyText(senderType)}});
      return false;
    }
    if (!facts().isTuple(senderType)) {
      return true;
    }
    litestl::util::span<const TypeId> elements = facts().typeArguments(senderType);
    litestl::util::span<Node *> receivers = ast::ArrayPattern(pattern).elements();
    bool reported = false;
    for (size_t i = 0; i < receivers.size() && i < elements.size(); i++) {
      Node *element = receivers[i];
      // A rest element is not a one-to-one assignment.
      if (!element || element->kind == NodeKind::RestElement) {
        continue;
      }
      TypeId elementType = elements[i];
      if (facts().isAny(elementType)) {
        m_ctx.report(
            element, "unsafeArrayPatternFromTuple", {{"sender", anyText(elementType)}});
        reported = true;
      } else if (element->kind == NodeKind::ArrayPattern) {
        reported = checkArrayPattern(element, elementType);
      } else if (element->kind == NodeKind::ObjectPattern) {
        reported = checkObjectPattern(element, elementType);
      }
    }
    return reported;
  }

  bool checkObjectDestructure(Node *receiver, Node *sender)
  {
    if (receiver->kind != NodeKind::ObjectPattern) {
      return false;
    }
    return checkObjectPattern(receiver, facts().typeOf(sender));
  }

  bool checkObjectPattern(Node *pattern, TypeId senderType)
  {
    bool reported = false;
    for (Node *property : ast::ObjectPattern(pattern).properties()) {
      if (!property || property->kind != NodeKind::Property) {
        continue;
      }
      ast::Property view(property);
      string_view key = staticPropertyName(view.key(), view.isComputed());
      if (key.empty()) {
        continue;
      }
      TypeId propertyType = facts().propertyType(senderType, key);
      if (!propertyType) {
        continue;
      }
      Node *value = view.value();
      if (facts().isAny(propertyType)) {
        m_ctx.report(value, "unsafeObjectPattern", {{"sender", anyText(propertyType)}});
        reported = true;
      } else if (value->kind == NodeKind::ArrayPattern) {
        reported = checkArrayPattern(value, propertyType);
      } else if (value->kind == NodeKind::ObjectPattern) {
        reported = checkObjectPattern(value, propertyType);
      }
    }
    return reported;
  }

  string_view anyText(TypeId type)
  {
    return facts().isErrorType(type) ? "error typed" : "`any`";
  }

  RuleContext &m_ctx;
  bool m_noImplicitThis = true;
  unsafe::Texts m_texts;
};

void create(RuleContext &ctx)
{
  Checker *checker = ctx.state<Checker>(&ctx);
  ctx.on(NodeKind::PropertyDefinition,
         [checker](Node *node) { checker->checkClassField(node); });
  ctx.on(NodeKind::AccessorProperty,
         [checker](Node *node) { checker->checkClassField(node); });
  ctx.on(NodeKind::AssignmentExpression, [checker](Node *node) {
    ast::AssignmentExpression view(node);
    if (view.op() == ast::AssignmentOperator::Assign) {
      checker->checkPair(node, view.left(), view.right());
    }
  });
  ctx.on(NodeKind::AssignmentPattern, [checker](Node *node) {
    ast::AssignmentPattern view(node);
    checker->checkPair(node, view.left(), view.right());
  });
  ctx.on(NodeKind::VariableDeclarator,
         [checker](Node *node) { checker->checkDeclarator(node); });
  ctx.on(NodeKind::Property, [checker](Node *node) { checker->checkProperty(node); });
  ctx.on(NodeKind::SpreadElement,
         [checker](Node *node) { checker->checkArraySpread(node); });
  ctx.on(NodeKind::JSXAttribute,
         [checker](Node *node) { checker->checkJsxAttribute(node); });
}

} // namespace

const RuleDef kNoUnsafeAssignment{
    {
        "no-unsafe-assignment",
        "Disallow assigning a value with type `any` to variables and properties",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/"
        "no-unsafe-assignment.md",
        /*recommended=*/true,
        /*fixable=*/false,
        /*hasSuggestions=*/false,
        /*typeAware=*/true,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
