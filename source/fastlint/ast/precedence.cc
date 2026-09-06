#include "fastlint/ast/precedence.h"

#include "fastlint/ast/generated/views.h"
#include "fastlint/ast/kind_info.h"

namespace fastlint::ast {

namespace {

constexpr int kSequence = 1;
constexpr int kAssignment = 2;
constexpr int kConditional = 3;
constexpr int kUnary = 15;
constexpr int kPostfix = 16;
constexpr int kNewWithoutArgs = 17;
constexpr int kCall = 18;
constexpr int kPrimary = 19;

constexpr int kTypeConditional = 1;
constexpr int kTypeFunction = 2;
constexpr int kTypeUnion = 3;
constexpr int kTypeIntersection = 4;
constexpr int kTypeOperator = 5;
constexpr int kTypePostfix = 6;
constexpr int kTypePrimary = 7;

int binaryPrecedence(BinaryOperator op)
{
  switch (op) {
  case BinaryOperator::BitwiseOr:
    return 6;
  case BinaryOperator::BitwiseXor:
    return 7;
  case BinaryOperator::BitwiseAnd:
    return 8;
  case BinaryOperator::Equal:
  case BinaryOperator::NotEqual:
  case BinaryOperator::StrictEqual:
  case BinaryOperator::StrictNotEqual:
    return 9;
  case BinaryOperator::Less:
  case BinaryOperator::LessEqual:
  case BinaryOperator::Greater:
  case BinaryOperator::GreaterEqual:
  case BinaryOperator::In:
  case BinaryOperator::Instanceof:
    return 10;
  case BinaryOperator::ShiftLeft:
  case BinaryOperator::ShiftRight:
  case BinaryOperator::ShiftRightUnsigned:
    return 11;
  case BinaryOperator::Add:
  case BinaryOperator::Subtract:
    return 12;
  case BinaryOperator::Multiply:
  case BinaryOperator::Divide:
  case BinaryOperator::Remainder:
    return 13;
  case BinaryOperator::Exponent:
    return 14;
  }
  return kPrimary;
}

int logicalPrecedence(LogicalOperator op)
{
  return op == LogicalOperator::And ? 5 : 4;
}

bool isBinaryLike(const Node *n)
{
  return n->kind == NodeKind::BinaryExpression || n->kind == NodeKind::LogicalExpression;
}

/** The operator's own level for a binary or logical node. */
int operatorPrecedence(const Node *n)
{
  if (n->kind == NodeKind::BinaryExpression) {
    return binaryPrecedence(BinaryOperator(n->dataByte(0)));
  }
  return logicalPrecedence(LogicalOperator(n->dataByte(0)));
}

bool isNullish(const Node *n)
{
  return n->kind == NodeKind::LogicalExpression &&
         LogicalOperator(n->dataByte(0)) == LogicalOperator::Nullish;
}

bool isOrAnd(const Node *n)
{
  return n->kind == NodeKind::LogicalExpression &&
         LogicalOperator(n->dataByte(0)) != LogicalOperator::Nullish;
}

/** Whether a number literal's spelling ends in a bare integer, so `.` after it is a dot.
 */
bool isBareInteger(const Node *n)
{
  if (n->kind != NodeKind::Literal || LiteralKind(n->dataByte(0)) != LiteralKind::Number)
  {
    return false;
  }
  for (char c : n->text) {
    if (c == '.' || c == 'e' || c == 'E' || c == 'x' || c == 'X' || c == 'o' ||
        c == 'O' || c == 'b' || c == 'B' || c == 'n')
    {
      return false;
    }
  }
  return true;
}

/** Whether `n` is a `new` with no argument list, which swallows a following call. */
bool isNewWithoutArgs(const Node *n)
{
  return n->kind == NodeKind::NewExpression &&
         n->children.size() <= size_t(kindInfo(NodeKind::NewExpression).fixedChildren);
}

bool isSignUnary(const Node *n)
{
  if (n->kind == NodeKind::UnaryExpression) {
    UnaryOperator op = UnaryOperator(n->dataByte(0));
    return op == UnaryOperator::Minus || op == UnaryOperator::Plus;
  }
  return n->kind == NodeKind::UpdateExpression && n->hasFlag(Flag::Prefix);
}

/** The sign a unary minus, plus or prefix update starts with. */
char leadingSign(const Node *n)
{
  if (n->kind == NodeKind::UnaryExpression) {
    return UnaryOperator(n->dataByte(0)) == UnaryOperator::Minus ? '-' : '+';
  }
  return UpdateOperator(n->dataByte(0)) == UpdateOperator::Decrement ? '-' : '+';
}

/** Whether `n`'s printed form starts with the token that opens `kind`. */
bool startsWith(const Node *n, NodeKind kind)
{
  const Node *at = n;
  while (at) {
    if (at->kind == kind) {
      return true;
    }
    if (at->hasFlag(Flag::Parenthesized)) {
      return false;
    }
    switch (at->kind) {
    case NodeKind::MemberExpression:
    case NodeKind::CallExpression:
    case NodeKind::TaggedTemplateExpression:
    case NodeKind::BinaryExpression:
    case NodeKind::LogicalExpression:
    case NodeKind::AssignmentExpression:
    case NodeKind::ConditionalExpression:
    case NodeKind::SequenceExpression:
    case NodeKind::TSAsExpression:
    case NodeKind::TSSatisfiesExpression:
    case NodeKind::TSNonNullExpression:
    case NodeKind::TSInstantiationExpression:
      at = at->children.size() > 0 ? at->children[0] : nullptr;
      break;
    case NodeKind::UpdateExpression:
      at = at->hasFlag(Flag::Prefix) ? nullptr : at->children[0];
      break;
    default:
      at = nullptr;
      break;
    }
  }
  return false;
}

} // namespace

int expressionPrecedence(const Node *n)
{
  if (!n) {
    return kPrimary;
  }
  switch (n->kind) {
  case NodeKind::SequenceExpression:
    return kSequence;
  case NodeKind::AssignmentExpression:
  case NodeKind::ArrowFunctionExpression:
  case NodeKind::YieldExpression:
    return kAssignment;
  case NodeKind::ConditionalExpression:
    return kConditional;
  case NodeKind::BinaryExpression:
  case NodeKind::LogicalExpression:
    return operatorPrecedence(n);
  case NodeKind::UnaryExpression:
  case NodeKind::AwaitExpression:
  case NodeKind::TSTypeAssertion:
    return kUnary;
  case NodeKind::UpdateExpression:
    return n->hasFlag(Flag::Prefix) ? kUnary : kPostfix;
  case NodeKind::TSNonNullExpression:
    return kPostfix;
  case NodeKind::TSAsExpression:
  case NodeKind::TSSatisfiesExpression:
    return 10;
  case NodeKind::NewExpression:
    return isNewWithoutArgs(n) ? kNewWithoutArgs : kCall;
  case NodeKind::CallExpression:
  case NodeKind::MemberExpression:
  case NodeKind::TaggedTemplateExpression:
  case NodeKind::TSInstantiationExpression:
  case NodeKind::ImportExpression:
    return kCall;
  default:
    return kPrimary;
  }
}

int typePrecedence(const Node *n)
{
  if (!n) {
    return kTypePrimary;
  }
  switch (n->kind) {
  case NodeKind::TSConditionalType:
    return kTypeConditional;
  case NodeKind::TSFunctionType:
  case NodeKind::TSConstructorType:
    return kTypeFunction;
  case NodeKind::TSUnionType:
    return kTypeUnion;
  case NodeKind::TSIntersectionType:
    return kTypeIntersection;
  case NodeKind::TSTypeOperator:
  case NodeKind::TSInferType:
    return kTypeOperator;
  case NodeKind::TSArrayType:
  case NodeKind::TSIndexedAccessType:
  case NodeKind::TSOptionalType:
    return kTypePostfix;
  default:
    return kTypePrimary;
  }
}

bool needsParens(const Node *parent, int index, const Node *child)
{
  if (!parent || !child || index < 0) {
    return false;
  }
  if (child->isType()) {
    int c = typePrecedence(child);
    switch (parent->kind) {
    case NodeKind::TSArrayType:
    case NodeKind::TSOptionalType:
      return c < kTypePostfix;
    case NodeKind::TSIndexedAccessType:
      return index == 0 && c < kTypePostfix;
    case NodeKind::TSUnionType:
      return c < kTypeUnion;
    case NodeKind::TSIntersectionType:
      return c < kTypeIntersection;
    case NodeKind::TSTypeOperator:
    case NodeKind::TSRestType:
      return c < kTypeOperator;
    case NodeKind::TSConditionalType:
      return index < 2 && c <= kTypeFunction;
    case NodeKind::TSFunctionType:
    case NodeKind::TSConstructorType:
      // A conditional or function return type is fine; a union in a
      // parameter binds through the annotation and needs nothing.
      return false;
    default:
      return false;
    }
  }

  if (!child->isExpression()) {
    return false;
  }
  int c = expressionPrecedence(child);

  switch (parent->kind) {
  case NodeKind::BinaryExpression:
  case NodeKind::LogicalExpression: {
    int p = operatorPrecedence(parent);
    if (isBinaryLike(child) &&
        ((isNullish(parent) && isOrAnd(child)) || (isOrAnd(parent) && isNullish(child))))
    {
      return true;
    }
    bool exponent = parent->kind == NodeKind::BinaryExpression &&
                    BinaryOperator(parent->dataByte(0)) == BinaryOperator::Exponent;
    if (index == 0) {
      // A unary operand on the left of `**` is a syntax error without parens.
      if (exponent && c == kUnary) {
        return true;
      }
      return exponent ? c <= p : c < p;
    }
    return exponent ? c < p : c <= p;
  }
  case NodeKind::UnaryExpression:
  case NodeKind::AwaitExpression:
  case NodeKind::TSTypeAssertion:
    if (parent->kind == NodeKind::UnaryExpression && isSignUnary(parent) &&
        isSignUnary(child) && leadingSign(parent) == leadingSign(child))
    {
      return true;
    }
    return c < kUnary;
  case NodeKind::UpdateExpression:
    return c < kCall;
  case NodeKind::MemberExpression:
    return index == 0 && (c < kCall || isBareInteger(child));
  case NodeKind::CallExpression:
  case NodeKind::TaggedTemplateExpression:
  case NodeKind::TSNonNullExpression:
  case NodeKind::TSInstantiationExpression:
    if (index == 0) {
      return parent->kind == NodeKind::TSNonNullExpression ? c < kPostfix : c < kCall;
    }
    return c <= kSequence;
  case NodeKind::NewExpression:
    if (index == 0) {
      return c < kCall || child->kind == NodeKind::CallExpression ||
             startsWith(child, NodeKind::CallExpression);
    }
    return c <= kSequence;
  case NodeKind::ConditionalExpression:
    return index == 0 ? c <= kConditional : c < kAssignment;
  case NodeKind::TSAsExpression:
  case NodeKind::TSSatisfiesExpression:
    return c < 10;
  case NodeKind::SequenceExpression:
    return c <= kSequence;
  case NodeKind::ExpressionStatement:
    return c <= kSequence ? false
                          : startsWith(child, NodeKind::ObjectExpression) ||
                                startsWith(child, NodeKind::FunctionExpression) ||
                                startsWith(child, NodeKind::ClassExpression);
  case NodeKind::ArrowFunctionExpression:
    // The body slot; a bare object literal would read as a block.
    if (index == 3 && parent->hasFlag(Flag::Expression)) {
      return c <= kSequence || startsWith(child, NodeKind::ObjectExpression);
    }
    return false;
  case NodeKind::ForStatement:
    return false;
  case NodeKind::ForInStatement:
  case NodeKind::ForOfStatement:
    // The head reads `in` as the loop keyword and `async` as a modifier.
    return index == 1 && c <= kSequence;
  case NodeKind::ExportDefaultDeclaration:
    return c <= kSequence;
  case NodeKind::Decorator:
    return c < kCall;
  case NodeKind::ArrayExpression:
  case NodeKind::ObjectExpression:
  case NodeKind::Property:
  case NodeKind::SpreadElement:
  case NodeKind::VariableDeclarator:
  case NodeKind::AssignmentExpression:
  case NodeKind::AssignmentPattern:
  case NodeKind::ReturnStatement:
  case NodeKind::ThrowStatement:
  case NodeKind::YieldExpression:
  case NodeKind::TemplateLiteral:
  case NodeKind::ImportExpression:
  case NodeKind::JSXExpressionContainer:
  case NodeKind::JSXSpreadAttribute:
  case NodeKind::JSXSpreadChild:
  case NodeKind::PropertyDefinition:
  case NodeKind::AccessorProperty:
  case NodeKind::TSAbstractPropertyDefinition:
  case NodeKind::TSAbstractAccessorProperty:
  case NodeKind::TSEnumMember:
    return c <= kSequence;
  default:
    return false;
  }
}

} // namespace fastlint::ast
