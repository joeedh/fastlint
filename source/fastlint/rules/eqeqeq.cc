// eqeqeq: require `===` and `!==` (docs/rules/eqeqeq.md).

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::BinaryOperator;

constexpr Message kMessages[] = {
    {"unexpected",
     "Expected '{{expectedOperator}}' and instead saw '{{actualOperator}}'."},
    {"replaceOperator", "Use '{{expectedOperator}}' instead of '{{actualOperator}}'."},
};

enum class NullMode : uint8_t { Always, Never, Ignore };

struct Options {
  bool smart = false;
  NullMode null = NullMode::Always;
};

const char *operatorText(BinaryOperator op)
{
  switch (op) {
  case BinaryOperator::Equal:
    return "==";
  case BinaryOperator::NotEqual:
    return "!=";
  case BinaryOperator::StrictEqual:
    return "===";
  case BinaryOperator::StrictNotEqual:
    return "!==";
  default:
    return "";
  }
}

bool isTypeOf(const ast::Node *n)
{
  return n->kind == ast::NodeKind::UnaryExpression &&
         ast::UnaryExpression(const_cast<ast::Node *>(n)).op() ==
             ast::UnaryOperator::Typeof;
}

/** The literal's type name, `string` for a template with no substitutions, or empty. */
string_view literalType(ast::Node *n)
{
  if (n->kind == ast::NodeKind::Literal) {
    switch (ast::Literal(n).literalKind()) {
    case ast::LiteralKind::String:
      return "string";
    case ast::LiteralKind::Number:
      return "number";
    case ast::LiteralKind::Bigint:
      return "bigint";
    case ast::LiteralKind::Boolean:
      return "boolean";
    case ast::LiteralKind::Null:
      return "object";
    case ast::LiteralKind::Regex:
      return "object";
    }
  }
  if (n->kind == ast::NodeKind::TemplateLiteral) {
    for (ast::Node *part : ast::TemplateLiteral(n).parts()) {
      if (part->kind != ast::NodeKind::TemplateElement) {
        return {};
      }
    }
    return "string";
  }
  return {};
}

bool isNullLiteral(const ast::Node *n)
{
  return n->kind == ast::NodeKind::Literal &&
         ast::Literal(const_cast<ast::Node *>(n)).literalKind() == ast::LiteralKind::Null;
}

void create(RuleContext &ctx)
{
  Options *options = ctx.state<Options>();
  const JsonValue *mode = ctx.option(0);
  string_view modeText = mode ? mode->asString() : string_view("always");
  if (modeText == "smart") {
    options->smart = true;
    options->null = NullMode::Ignore;
  } else if (modeText == "allow-null") {
    options->null = NullMode::Ignore;
  } else if (const JsonValue *extra = ctx.option(1)) {
    string_view null = extra->getString("null");
    if (null == "never") {
      options->null = NullMode::Never;
    } else if (null == "ignore") {
      options->null = NullMode::Ignore;
    }
  }

  ctx.on(ast::NodeKind::BinaryExpression, [&ctx, options](ast::Node *node) {
    ast::BinaryExpression binary(node);
    BinaryOperator op = binary.op();
    bool loose = op == BinaryOperator::Equal || op == BinaryOperator::NotEqual;
    bool strict =
        op == BinaryOperator::StrictEqual || op == BinaryOperator::StrictNotEqual;
    if (!loose && !strict) {
      return;
    }
    ast::Node *left = binary.left();
    ast::Node *right = binary.right();
    bool nullCheck = isNullLiteral(left) || isNullLiteral(right);
    bool typeOfBinary = isTypeOf(left) || isTypeOf(right);
    string_view leftType = literalType(left);
    bool sameTypeLiterals = !leftType.empty() && leftType == literalType(right);

    BinaryOperator expected;
    if (strict) {
      if (options->null != NullMode::Never || !nullCheck) {
        return;
      }
      expected = op == BinaryOperator::StrictEqual ? BinaryOperator::Equal
                                                   : BinaryOperator::NotEqual;
    } else {
      if (options->smart && (typeOfBinary || sameTypeLiterals || nullCheck)) {
        return;
      }
      if (options->null != NullMode::Always && nullCheck) {
        return;
      }
      expected = op == BinaryOperator::Equal ? BinaryOperator::StrictEqual
                                             : BinaryOperator::StrictNotEqual;
    }

    string_view source = ctx.source();
    string_view actualText = operatorText(op);
    uint32_t at = findToken(source, left->end, right->start, actualText);
    Report r;
    r.node = node;
    r.at(at, at + uint32_t(actualText.size()));
    r.messageId = "unexpected";
    r.data.append({"expectedOperator", operatorText(expected)});
    r.data.append({"actualOperator", actualText});
    auto swap = [node, expected](ast::Fixer &fixer) {
      fixer.setData(node, 0, uint8_t(expected));
    };
    // The fix is safe only when both sides share a runtime type already.
    if (typeOfBinary || sameTypeLiterals) {
      r.fix = swap;
    } else {
      Suggestion s;
      s.messageId = "replaceOperator";
      s.data.append({"expectedOperator", operatorText(expected)});
      s.data.append({"actualOperator", actualText});
      s.fix = swap;
      r.suggestions.append(std::move(s));
    }
    ctx.report(std::move(r));
  });
}

const char kSchema[] =
    R"([{"enum":["always","smart","allow-null"]},{"type":"object","properties":{"null":{"enum":["always","never","ignore"]}},"additionalProperties":false}])";

} // namespace

const RuleDef kEqeqeq{
    {
        "eqeqeq",
        "Require the use of `===` and `!==`",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/eqeqeq.md",
        /*recommended=*/false,
        /*fixable=*/true,
        /*hasSuggestions=*/true,
        /*typeAware=*/false,
        messagesOf(kMessages),
        kSchema,
    },
    create,
};

} // namespace fastlint::rules
