#include "fastlint/syntax/parser.h"

#include "fastlint/syntax/parser/internal.h"

namespace fastlint::syntax {

using litestl::util::string;

using detail::binaryPrecedence;
using detail::isAlwaysIdentifier;

// ----------------------------------------------------------------------- types

NodeId Parser::parseType()
{
  return parseTypeOrTypePredicate();
}

NodeId Parser::parseTypeOrTypePredicate()
{
  uint32_t firstToken = pos();
  // `asserts x is T`, `asserts x`, `x is T`
  if (is(TokenKind::AssertsKeyword)) {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::TypePredicate, firstToken);
    m_tree->node(node).flags |= FLAG_READONLY; // `asserts` marker (no dedicated flag)
    m_tree->addChild(parseTypeReference());
    if (eat(TokenKind::IsKeyword)) {
      m_tree->addChild(parseType());
    }
    return m_tree->endNode(node, pos());
  }
  Mark mark = begin();
  if (isAlwaysIdentifier(kind())) {
    uint32_t nameIndex = pos();
    m_scanner.scanOne();
    if (is(TokenKind::IsKeyword)) {
      rollback(mark);
      NodeId node = m_tree->beginNode(NodeKind::TypePredicate, nameIndex);
      m_tree->addChild(parseTypeReference());
      (void)expect(TokenKind::IsKeyword);
      m_tree->addChild(parseType());
      return m_tree->endNode(node, pos());
    }
  }
  rollback(mark);
  return parseUnionType();
}

NodeId Parser::parseUnionType()
{
  if (eat(TokenKind::PipeToken)) {
    // leading `|`
  }
  uint32_t firstToken = pos();
  NodeId first = parseIntersectionType();
  if (!is(TokenKind::PipeToken)) {
    return first;
  }
  NodeId node = m_tree->beginNode(NodeKind::UnionType, firstToken);
  m_tree->addChild(first);
  while (eat(TokenKind::PipeToken)) {
    m_tree->addChild(parseIntersectionType());
  }
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseIntersectionType()
{
  if (eat(TokenKind::AmpersandToken)) {
    // leading `&`
  }
  uint32_t firstToken = pos();
  NodeId first = parseTypeOperator();
  if (!is(TokenKind::AmpersandToken)) {
    return first;
  }
  NodeId node = m_tree->beginNode(NodeKind::IntersectionType, firstToken);
  m_tree->addChild(first);
  while (eat(TokenKind::AmpersandToken)) {
    m_tree->addChild(parseTypeOperator());
  }
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseTypeOperator()
{
  if (is(TokenKind::KeyOfKeyword) || is(TokenKind::UniqueKeyword) || is(TokenKind::ReadOnlyKeyword)) {
    uint32_t firstToken = pos();
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::TypeOperator, firstToken);
    m_tree->addChild(parseTypeOperator());
    return m_tree->endNode(node, pos());
  }
  return parsePostfixType(parsePrimaryType());
}

NodeId Parser::parsePostfixType(NodeId type)
{
  while (is(TokenKind::OpenBracketToken) && peekKind() == TokenKind::CloseBracketToken) {
    uint32_t firstToken = pos();
    m_scanner.scanOne();
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::ArrayType, firstToken);
    m_tree->addChild(type);
    type = m_tree->endNode(node, pos());
  }
  return type;
}

NodeId Parser::parsePrimaryType()
{
  uint32_t firstToken = pos();
  switch (kind()) {
  case TokenKind::OpenParenToken: {
    m_scanner.scanOne();
    NodeId inner = parseType();
    (void)expect(TokenKind::CloseParenToken);
    NodeId node = m_tree->beginNode(NodeKind::ParenthesizedType, firstToken);
    m_tree->addChild(inner);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::OpenBracketToken: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::TupleType, firstToken);
    parseDelimitedList(ListKind::TupleElements, [&] { return parseTupleElement(); });
    (void)expect(TokenKind::CloseBracketToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::OpenBraceToken: {
    // Type literal or mapped type.
    m_scanner.scanOne(); // `{`
    NodeId node = m_tree->beginNode(NodeKind::TypeLiteral, firstToken);
    // Mapped type check: `{ [K in T]… }` or `{ -readonly [K in T]… }`.
    Mark probe = begin();
    if (eat(TokenKind::MinusToken)) {
      m_scanner.scanOne();
    }
    if (eat(TokenKind::ReadOnlyKeyword)) {
      // modifier
    }
    bool isMapped = is(TokenKind::OpenBracketToken) && isAlwaysIdentifier(peekKind()) &&
                    peekKind2() == TokenKind::InKeyword;
    rollback(probe);
    if (isMapped) {
      m_tree->node(node).kind = NodeKind::MappedType;
      if (eat(TokenKind::PlusToken) || eat(TokenKind::MinusToken)) {
        m_tree->addChild(parseType());
      }
      (void)eat(TokenKind::OpenBracketToken);
      uint32_t paramIndex = pos();
      NodeId param = m_tree->beginNode(NodeKind::TypeParameter, paramIndex);
      m_scanner.scanOne(); // the key type parameter name
      (void)expect(TokenKind::InKeyword);
      m_tree->addChild(parseType());
      if (eat(TokenKind::AsKeyword)) {
        m_tree->addChild(parseType());
      }
      (void)expect(TokenKind::CloseBracketToken);
      m_tree->addChild(m_tree->endNode(param, pos()));
      if (eat(TokenKind::QuestionToken)) {
        // optional mapped member `?:`
      }
      if (eat(TokenKind::ColonToken)) {
        m_tree->addChild(parseType());
      }
    }
    parseList(ListKind::TypeMembers, [&] { return parseTypeMember(false); });
    (void)expect(TokenKind::CloseBraceToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::LessThanToken: {
    // Function type: `<T>(…) => U`.
    NodeId node = m_tree->beginNode(NodeKind::FunctionType, firstToken);
    m_tree->addChild(parseTypeParameters());
    (void)expect(TokenKind::OpenParenToken);
    parseParameterList(false, false);
    (void)expect(TokenKind::CloseParenToken);
    (void)expect(TokenKind::EqualsGreaterThanToken);
    m_tree->addChild(parseType());
    return m_tree->endNode(node, pos());
  }
    case TokenKind::NewKeyword: {
    // Constructor type: `new (…) => T`.
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::ConstructorType, firstToken);
    if (is(TokenKind::LessThanToken)) {
      m_tree->addChild(parseTypeParameters());
    }
    (void)expect(TokenKind::OpenParenToken);
    parseParameterList(false, false);
    (void)expect(TokenKind::CloseParenToken);
    (void)expect(TokenKind::EqualsGreaterThanToken);
    m_tree->addChild(parseType());
    return m_tree->endNode(node, pos());
  }
  case TokenKind::TypeOfKeyword: {
    m_scanner.scanOne();
    if (eat(TokenKind::ImportKeyword)) {
      NodeId node = m_tree->beginNode(NodeKind::ImportType, firstToken);
      m_tree->addChild(parseImportTypeRest(firstToken));
      return m_tree->endNode(node, pos());
    }
    NodeId node = m_tree->beginNode(NodeKind::TypeQuery, firstToken);
    m_tree->addChild(parseTypeReference());
    return m_tree->endNode(node, pos());
  }
  case TokenKind::ImportKeyword: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::ImportType, firstToken);
    m_tree->addChild(parseImportTypeRest(firstToken));
    return m_tree->endNode(node, pos());
  }
  case TokenKind::InferKeyword: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::InferType, firstToken);
    uint32_t paramIndex = pos();
    NodeId param = m_tree->beginNode(NodeKind::TypeParameter, paramIndex);
    if (isAlwaysIdentifier(kind())) {
      m_scanner.scanOne();
    }
    if (eat(TokenKind::ExtendsKeyword)) {
      m_tree->addChild(parseType()); // constraint, parsed without conditionals
    }
    m_tree->addChild(m_tree->endNode(param, pos()));
    return m_tree->endNode(node, pos());
  }
  case TokenKind::ThisKeyword: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::ThisType, firstToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::MinusToken: {
    // `-1` numeric literal type.
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::LiteralType, firstToken);
    uint32_t litIndex = pos();
    NodeId literal = m_tree->beginNode(NodeKind::NumericLiteral, litIndex);
    m_scanner.scanOne();
    m_tree->addChild(m_tree->endNode(literal, litIndex + 1));
    return m_tree->endNode(node, pos());
  }
  case TokenKind::StringLiteral:
  case TokenKind::NumericLiteral:
  case TokenKind::TrueKeyword:
  case TokenKind::FalseKeyword:
  case TokenKind::NullKeyword: {
    NodeId node = m_tree->beginNode(NodeKind::LiteralType, firstToken);
    m_scanner.scanOne();
    return m_tree->endNode(node, pos());
  }
  case TokenKind::BacktickToken:
  case TokenKind::TemplateHead:
  case TokenKind::NoSubstitutionTemplateLiteral: {
    // Template literal type.
    NodeId node = m_tree->beginNode(NodeKind::TemplateLiteralType, firstToken);
    if (is(TokenKind::NoSubstitutionTemplateLiteral)) {
      m_scanner.scanOne();
      return m_tree->endNode(node, pos());
    }
    m_scanner.scanOne(); // the head
    for (;;) {
      m_tree->addChild(parseType());
      if (is(TokenKind::CloseBraceToken)) {
        m_scanner.rescanTemplateTail(false);
        std::string_view text = m_scanner.text(m_scanner.current());
        bool tail = !text.empty() && text.back() == '`';
        if (tail) {
          m_scanner.rescanTemplateTail(true);
        }
        m_scanner.scanOne();
        if (tail) {
          break;
        }
        continue;
      }
      errorAt(token(), 1110, string("Type expected."));
      break;
    }
    return m_tree->endNode(node, pos());
  }
  default:
    if (isAlwaysIdentifier(kind())) {
      return parseTypeReference();
    }
    errorAt(token(), 1110, string("Type expected."));
    NodeId node = m_tree->beginNode(NodeKind::ErrorNode, firstToken);
    return m_tree->endNode(node, pos());
  }
}

NodeId Parser::parseTupleElement()
{
  if (is(TokenKind::DotDotDotToken)) {
    NodeId rest = m_tree->beginNode(NodeKind::RestType, pos());
    m_scanner.scanOne();
    m_tree->addChild(parseType());
    return m_tree->endNode(rest, pos());
  }
  if (isAlwaysIdentifier(kind()) && peekKind() == TokenKind::ColonToken) {
    // named tuple member: `name: T`
    NodeId member = m_tree->beginNode(NodeKind::NamedTupleMember, pos());
    m_scanner.scanOne();
    m_scanner.scanOne();
    m_tree->addChild(parseType());
    return m_tree->endNode(member, pos());
  }
  uint32_t firstToken = pos();
  bool optionalType = eat(TokenKind::QuestionToken);
  NodeId element = m_tree->beginNode(
      optionalType ? NodeKind::OptionalType : NodeKind::TypeReference, firstToken);
  m_tree->addChild(parseType());
  return m_tree->endNode(element, pos());
}

NodeId Parser::parseImportTypeRest(uint32_t firstToken)
{
  (void)expect(TokenKind::OpenParenToken);
  if (is(TokenKind::StringLiteral)) {
    m_scanner.scanOne();
  }
  (void)expect(TokenKind::CloseParenToken);
  while (eat(TokenKind::DotToken)) {
    if (isAlwaysIdentifier(kind())) {
      m_scanner.scanOne();
    }
  }
  if (eat(TokenKind::LessThanToken)) {
    m_tree->addChild(parseTypeArgumentList());
  }
  return m_tree->endNode(m_tree->beginNode(NodeKind::ImportType, firstToken), pos());
}

NodeId Parser::parseTypeReference()
{
  uint32_t firstToken = pos();
  NodeId node = m_tree->beginNode(NodeKind::TypeReference, firstToken);
  // Name: `A.B.C`.
  NodeId name = m_tree->beginNode(NodeKind::QualifiedName, firstToken);
  uint32_t firstName = pos();
  if (isAlwaysIdentifier(kind())) {
    m_scanner.scanOne();
  }
  else if (is(TokenKind::ThisKeyword)) {
    m_scanner.scanOne();
  }
  while (is(TokenKind::LessThanToken) == false && is(TokenKind::DotToken)) {
    m_scanner.scanOne();
    if (isAlwaysIdentifier(kind())) {
      m_scanner.scanOne();
    }
  }
  (void)firstName;
  m_tree->addChild(m_tree->endNode(name, pos()));
  if (is(TokenKind::LessThanToken)) {
    m_tree->addChild(parseTypeArgumentList());
  }
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseTypeParameters()
{
  uint32_t firstToken = pos();
  (void)expect(TokenKind::LessThanToken);
  NodeId node = m_tree->beginNode(NodeKind::TypeParameter, firstToken); // wrapper
  parseDelimitedList(ListKind::TypeParameters, [&] { return parseTypeParameter(); });
  (void)expect(TokenKind::GreaterThanToken);
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseTypeParameter()
{
  NodeId param = m_tree->beginNode(NodeKind::TypeParameter, pos());
  if (eat(TokenKind::ConstKeyword)) {
    // `const T` type parameter
  }
  // Variance annotations: `in T`, `out T`, `in out T`.
  while (is(TokenKind::InKeyword) || (is(TokenKind::OutKeyword) && wordFollows())) {
    m_scanner.scanOne();
  }
  if (isAlwaysIdentifier(kind())) {
    m_scanner.scanOne();
  } else {
    errorAt(token(), 1003, string("Identifier expected."));
  }
  if (eat(TokenKind::ExtendsKeyword)) {
    m_tree->addChild(parseType());
  }
  if (eat(TokenKind::EqualsToken)) {
    m_tree->addChild(parseType());
  }
  return m_tree->endNode(param, pos());
}

NodeId Parser::parseTypeArguments(bool speculative)
{
  (void)speculative;
  return parseTypeArgumentList();
}

NodeId Parser::parseTypeArgumentList()
{
  uint32_t firstToken = pos();
  (void)expect(TokenKind::LessThanToken);
  NodeId node = m_tree->beginNode(NodeKind::TypeReference, firstToken); // wrapper
  parseDelimitedList(ListKind::TypeArguments, [&] { return parseType(); });
  (void)expect(TokenKind::GreaterThanToken);
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseTypeMember(bool isInterface)
{
  (void)isInterface;
  uint32_t firstToken = pos();
  if (eat(TokenKind::OpenBracketToken)) {
    // Index signature.
    NodeId member = m_tree->beginNode(NodeKind::IndexSignature, firstToken);
    m_tree->addChild(parseBindingPattern());
    (void)expect(TokenKind::CloseBracketToken);
    if (eat(TokenKind::ColonToken)) {
      m_tree->addChild(parseType());
    }
    if (eat(TokenKind::SemicolonToken)) {
      // separator
    }
    return m_tree->endNode(member, pos());
  }
  if (is(TokenKind::NewKeyword) && peekKind() == TokenKind::OpenParenToken) {
    m_scanner.scanOne();
    NodeId member = m_tree->beginNode(NodeKind::ConstructSignature, firstToken);
    (void)expect(TokenKind::OpenParenToken);
    parseParameterList(false, false);
    (void)expect(TokenKind::CloseParenToken);
    if (eat(TokenKind::ColonToken)) {
      m_tree->addChild(parseTypeOrTypePredicate());
    }
    if (eat(TokenKind::SemicolonToken)) {
      // separator
    }
    return m_tree->endNode(member, pos());
  }
  bool isGet = false;
  bool isSet = false;
  if ((is(TokenKind::GetKeyword) || is(TokenKind::SetKeyword)) && wordFollows()) {
    isGet = is(TokenKind::GetKeyword);
    m_scanner.scanOne();
    isSet = !isGet;
  }
  NodeId name = parsePropertyName();
  NodeKind memberKind = isGet  ? NodeKind::GetAccessorSignature
                        : isSet ? NodeKind::SetAccessorSignature
                                : NodeKind::PropertySignature;
  if (is(TokenKind::OpenParenToken) || is(TokenKind::LessThanToken)) {
    memberKind = NodeKind::MethodSignature;
  }
  NodeId member = m_tree->beginNode(memberKind, firstToken);
  m_tree->addChild(name);
  if (eat(TokenKind::QuestionToken)) {
    m_tree->node(member).flags |= FLAG_OPTIONAL;
  }
  if (is(TokenKind::OpenParenToken) || is(TokenKind::LessThanToken)) {
    if (is(TokenKind::LessThanToken)) {
      m_tree->addChild(parseTypeParameters());
    }
    (void)expect(TokenKind::OpenParenToken);
    parseParameterList(false, false);
    (void)expect(TokenKind::CloseParenToken);
    if (eat(TokenKind::ColonToken)) {
      m_tree->addChild(parseTypeOrTypePredicate());
    }
  }
  else if (eat(TokenKind::ColonToken)) {
    m_tree->addChild(parseType());
  }
  if (eat(TokenKind::SemicolonToken) || eat(TokenKind::CommaToken)) {
    // separator
  }
  return m_tree->endNode(member, pos());
}

NodeId Parser::parseTypeLiteralBody()
{
  uint32_t firstToken = pos();
  (void)expect(TokenKind::OpenBraceToken);
  NodeId node = m_tree->beginNode(NodeKind::TypeLiteral, firstToken);
  parseList(ListKind::TypeMembers, [&] { return parseTypeMember(true); });
  (void)expect(TokenKind::CloseBraceToken);
  return m_tree->endNode(node, pos());
}


} // namespace fastlint::syntax
