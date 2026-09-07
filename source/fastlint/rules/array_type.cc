// array-type: `T[]` or `Array<T>` consistently (docs/rules/array-type.md).

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;

constexpr Message kMessages[] = {
    {"errorStringArray",
     "Array type using '{{className}}<{{type}}>' is forbidden. Use "
     "'{{readonlyPrefix}}{{type}}[]' "
     "instead."},
    {"errorStringArrayReadonly",
     "Array type using '{{className}}<{{type}}>' is forbidden. Use "
     "'{{readonlyPrefix}}{{type}}' "
     "instead."},
    {"errorStringArraySimple",
     "Array type using '{{className}}<{{type}}>' is forbidden for simple types. Use "
     "'{{readonlyPrefix}}{{type}}[]' instead."},
    {"errorStringArraySimpleReadonly",
     "Array type using '{{className}}<{{type}}>' is forbidden for simple types. Use "
     "'{{readonlyPrefix}}{{type}}' instead."},
    {"errorStringGeneric",
     "Array type using '{{readonlyPrefix}}{{type}}[]' is forbidden. Use "
     "'{{className}}<{{type}}>' "
     "instead."},
    {"errorStringGenericSimple",
     "Array type using '{{readonlyPrefix}}{{type}}[]' is forbidden for non-simple types. "
     "Use "
     "'{{className}}<{{type}}>' instead."},
};

enum class Style : uint8_t { Array, ArraySimple, Generic };

struct Options {
  Style def = Style::Array;
  Style readonly = Style::Array;
};

Style parseStyle(string_view text, Style fallback)
{
  if (text == "array") {
    return Style::Array;
  }
  if (text == "array-simple") {
    return Style::ArraySimple;
  }
  if (text == "generic") {
    return Style::Generic;
  }
  return fallback;
}

bool isSimpleType(Node *node)
{
  switch (node->kind) {
  case NodeKind::Identifier:
  case NodeKind::TSKeywordType:
  case NodeKind::TSArrayType:
  case NodeKind::TSThisType:
  case NodeKind::TSQualifiedName:
    return true;
  case NodeKind::TSTypeReference: {
    ast::TSTypeReference ref(node);
    Node *arguments = ref.typeArguments();
    if (ref.typeName()->isIdentifier("Array")) {
      if (!arguments) {
        return true;
      }
      span<Node *> params = ast::TSTypeParameterInstantiation(arguments).params();
      return params.size() == 1 && isSimpleType(params[0]);
    }
    return !arguments && isSimpleType(ref.typeName());
  }
  default:
    return false;
  }
}

Node *genericOf(ast::Fixer &fixer, const char *className, Node *element)
{
  Node *arguments = fixer.build(NodeKind::TSTypeParameterInstantiation, {});
  fixer.append(arguments, element);
  Node *ref = fixer.typeReference(fixer.identifier(className));
  fixer.set(ref, 1, arguments);
  return ref;
}

Node *arrayOf(ast::Fixer &fixer, Node *element, bool readonly)
{
  Node *array = fixer.build(NodeKind::TSArrayType, {element});
  if (!readonly) {
    return array;
  }
  Node *op = fixer.build(NodeKind::TSTypeOperator, {array});
  op->setDataByte(0, uint8_t(ast::TypeOperator::Readonly));
  return op;
}

void create(RuleContext &ctx)
{
  Options *options = ctx.state<Options>();
  if (const JsonValue *given = ctx.option(0)) {
    options->def = parseStyle(given->getString("default"), Style::Array);
    options->readonly = parseStyle(given->getString("readonly"), options->def);
  }

  auto typeText = [&ctx](Node *type) -> string_view {
    return isSimpleType(type) ? ctx.textOf(type) : string_view("T");
  };

  ctx.on(NodeKind::TSArrayType, [&ctx, options, typeText](Node *node) {
    Node *parent = node->parent;
    bool readonly = parent && parent->kind == NodeKind::TSTypeOperator &&
                    ast::TSTypeOperator(parent).op() == ast::TypeOperator::Readonly;
    Style style = readonly ? options->readonly : options->def;
    Node *element = ast::TSArrayType(node).elementType();
    if (style == Style::Array || (style == Style::ArraySimple && isSimpleType(element))) {
      return;
    }
    Node *target = readonly ? parent : node;
    const char *className = readonly ? "ReadonlyArray" : "Array";
    Report r;
    r.node = target;
    r.messageId =
        style == Style::Generic ? "errorStringGeneric" : "errorStringGenericSimple";
    r.data.append({"type", typeText(element)});
    r.data.append({"className", className});
    r.data.append({"readonlyPrefix", readonly ? "readonly " : ""});
    r.fix = [target, element, className](ast::Fixer &fixer) {
      fixer.detach(element);
      fixer.replace(target, genericOf(fixer, className, element));
    };
    ctx.report(std::move(r));
  });

  ctx.on(NodeKind::TSTypeReference, [&ctx, options, typeText](Node *node) {
    ast::TSTypeReference ref(node);
    Node *name = ref.typeName();
    if (name->kind != NodeKind::Identifier) {
      return;
    }
    bool isArray = name->text == "Array";
    bool isReadonlyArray = name->text == "ReadonlyArray";
    bool isReadonly = name->text == "Readonly";
    if (!isArray && !isReadonlyArray && !isReadonly) {
      return;
    }
    // A local type of the same name is not the built-in.
    if (const ast::Reference *reference = ctx.bindings().referenceOf(name)) {
      if (reference->resolved) {
        return;
      }
    }
    // A bare `Array` has no element type to move; upstream leaves it too.
    Node *arguments = ref.typeArguments();
    if (!arguments) {
      return;
    }
    span<Node *> params = ast::TSTypeParameterInstantiation(arguments).params();
    if (isReadonly && (params.size() != 1 || params[0]->kind != NodeKind::TSArrayType)) {
      return;
    }
    bool readonlyResult = isReadonlyArray || isReadonly;
    Style style = readonlyResult ? options->readonly : options->def;
    if (style == Style::Generic || params.size() != 1) {
      return;
    }
    Node *type = params[0];
    if (style == Style::ArraySimple && !isSimpleType(type)) {
      return;
    }
    const char *messageId;
    if (style == Style::Array) {
      messageId = isReadonly ? "errorStringArrayReadonly" : "errorStringArray";
    } else {
      messageId =
          isReadonly ? "errorStringArraySimpleReadonly" : "errorStringArraySimple";
    }
    Report r;
    r.node = node;
    r.messageId = messageId;
    r.data.append({"type", typeText(type)});
    r.data.append({"className", readonlyResult ? name->text : string_view("Array")});
    r.data.append({"readonlyPrefix", readonlyResult ? "readonly " : ""});
    r.fix = [node, type, readonlyResult, isReadonly](ast::Fixer &fixer) {
      fixer.detach(type);
      Node *fresh;
      if (isReadonly) {
        // `Readonly<T[]>` keeps the array and gains the operator.
        fresh = fixer.build(NodeKind::TSTypeOperator, {type});
        fresh->setDataByte(0, uint8_t(ast::TypeOperator::Readonly));
      } else {
        fresh = arrayOf(fixer, type, readonlyResult);
      }
      fixer.replace(node, fresh);
    };
    ctx.report(std::move(r));
  });
}

} // namespace

const RuleDef kArrayType{
    {
        "array-type",
        "Require consistently using either `T[]` or `Array<T>` for arrays",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/array-type.md",
        /*recommended=*/false,
        /*fixable=*/true,
        /*hasSuggestions=*/false,
        /*typeAware=*/false,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
