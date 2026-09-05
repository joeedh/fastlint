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
  // `x is T` / `this is T`: the name and `is` sit on one line.
  if ((isAlwaysIdentifier(kind()) || is(TokenKind::ThisKeyword)) &&
      peekKind() == TokenKind::IsKeyword && !nextHasLineBreak())
  {
    NodeId node = m_tree->beginNode(NodeKind::TypePredicate, firstToken);
    m_tree->addChild(parseTypeReference());
    (void)expect(TokenKind::IsKeyword);
    m_tree->addChild(parseType());
    return m_tree->endNode(node, pos());
  }
  return parseConditionalType();
}

NodeId Parser::parseConditionalType()
{
  uint32_t firstToken = pos();
  NodeId check = parseUnionType();
  if (m_noConditionalType || !is(TokenKind::ExtendsKeyword) || hasPrecedingLineBreak()) {
    return check;
  }
  m_scanner.scanOne();
  NodeId node = m_tree->beginNode(NodeKind::ConditionalType, firstToken);
  m_tree->addChild(check);
  bool saved = m_noConditionalType;
  m_noConditionalType = true;
  m_tree->addChild(parseUnionType());
  m_noConditionalType = saved;
  (void)expect(TokenKind::QuestionToken);
  m_tree->addChild(parseType());
  (void)expect(TokenKind::ColonToken);
  m_tree->addChild(parseType());
  return m_tree->endNode(node, pos());
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
  if (is(TokenKind::KeyOfKeyword) || is(TokenKind::UniqueKeyword) ||
      is(TokenKind::ReadOnlyKeyword))
  {
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
  uint32_t firstToken = m_tree->node(type).firstToken;
  while (is(TokenKind::OpenBracketToken) && !hasPrecedingLineBreak()) {
    m_scanner.scanOne();
    if (eat(TokenKind::CloseBracketToken)) {
      NodeId node = m_tree->beginNode(NodeKind::ArrayType, firstToken);
      m_tree->addChild(type);
      type = m_tree->endNode(node, pos());
      continue;
    }
    NodeId node = m_tree->beginNode(NodeKind::IndexedAccessType, firstToken);
    m_tree->addChild(type);
    bool saved = m_noConditionalType;
    m_noConditionalType = false;
    m_tree->addChild(parseType());
    m_noConditionalType = saved;
    (void)expect(TokenKind::CloseBracketToken);
    type = m_tree->endNode(node, pos());
  }
  return type;
}

NodeId Parser::parsePrimaryType()
{
  uint32_t firstToken = pos();
  switch (kind()) {
  case TokenKind::OpenParenToken: {
    if (isArrowHead()) {
      return parseFunctionType(NodeKind::FunctionType, firstToken);
    }
    m_scanner.scanOne();
    bool saved = m_noConditionalType;
    m_noConditionalType = false;
    NodeId inner = parseType();
    m_noConditionalType = saved;
    (void)expect(TokenKind::CloseParenToken);
    NodeId node = m_tree->beginNode(NodeKind::ParenthesizedType, firstToken);
    m_tree->addChild(inner);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::OpenBracketToken: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::TupleType, firstToken);
    bool saved = m_noConditionalType;
    m_noConditionalType = false;
    parseDelimitedList(ListKind::TupleElements, [&] { return parseTupleElement(); });
    m_noConditionalType = saved;
    (void)expect(TokenKind::CloseBracketToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::OpenBraceToken: {
    // Type literal or mapped type.
    m_scanner.scanOne(); // `{`
    NodeId node = m_tree->beginNode(NodeKind::TypeLiteral, firstToken);
    // Mapped type check: `{ [K in T]… }` with optional `+`/`-` `readonly`.
    Mark probe = begin();
    (void)(eat(TokenKind::PlusToken) || eat(TokenKind::MinusToken));
    (void)eat(TokenKind::ReadOnlyKeyword);
    bool isMapped = is(TokenKind::OpenBracketToken) && isAlwaysIdentifier(peekKind()) &&
                    peekKind2() == TokenKind::InKeyword;
    rollback(probe);
    if (isMapped) {
      // The `+`/`-` prefixes are recorded as tokens only; the flags say
      // whether `readonly`/`?` are present at all.
      m_tree->node(node).kind = NodeKind::MappedType;
      (void)(eat(TokenKind::PlusToken) || eat(TokenKind::MinusToken));
      if (eat(TokenKind::ReadOnlyKeyword)) {
        m_tree->node(node).flags |= FLAG_READONLY;
      }
      (void)expect(TokenKind::OpenBracketToken);
      uint32_t paramIndex = pos();
      NodeId param = m_tree->beginNode(NodeKind::TypeParameter, paramIndex);
      m_scanner.scanOne(); // the key type parameter name
      (void)expect(TokenKind::InKeyword);
      m_tree->addChild(parseType());
      m_tree->addChild(m_tree->endNode(param, pos()));
      if (eat(TokenKind::AsKeyword)) {
        m_tree->addChild(parseType());
      }
      (void)expect(TokenKind::CloseBracketToken);
      (void)(eat(TokenKind::PlusToken) || eat(TokenKind::MinusToken));
      if (eat(TokenKind::QuestionToken)) {
        m_tree->node(node).flags |= FLAG_OPTIONAL;
      }
      if (eat(TokenKind::ColonToken)) {
        m_tree->addChild(parseType());
      }
      (void)(eat(TokenKind::SemicolonToken) || eat(TokenKind::CommaToken));
      (void)expect(TokenKind::CloseBraceToken);
      return m_tree->endNode(node, pos());
    }
    bool saved = m_noConditionalType;
    m_noConditionalType = false;
    parseList(ListKind::TypeMembers, [&] { return parseTypeMember(false); });
    m_noConditionalType = saved;
    (void)expect(TokenKind::CloseBraceToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::LessThanToken:
    return parseFunctionType(NodeKind::FunctionType, firstToken);
  case TokenKind::NewKeyword:
    m_scanner.scanOne();
    return parseFunctionType(NodeKind::ConstructorType, firstToken);
  case TokenKind::AbstractKeyword: {
    if (peekKind() != TokenKind::NewKeyword) {
      break;
    }
    m_scanner.scanOne();
    m_scanner.scanOne();
    NodeId node = parseFunctionType(NodeKind::ConstructorType, firstToken);
    m_tree->node(node).flags |= FLAG_ABSTRACT;
    return node;
  }
  case TokenKind::TypeOfKeyword: {
    m_scanner.scanOne();
    if (eat(TokenKind::ImportKeyword)) {
      NodeId node = m_tree->beginNode(NodeKind::ImportType, firstToken);
      parseImportTypeRest();
      return m_tree->endNode(node, pos());
    }
    NodeId node = m_tree->beginNode(NodeKind::TypeQuery, firstToken);
    m_tree->addChild(parseTypeReference());
    return m_tree->endNode(node, pos());
  }
  case TokenKind::ImportKeyword: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::ImportType, firstToken);
    parseImportTypeRest();
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
    if (is(TokenKind::ExtendsKeyword)) {
      // `infer U extends C ? …` outside a conditional's extends operand is
      // the conditional's own `extends`, so the constraint is dropped then.
      Mark mark = begin();
      m_scanner.scanOne();
      bool saved = m_noConditionalType;
      m_noConditionalType = true;
      m_tree->addChild(parseUnionType());
      m_noConditionalType = saved;
      if (!saved && is(TokenKind::QuestionToken)) {
        rollback(mark);
      }
    }
    m_tree->addChild(m_tree->endNode(param, pos()));
    return m_tree->endNode(node, pos());
  }
  case TokenKind::ThisKeyword: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::ThisType, firstToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::VoidKeyword: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::KeywordType, firstToken);
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
  case TokenKind::BigIntLiteral:
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
    break;
  }
  // `const` is the `as const` marker type.
  if (isAlwaysIdentifier(kind()) || is(TokenKind::ConstKeyword)) {
    return parseTypeReference();
  }
  errorAt(token(), 1110, string("Type expected."));
  NodeId node = m_tree->beginNode(NodeKind::ErrorNode, firstToken);
  return m_tree->endNode(node, pos());
}

/** `<T>(params) => R`; the caller consumed `new` for a constructor type. */
NodeId Parser::parseFunctionType(NodeKind kind, uint32_t firstToken)
{
  NodeId node = m_tree->beginNode(kind, firstToken);
  if (is(TokenKind::LessThanToken)) {
    m_tree->addChild(parseTypeParameters());
  }
  (void)expect(TokenKind::OpenParenToken);
  parseParameterList(false, false);
  (void)expect(TokenKind::CloseParenToken);
  (void)expect(TokenKind::EqualsGreaterThanToken);
  // The return type may be conditional even inside an `extends` operand.
  detail::FlagScope allowConditional(m_noConditionalType, false);
  m_tree->addChild(parseTypeOrTypePredicate());
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseTupleElement()
{
  if (is(TokenKind::DotDotDotToken)) {
    NodeId rest = m_tree->beginNode(NodeKind::RestType, pos());
    m_scanner.scanOne();
    if (isAlwaysIdentifier(kind()) && peekKind() == TokenKind::ColonToken) {
      // `...name: T`
      NodeId member = m_tree->beginNode(NodeKind::NamedTupleMember, pos());
      m_scanner.scanOne();
      m_scanner.scanOne();
      m_tree->addChild(parseType());
      m_tree->addChild(m_tree->endNode(member, pos()));
    } else {
      m_tree->addChild(parseType());
    }
    return m_tree->endNode(rest, pos());
  }
  uint32_t firstToken = pos();
  if (isAlwaysIdentifier(kind()) &&
      (peekKind() == TokenKind::ColonToken ||
       (peekKind() == TokenKind::QuestionToken && peekKind2() == TokenKind::ColonToken)))
  {
    // named tuple member: `name: T` / `name?: T`
    NodeId member = m_tree->beginNode(NodeKind::NamedTupleMember, firstToken);
    m_scanner.scanOne();
    if (eat(TokenKind::QuestionToken)) {
      m_tree->node(member).flags |= FLAG_OPTIONAL;
    }
    (void)expect(TokenKind::ColonToken);
    m_tree->addChild(parseType());
    return m_tree->endNode(member, pos());
  }
  NodeId type = parseType();
  if (!is(TokenKind::QuestionToken)) {
    return type;
  }
  // `T?` optional element
  m_scanner.scanOne();
  NodeId element = m_tree->beginNode(NodeKind::OptionalType, firstToken);
  m_tree->addChild(type);
  return m_tree->endNode(element, pos());
}

void Parser::parseImportTypeRest()
{
  (void)expect(TokenKind::OpenParenToken);
  if (is(TokenKind::StringLiteral)) {
    uint32_t index = pos();
    NodeId module = m_tree->beginNode(NodeKind::StringLiteral, index);
    m_scanner.scanOne();
    m_tree->addChild(m_tree->endNode(module, index + 1));
  } else {
    errorAt(token(), 1141, string("String literal expected."));
  }
  (void)expect(TokenKind::CloseParenToken);
  if (is(TokenKind::DotToken)) {
    NodeId qualifier = m_tree->beginNode(NodeKind::QualifiedName, pos());
    while (eat(TokenKind::DotToken)) {
      if (isAlwaysIdentifier(kind()) || tokenIsKeyword(kind())) {
        m_scanner.scanOne();
      } else {
        errorAt(token(), 1003, string("Identifier expected."));
        break;
      }
    }
    m_tree->addChild(m_tree->endNode(qualifier, pos()));
  }
  if (is(TokenKind::LessThanToken)) {
    m_tree->addChild(parseTypeArgumentList());
  }
}

NodeId Parser::parseTypeReference()
{
  uint32_t firstToken = pos();
  NodeId node = m_tree->beginNode(NodeKind::TypeReference, firstToken);
  // Name: `A.B.C`.
  NodeId name = m_tree->beginNode(NodeKind::QualifiedName, firstToken);
  if (isAlwaysIdentifier(kind()) || is(TokenKind::ThisKeyword) ||
      is(TokenKind::ConstKeyword))
  {
    m_scanner.scanOne();
  } else {
    errorAt(token(), 1110, string("Type expected."));
  }
  while (is(TokenKind::DotToken)) {
    m_scanner.scanOne();
    if (isAlwaysIdentifier(kind()) || tokenIsKeyword(kind())) {
      m_scanner.scanOne();
    } else {
      errorAt(token(), 1003, string("Identifier expected."));
    }
  }
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
  NodeId node = m_tree->beginNode(NodeKind::TypeParameters, firstToken);
  parseDelimitedList(ListKind::TypeParameters, [&] { return parseTypeParameter(); });
  (void)expectGreaterThan();
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
  NodeId node = m_tree->beginNode(NodeKind::TypeArguments, firstToken);
  bool saved = m_noConditionalType;
  m_noConditionalType = false;
  parseDelimitedList(ListKind::TypeArguments, [&] { return parseType(); });
  m_noConditionalType = saved;
  (void)expectGreaterThan();
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseTypeMember(bool isInterface)
{
  (void)isInterface;
  uint32_t firstToken = pos();
  uint32_t flags = FLAG_NONE;
  if (is(TokenKind::ReadOnlyKeyword) &&
      (wordFollows() || peekKind() == TokenKind::OpenBracketToken ||
       peekKind() == TokenKind::StringLiteral || peekKind() == TokenKind::NumericLiteral))
  {
    flags |= FLAG_READONLY;
    m_scanner.scanOne();
  }
  // `[name: T]` is an index signature; any other `[` opens a computed name.
  if (is(TokenKind::OpenBracketToken) && peekKind2() == TokenKind::ColonToken) {
    m_scanner.scanOne();
    NodeId member = m_tree->beginNode(NodeKind::IndexSignature, firstToken);
    m_tree->node(member).flags |= flags;
    parseIndexSignatureRest();
    (void)(eat(TokenKind::SemicolonToken) || eat(TokenKind::CommaToken));
    return m_tree->endNode(member, pos());
  }
  bool construct =
      is(TokenKind::NewKeyword) &&
      (peekKind() == TokenKind::OpenParenToken || peekKind() == TokenKind::LessThanToken);
  if (construct || is(TokenKind::OpenParenToken) || is(TokenKind::LessThanToken)) {
    NodeId member = m_tree->beginNode(
        construct ? NodeKind::ConstructSignature : NodeKind::CallSignature, firstToken);
    if (construct) {
      m_scanner.scanOne();
    }
    if (is(TokenKind::LessThanToken)) {
      m_tree->addChild(parseTypeParameters());
    }
    (void)expect(TokenKind::OpenParenToken);
    parseParameterList(false, false);
    (void)expect(TokenKind::CloseParenToken);
    if (eat(TokenKind::ColonToken)) {
      m_tree->addChild(parseTypeOrTypePredicate());
    }
    (void)(eat(TokenKind::SemicolonToken) || eat(TokenKind::CommaToken));
    return m_tree->endNode(member, pos());
  }
  bool isGet = false;
  bool isSet = false;
  if ((is(TokenKind::GetKeyword) || is(TokenKind::SetKeyword)) && nextCanFollowModifier())
  {
    isGet = is(TokenKind::GetKeyword);
    m_scanner.scanOne();
    isSet = !isGet;
  }
  NodeId name = parsePropertyName();
  if (eat(TokenKind::QuestionToken)) {
    flags |= FLAG_OPTIONAL;
  }
  NodeKind memberKind = isGet   ? NodeKind::GetAccessorSignature
                        : isSet ? NodeKind::SetAccessorSignature
                                : NodeKind::PropertySignature;
  if (is(TokenKind::OpenParenToken) || is(TokenKind::LessThanToken)) {
    memberKind = NodeKind::MethodSignature;
  }
  NodeId member = m_tree->beginNode(memberKind, firstToken);
  m_tree->node(member).flags |= flags;
  m_tree->addChild(name);
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
  } else if (eat(TokenKind::ColonToken)) {
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
