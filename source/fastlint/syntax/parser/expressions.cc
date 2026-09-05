#include "fastlint/syntax/parser.h"
#include "fastlint/syntax/parser/internal.h"

namespace fastlint::syntax {

using litestl::util::string;

using detail::binaryPrecedence;
using detail::isAlwaysIdentifier;

// ----------------------------------------------------------------- expressions

NodeId Parser::parseExpression(bool disallowComma)
{
  NodeId expression = parseAssignmentExpression();
  if (!disallowComma && is(TokenKind::CommaToken)) {
    uint32_t firstToken = pos() - 1;
    NodeId node = m_tree->beginNode(NodeKind::BinaryExpression, firstToken);
    m_tree->addChild(expression);
    while (eat(TokenKind::CommaToken)) {
      m_tree->addChild(parseAssignmentExpression());
    }
    return m_tree->endNode(node, pos());
  }
  return expression;
}

NodeId Parser::parseAssignmentExpression()
{
  if (m_inYieldContext && is(TokenKind::YieldKeyword)) {
    uint32_t firstToken = pos();
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::YieldExpression, firstToken);
    if (eat(TokenKind::AsteriskToken)) {
      // delegate: `yield* gen()`
    }
    if (!hasPrecedingLineBreak() && !is(TokenKind::SemicolonToken) &&
        !is(TokenKind::CloseBraceToken) && !is(TokenKind::CloseParenToken) &&
        !is(TokenKind::EndOfFile) && !is(TokenKind::ColonToken))
    {
      m_tree->addChild(parseAssignmentExpression());
    }
    return m_tree->endNode(node, pos());
  }

  // Arrow function heads: `x =>`, `async x =>`, `async (…) =>`, `(…) =>`.
  if (is(TokenKind::AsyncKeyword) && !nextHasLineBreak()) {
    TokenKind next = peekKind();
    if (next == TokenKind::FunctionKeyword) {
      m_scanner.scanOne();
      return parseFunctionExpression(true);
    }
    if (isAlwaysIdentifier(next) || next == TokenKind::OpenParenToken ||
        next == TokenKind::LessThanToken)
    {
      return parseArrowFunction(true);
    }
  } else if (isAlwaysIdentifier(kind()) &&
             peekKind() == TokenKind::EqualsGreaterThanToken)
  {
    return parseArrowFunction(false);
  } else if (is(TokenKind::LessThanToken) && !m_options.jsx && isArrowHead()) {
    return parseArrowFunction(false);
  }

  uint32_t start = pos();
  NodeId left = parseConditional();
  if (binaryPrecedence(kind(), false) == 2) {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::BinaryExpression, start);
    m_tree->addChild(left);
    m_tree->addChild(parseAssignmentExpression());
    return m_tree->endNode(node, pos());
  }
  return left;
}

bool Parser::isArrowHead()
{
  Mark mark = begin();
  // Parameters attach to the innermost open node, so the probe needs one.
  (void)m_tree->beginNode(NodeKind::ArrowFunction, pos());
  if (is(TokenKind::LessThanToken)) {
    (void)parseTypeParameters();
  }
  bool ok = eat(TokenKind::OpenParenToken);
  if (ok) {
    parseParameterList(false, false);
    ok = eat(TokenKind::CloseParenToken);
  }
  if (ok && eat(TokenKind::ColonToken)) {
    (void)parseTypeOrTypePredicate();
  }
  ok = ok && is(TokenKind::EqualsGreaterThanToken) && !hasPrecedingLineBreak() &&
       m_diagnostics.size() == mark.diagnostics;
  rollback(mark);
  return ok;
}

/** From `async`, `<`, `(` or the lone parameter through the body. */
NodeId Parser::parseArrowFunction(bool asyncFlag)
{
  uint32_t firstToken = pos() - (asyncFlag ? 1 : 0);
  if (asyncFlag) {
    m_scanner.scanOne(); // `async`
  }
  NodeId node = m_tree->beginNode(NodeKind::ArrowFunction, firstToken);
  if (asyncFlag) {
    m_tree->node(node).flags |= FLAG_ASYNC;
  }
  if (is(TokenKind::LessThanToken)) {
    m_tree->addChild(parseTypeParameters());
  }
  if (is(TokenKind::OpenParenToken)) {
    (void)expect(TokenKind::OpenParenToken);
    parseParameterList(false, false);
    (void)expect(TokenKind::CloseParenToken);
  } else {
    // Single parameter without parens: `x => …`.
    m_tree->addChild(parseBindingPattern());
  }
  if (eat(TokenKind::ColonToken)) {
    m_tree->addChild(parseTypeOrTypePredicate());
  }
  (void)expect(TokenKind::EqualsGreaterThanToken);
  bool savedYield = m_inYieldContext;
  bool savedAwait = m_allowAwait;
  m_inYieldContext = false;
  m_allowAwait = asyncFlag || savedAwait;
  if (is(TokenKind::OpenBraceToken)) {
    m_tree->addChild(parseBlock());
  } else {
    m_tree->addChild(parseAssignmentExpression());
  }
  m_inYieldContext = savedYield;
  m_allowAwait = savedAwait;
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseConditional()
{
  uint32_t firstToken = pos();
  NodeId condition = parseBinary(0);
  if (is(TokenKind::QuestionToken)) {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::ConditionalExpression, firstToken);
    m_tree->addChild(condition);
    m_tree->addChild(parseAssignmentExpression());
    (void)expect(TokenKind::ColonToken);
    m_tree->addChild(parseAssignmentExpression());
    return m_tree->endNode(node, pos());
  }
  return condition;
}

NodeId Parser::parseBinary(int minPrecedence)
{
  constexpr int relationalPrecedence = 10;
  uint32_t firstToken = pos();
  NodeId left = parseUnary();
  for (;;) {
    // `x as T` and `x satisfies T` bind like a relational operator and take
    // a type; a line break before the keyword ends the expression instead.
    if ((is(TokenKind::AsKeyword) || is(TokenKind::SatisfiesKeyword)) &&
        !hasPrecedingLineBreak() && !m_options.javaScript)
    {
      if (relationalPrecedence <= minPrecedence) {
        return left;
      }
      NodeKind kind = is(TokenKind::AsKeyword) ? NodeKind::AsExpression
                                               : NodeKind::SatisfiesExpression;
      m_scanner.scanOne();
      NodeId node = m_tree->beginNode(kind, firstToken);
      m_tree->addChild(left);
      m_tree->addChild(parseType());
      left = m_tree->endNode(node, pos());
      continue;
    }
    int precedence = binaryPrecedence(kind(), m_disallowIn);
    if (precedence <= minPrecedence || precedence <= 2) { // assignment is handled higher
      return left;
    }
    // `**` is right-associative; every other operator is left-associative.
    int rightMinimum = is(TokenKind::AsteriskAsteriskToken) ? precedence - 1 : precedence;
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::BinaryExpression, firstToken);
    m_tree->addChild(left);
    m_tree->addChild(parseBinary(rightMinimum));
    left = m_tree->endNode(node, pos());
  }
}

NodeId Parser::parseUnary()
{
  uint32_t firstToken = pos();
  switch (kind()) {
  case TokenKind::PlusToken:
  case TokenKind::MinusToken:
  case TokenKind::TildeToken:
  case TokenKind::ExclamationToken: {
    TokenKind op = kind();
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::PrefixUnaryExpression, firstToken);
    (void)op;
    m_tree->addChild(parseUnary());
    return m_tree->endNode(node, pos());
  }
  case TokenKind::PlusPlusToken:
  case TokenKind::MinusMinusToken: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::PrefixUnaryExpression, firstToken);
    m_tree->addChild(parseUnary());
    return m_tree->endNode(node, pos());
  }
  case TokenKind::DeleteKeyword:
  case TokenKind::TypeOfKeyword:
  case TokenKind::VoidKeyword: {
    NodeKind expressionKind =
        kind() == TokenKind::DeleteKeyword   ? NodeKind::DeleteExpression
        : kind() == TokenKind::TypeOfKeyword ? NodeKind::TypeOfExpression
                                             : NodeKind::VoidExpression;
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(expressionKind, firstToken);
    m_tree->addChild(parseUnary());
    return m_tree->endNode(node, pos());
  }
  case TokenKind::AwaitKeyword: {
    if (m_allowAwait || m_options.javaScript) {
      m_scanner.scanOne();
      NodeId node = m_tree->beginNode(NodeKind::AwaitExpression, firstToken);
      m_tree->addChild(parseUnary());
      return m_tree->endNode(node, pos());
    }
    break; // plain identifier `await`
  }
  case TokenKind::LessThanToken: {
    if (!m_options.javaScript && !m_options.jsx) {
      // `<T>expr` type assertion in .ts files.
      Mark mark = begin();
      m_scanner.scanOne();
      bool looksLikeAssertion = !is(TokenKind::EqualsToken);
      rollback(mark);
      if (looksLikeAssertion) {
        m_scanner.scanOne();
        NodeId node = m_tree->beginNode(NodeKind::TypeAssertionExpression, firstToken);
        m_tree->addChild(parseType());
        (void)expectGreaterThan();
        m_tree->addChild(parseUnary());
        return m_tree->endNode(node, pos());
      }
    }
    break;
  }
  default:
    break;
  }
  return parsePostfix();
}

NodeId Parser::parsePostfix()
{
  NodeId expression = parseCallChain(parsePrimary());
  while ((is(TokenKind::PlusPlusToken) || is(TokenKind::MinusMinusToken)) &&
         !hasPrecedingLineBreak())
  {
    uint32_t firstToken = m_tree->node(expression).firstToken;
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::PostfixUnaryExpression, firstToken);
    m_tree->addChild(expression);
    expression = m_tree->endNode(node, pos());
  }
  return expression;
}

NodeId Parser::parsePrimary()
{
  uint32_t firstToken = pos();
  detail::FlagScope allowIn(m_disallowIn, false); // brackets reset the `for` head ban
  switch (kind()) {
  case TokenKind::AtToken:
    return parseClassExpression();
  case TokenKind::ThisKeyword: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::ThisExpression, firstToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::SuperKeyword: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::SuperExpression, firstToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::NullKeyword: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::NullLiteral, firstToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::TrueKeyword:
  case TokenKind::FalseKeyword: {
    NodeKind literal =
        is(TokenKind::TrueKeyword) ? NodeKind::TrueLiteral : NodeKind::FalseLiteral;
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(literal, firstToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::NumericLiteral:
  case TokenKind::BigIntLiteral: {
    NodeKind literal =
        is(TokenKind::BigIntLiteral) ? NodeKind::BigIntLiteral : NodeKind::NumericLiteral;
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(literal, firstToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::StringLiteral: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::StringLiteral, firstToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::SlashToken:
  case TokenKind::SlashEqualsToken:
    // A slash where an expression starts is a regular expression.
    if (!m_scanner.rescanSlash()) {
      break;
    }
    [[fallthrough]];
  case TokenKind::RegularExpressionLiteral: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::RegularExpressionLiteral, firstToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::BacktickToken:
  case TokenKind::TemplateHead:
  case TokenKind::NoSubstitutionTemplateLiteral:
    return parseTemplateLiteral();
  case TokenKind::OpenParenToken:
    return parseParenthesizedOrArrow();
  case TokenKind::OpenBracketToken: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::ArrayLiteralExpression, firstToken);
    parseDelimitedList(ListKind::ArrayLiteralMembers, [&] {
      if (is(TokenKind::CommaToken)) {
        return missingNode(NodeKind::OmittedExpression, pos());
      }
      if (is(TokenKind::DotDotDotToken)) {
        NodeId spread = m_tree->beginNode(NodeKind::SpreadElement, pos());
        m_scanner.scanOne();
        m_tree->addChild(parseAssignmentExpression());
        return m_tree->endNode(spread, pos());
      }
      return parseAssignmentExpression();
    });
    (void)expect(TokenKind::CloseBracketToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::OpenBraceToken:
    return parseObjectLiteral();
  case TokenKind::FunctionKeyword:
    return parseFunctionExpression(false);
  case TokenKind::ClassKeyword:
    return parseClassExpression();
  case TokenKind::NewKeyword: {
    m_scanner.scanOne();
    if (eat(TokenKind::DotToken)) {
      // `new.target`
      NodeId node = m_tree->beginNode(NodeKind::MetaProperty, firstToken);
      uint32_t nameIndex = pos();
      NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
      if (isAlwaysIdentifier(kind())) {
        m_scanner.scanOne();
      } else {
        errorAt(token(), 1003, string("Identifier expected."));
        m_tree->node(name).flags |= FLAG_MISSING;
      }
      m_tree->addChild(m_tree->endNode(name, pos()));
      return m_tree->endNode(node, pos());
    }
    NodeId node = m_tree->beginNode(NodeKind::NewExpression, firstToken);
    m_tree->addChild(parseCallChain(parsePrimary()));
    return m_tree->endNode(node, pos());
  }
  case TokenKind::ImportKeyword: {
    // `import.meta` / `import("…")`
    m_scanner.scanOne();
    if (eat(TokenKind::DotToken)) {
      NodeId node = m_tree->beginNode(NodeKind::MetaProperty, firstToken);
      uint32_t nameIndex = pos();
      NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
      if (isAlwaysIdentifier(kind())) {
        m_scanner.scanOne();
      }
      m_tree->addChild(m_tree->endNode(name, nameIndex + 1));
      return m_tree->endNode(node, pos());
    }
    if (is(TokenKind::OpenParenToken)) {
      NodeId node = m_tree->beginNode(NodeKind::CallExpression, firstToken);
      NodeId callee = m_tree->beginNode(NodeKind::Identifier, firstToken);
      callee = m_tree->endNode(callee, pos());
      m_tree->addChild(callee);
      m_tree->addChild(parseArguments());
      return m_tree->endNode(node, pos());
    }
    errorAt(token(), 1479, string("Expression expected."));
    NodeId node = m_tree->beginNode(NodeKind::ErrorNode, firstToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::PrivateIdentifier: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::PrivateIdentifier, firstToken);
    return m_tree->endNode(node, pos());
  }
  default:
    break;
  }
  if (isAlwaysIdentifier(kind())) {
    NodeId node = m_tree->beginNode(NodeKind::Identifier, firstToken);
    m_scanner.scanOne();
    return m_tree->endNode(node, pos());
  }
  errorAt(token(), 1479, string("Expression expected."));
  NodeId node = m_tree->beginNode(NodeKind::ErrorNode, firstToken);
  m_scanner.scanOne();
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseParenthesizedOrArrow()
{
  if (isArrowHead()) {
    return parseArrowFunction(false);
  }
  uint32_t firstToken = pos();
  NodeId node = m_tree->beginNode(NodeKind::ParenthesizedExpression, firstToken);
  (void)expect(TokenKind::OpenParenToken);
  m_tree->addChild(parseExpression());
  (void)expect(TokenKind::CloseParenToken);
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseObjectLiteral()
{
  uint32_t firstToken = pos();
  (void)expect(TokenKind::OpenBraceToken);
  NodeId node = m_tree->beginNode(NodeKind::ObjectLiteralExpression, firstToken);
  parseDelimitedList(ListKind::ObjectLiteralMembers, [&] { return parseObjectMember(); });
  (void)expect(TokenKind::CloseBraceToken);
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseObjectMember()
{
  uint32_t firstToken = pos();
  if (eat(TokenKind::DotDotDotToken)) {
    NodeId spread = m_tree->beginNode(NodeKind::SpreadAssignment, firstToken);
    m_tree->addChild(parseAssignmentExpression());
    return m_tree->endNode(spread, pos());
  }
  bool asyncFlag = is(TokenKind::AsyncKeyword) && nextCanFollowModifier();
  if (eat(TokenKind::AsteriskToken)) {
    // Generator method.
    NodeId member = m_tree->beginNode(NodeKind::MethodDeclaration, firstToken);
    m_tree->node(member).flags |= FLAG_GENERATOR;
    m_tree->addChild(parsePropertyName());
    if (is(TokenKind::LessThanToken)) {
      m_tree->addChild(parseTypeParameters());
    }
    (void)expect(TokenKind::OpenParenToken);
    parseParameterList(false, false);
    (void)expect(TokenKind::CloseParenToken);
    if (eat(TokenKind::ColonToken)) {
      m_tree->addChild(parseTypeOrTypePredicate());
    }
    m_tree->addChild(parseBlock());
    return m_tree->endNode(member, pos());
  }
  if (is(TokenKind::GetKeyword) || is(TokenKind::SetKeyword)) {
    bool isGet = is(TokenKind::GetKeyword);
    Mark mark = begin();
    m_scanner.scanOne();
    bool memberForm = !is(TokenKind::CommaToken) && !is(TokenKind::CloseBraceToken) &&
                      !is(TokenKind::ColonToken) && !is(TokenKind::OpenParenToken) &&
                      !is(TokenKind::EqualsToken);
    rollback(mark);
    if (memberForm) {
      m_scanner.scanOne();
      NodeId member = m_tree->beginNode(
          isGet ? NodeKind::GetAccessor : NodeKind::SetAccessor, firstToken);
      m_tree->addChild(parsePropertyName());
      (void)expect(TokenKind::OpenParenToken);
      parseParameterList(false, false);
      (void)expect(TokenKind::CloseParenToken);
      if (eat(TokenKind::ColonToken)) {
        m_tree->addChild(parseTypeOrTypePredicate());
      }
      m_tree->addChild(parseBlock());
      return m_tree->endNode(member, pos());
    }
  }
  if (asyncFlag && peekKind() == TokenKind::AsteriskToken) {
    m_scanner.scanOne();
    {
      m_scanner.scanOne();
      NodeId member = m_tree->beginNode(NodeKind::MethodDeclaration, firstToken);
      m_tree->node(member).flags |= FLAG_ASYNC | FLAG_GENERATOR;
      m_tree->addChild(parsePropertyName());
      if (is(TokenKind::LessThanToken)) {
        m_tree->addChild(parseTypeParameters());
      }
      (void)expect(TokenKind::OpenParenToken);
      parseParameterList(false, false);
      (void)expect(TokenKind::CloseParenToken);
      if (eat(TokenKind::ColonToken)) {
        m_tree->addChild(parseTypeOrTypePredicate());
      }
      detail::FlagScope allowAwait(m_allowAwait, true);
      detail::FlagScope allowYield(m_inYieldContext, true);
      m_tree->addChild(parseBlock());
      return m_tree->endNode(member, pos());
    }
  }

  // Property, shorthand property, or method.
  Mark mark = begin();
  NodeId name = parsePropertyName();
  bool methodForm = is(TokenKind::OpenParenToken) || is(TokenKind::LessThanToken) ||
                    (asyncFlag && isStartOfPropertyName());
  rollback(mark);

  if (methodForm) {
    if (asyncFlag) {
      m_scanner.scanOne(); // `async`
    }
    NodeId member = m_tree->beginNode(NodeKind::MethodDeclaration, firstToken);
    if (asyncFlag) {
      m_tree->node(member).flags |= FLAG_ASYNC;
    }
    m_tree->addChild(parsePropertyName());
    if (is(TokenKind::LessThanToken)) {
      m_tree->addChild(parseTypeParameters());
    }
    (void)expect(TokenKind::OpenParenToken);
    parseParameterList(false, false);
    (void)expect(TokenKind::CloseParenToken);
    if (eat(TokenKind::ColonToken)) {
      m_tree->addChild(parseTypeOrTypePredicate());
    }
    detail::FlagScope allowAwait(m_allowAwait, asyncFlag);
    detail::FlagScope allowYield(m_inYieldContext, false);
    m_tree->addChild(parseBlock());
    return m_tree->endNode(member, pos());
  }

  name = parsePropertyName();
  if (eat(TokenKind::ColonToken)) {
    NodeId member = m_tree->beginNode(NodeKind::PropertyAssignment, firstToken);
    m_tree->addChild(name);
    m_tree->addChild(parseAssignmentExpression());
    return m_tree->endNode(member, pos());
  }
  // Shorthand `{ a }`, possibly with a default `{ a = 1 }`.
  NodeId member = m_tree->beginNode(NodeKind::ShorthandPropertyAssignment, firstToken);
  m_tree->addChild(name);
  if (eat(TokenKind::EqualsToken)) {
    m_tree->addChild(parseAssignmentExpression());
  }
  return m_tree->endNode(member, pos());
}

NodeId Parser::parsePropertyName()
{
  uint32_t firstToken = pos();
  if (is(TokenKind::OpenBracketToken)) {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::ComputedPropertyName, firstToken);
    m_tree->addChild(parseAssignmentExpression());
    (void)expect(TokenKind::CloseBracketToken);
    return m_tree->endNode(node, pos());
  }
  NodeKind nameKind = is(TokenKind::StringLiteral)       ? NodeKind::StringLiteral
                      : is(TokenKind::NumericLiteral)    ? NodeKind::NumericLiteral
                      : is(TokenKind::PrivateIdentifier) ? NodeKind::PrivateIdentifier
                                                         : NodeKind::Identifier;
  NodeId node = m_tree->beginNode(nameKind, firstToken);
  if (is(TokenKind::StringLiteral) || is(TokenKind::NumericLiteral) ||
      is(TokenKind::PrivateIdentifier) || isAlwaysIdentifier(kind()) ||
      tokenIsKeyword(kind()))
  {
    m_scanner.scanOne();
  } else {
    errorAt(token(), 1174, string("Property name expected."));
    m_tree->node(node).flags |= FLAG_MISSING;
  }
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseArguments()
{
  uint32_t firstToken = pos();
  (void)expect(TokenKind::OpenParenToken);
  NodeId node =
      m_tree->beginNode(NodeKind::OmittedExpression, firstToken); // arguments run
  detail::FlagScope allowIn(m_disallowIn, false);
  parseDelimitedList(ListKind::Arguments, [&] {
    if (is(TokenKind::DotDotDotToken)) {
      NodeId spread = m_tree->beginNode(NodeKind::SpreadElement, pos());
      m_scanner.scanOne();
      m_tree->addChild(parseAssignmentExpression());
      return m_tree->endNode(spread, pos());
    }
    return parseAssignmentExpression();
  });
  (void)expect(TokenKind::CloseParenToken);
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseTemplateLiteral()
{
  uint32_t firstToken = pos();
  // The scanner produced TemplateHead (or NoSubstitution) tokens; the parser
  // drives the rescan boundary after each substitution's `}`.
  if (is(TokenKind::NoSubstitutionTemplateLiteral)) {
    NodeId node = m_tree->beginNode(NodeKind::NoSubstitutionTemplateLiteral, firstToken);
    m_scanner.scanOne();
    return m_tree->endNode(node, pos());
  }
  (void)expect(TokenKind::TemplateHead);
  NodeId node = m_tree->beginNode(NodeKind::TemplateExpression, firstToken);
  for (;;) {
    NodeId span = m_tree->beginNode(NodeKind::TemplateSpan, pos());
    m_tree->addChild(parseExpression());
    if (is(TokenKind::CloseBraceToken)) {
      // Rescan `}` + text into a template middle or tail (the `}` stays part
      // of that token, like TS). Middle mode that ran to the closing backtick
      // is really a tail.
      m_scanner.rescanTemplateTail(false);
      std::string_view text = m_scanner.text(m_scanner.current());
      bool tail = !text.empty() && text.back() == '`';
      if (tail) {
        m_scanner.rescanTemplateTail(true);
      }
      m_scanner.scanOne(); // past the middle/tail token
      m_tree->addChild(m_tree->endNode(span, pos()));
      if (tail) {
        break;
      }
      continue;
    }
    errorAt(token(), 1109, string("Expression expected."));
    break;
  }
  return m_tree->endNode(node, pos());
}

// -------------------------------------------------------------- member chains

bool Parser::canFollowTypeArguments()
{
  switch (kind()) {
  case TokenKind::OpenParenToken:
  case TokenKind::NoSubstitutionTemplateLiteral:
  case TokenKind::TemplateHead:
  case TokenKind::BacktickToken:
    return true;
  case TokenKind::LessThanToken:
  case TokenKind::GreaterThanToken:
  case TokenKind::PlusToken:
  case TokenKind::MinusToken:
    return false;
  default:
    // `f<T>` followed by something that could continue a comparison is
    // taken as the comparison, matching TS.
    return hasPrecedingLineBreak() || !isStartOfExpression();
  }
}

bool Parser::tryParseTypeArguments(NodeId &typeArguments)
{
  Mark mark = begin();
  NodeId list = parseTypeArgumentList();
  if (m_diagnostics.size() != mark.diagnostics || !canFollowTypeArguments()) {
    rollback(mark);
    return false;
  }
  typeArguments = list;
  return true;
}

NodeId Parser::parseCallChain(NodeId expression)
{
  for (;;) {
    switch (kind()) {
    case TokenKind::LessThanToken: {
      NodeId typeArguments = kNoNode;
      if (m_options.javaScript || !tryParseTypeArguments(typeArguments)) {
        return expression;
      }
      NodeId node = m_tree->beginNode(NodeKind::ExpressionWithTypeArguments,
                                      m_tree->node(expression).firstToken);
      m_tree->addChild(expression);
      m_tree->addChild(typeArguments);
      expression = m_tree->endNode(node, pos());
      break;
    }
    case TokenKind::NoSubstitutionTemplateLiteral:
    case TokenKind::TemplateHead:
    case TokenKind::BacktickToken: {
      NodeId node = m_tree->beginNode(NodeKind::TaggedTemplateExpression,
                                      m_tree->node(expression).firstToken);
      m_tree->addChild(expression);
      m_tree->addChild(parseTemplateLiteral());
      expression = m_tree->endNode(node, pos());
      break;
    }
    case TokenKind::DotToken: {
      m_scanner.scanOne();
      NodeId node = m_tree->beginNode(NodeKind::PropertyAccessExpression,
                                      m_tree->node(expression).firstToken);
      m_tree->addChild(expression);
      m_tree->addChild(parseMemberName(kind()));
      expression = m_tree->endNode(node, pos());
      break;
    }
    case TokenKind::OpenBracketToken: {
      uint32_t firstToken = pos();
      m_scanner.scanOne();
      NodeId node = m_tree->beginNode(NodeKind::ElementAccessExpression, firstToken);
      m_tree->addChild(expression);
      {
        detail::FlagScope allowIn(m_disallowIn, false);
        m_tree->addChild(parseExpression());
      }
      (void)expect(TokenKind::CloseBracketToken);
      expression = m_tree->endNode(node, pos());
      break;
    }
    case TokenKind::OpenParenToken: {
      NodeKind kind = hasFlags(m_tree->node(expression).flags, FLAG_OPTIONAL_CHAIN)
                          ? NodeKind::OptionalCallExpression
                          : NodeKind::CallExpression;
      uint32_t firstToken = pos();
      NodeId node = m_tree->beginNode(kind, firstToken);
      m_tree->addChild(expression);
      m_tree->addChild(parseArguments());
      expression = m_tree->endNode(node, pos());
      break;
    }
    case TokenKind::QuestionDotToken: {
      uint32_t firstToken = pos();
      m_scanner.scanOne();
      if (is(TokenKind::OpenParenToken)) {
        NodeId node = m_tree->beginNode(NodeKind::OptionalCallExpression, firstToken);
        m_tree->addChild(expression);
        m_tree->addChild(parseArguments());
        expression = m_tree->endNode(node, pos());
      } else if (is(TokenKind::OpenBracketToken)) {
        m_scanner.scanOne();
        NodeId node = m_tree->beginNode(NodeKind::ElementAccessExpression, firstToken);
        m_tree->node(node).flags |= FLAG_OPTIONAL_CHAIN;
        m_tree->addChild(expression);
        m_tree->addChild(parseAssignmentExpression());
        (void)expect(TokenKind::CloseBracketToken);
        expression = m_tree->endNode(node, pos());
      } else {
        NodeId node = m_tree->beginNode(NodeKind::PropertyAccessExpression, firstToken);
        m_tree->node(node).flags |= FLAG_OPTIONAL_CHAIN;
        m_tree->addChild(expression);
        m_tree->addChild(parseMemberName(kind()));
        expression = m_tree->endNode(node, pos());
      }
      break;
    }
    case TokenKind::ExclamationToken: {
      if (hasPrecedingLineBreak()) {
        return expression;
      }
      m_scanner.scanOne();
      NodeId node = m_tree->beginNode(NodeKind::NonNullExpression,
                                      m_tree->node(expression).firstToken);
      m_tree->addChild(expression);
      expression = m_tree->endNode(node, pos());
      break;
    }
    default:
      return expression;
    }
  }
}

NodeId Parser::parseMemberName(TokenKind propertyKind)
{
  (void)propertyKind;
  uint32_t firstToken = pos();
  NodeId node = m_tree->beginNode(NodeKind::Identifier, firstToken);
  if (isAlwaysIdentifier(kind()) || tokenIsKeyword(kind()) ||
      is(TokenKind::PrivateIdentifier) || is(TokenKind::NumericLiteral) ||
      is(TokenKind::StringLiteral))
  {
    m_scanner.scanOne();
  } else {
    errorAt(token(), 1003, string("Identifier expected."));
    m_tree->node(node).flags |= FLAG_MISSING;
    return m_tree->endNode(node, firstToken);
  }
  return m_tree->endNode(node, pos());
}

} // namespace fastlint::syntax
