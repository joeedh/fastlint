#include "fastlint/syntax/parser.h"

#include "fastlint/syntax/parser/internal.h"

namespace fastlint::syntax {

using litestl::util::string;

using detail::binaryPrecedence;
using detail::isAlwaysIdentifier;
using detail::isBindingIdentifier;

// ------------------------------------------------------------------ lookahead

/** True when the current word token begins a `type` alias (contextual check). */
bool Parser::startsTypeAlias()
{
  return kind() == TokenKind::TypeKeyword && wordFollows() &&
         (peekKind2() == TokenKind::EqualsToken ||
          peekKind2() == TokenKind::LessThanToken);
}

/** True when a name follows the current contextual keyword on the same line. */
bool Parser::wordFollows()
{
  return !nextHasLineBreak() && isBindingIdentifier(peekKind());
}

bool Parser::nextStartsPropertyName()
{
  Mark mark = begin();
  m_scanner.scanOne();
  bool result = isStartOfPropertyName();
  rollback(mark);
  return result;
}

bool Parser::nextCanFollowModifier(bool sameLine)
{
  Mark mark = begin();
  m_scanner.scanOne();
  bool result = (!sameLine || !hasPrecedingLineBreak()) &&
                (isStartOfPropertyName() || is(TokenKind::OpenBracketToken) ||
                 is(TokenKind::AsteriskToken) || is(TokenKind::OpenBraceToken));
  rollback(mark);
  return result;
}

/** True when a line break separates the current token from the next one. */
bool Parser::nextHasLineBreak()
{
  Mark mark = begin();
  m_scanner.scanOne();
  bool result = hasPrecedingLineBreak();
  rollback(mark);
  return result;
}

bool Parser::letStartsDeclaration()
{
  TokenKind next = peekKind();
  return isBindingIdentifier(next) || next == TokenKind::OpenBracketToken ||
         next == TokenKind::OpenBraceToken;
}

bool Parser::usingStartsDeclaration()
{
  Mark mark = begin();
  bool result = false;
  if (eat(TokenKind::AwaitKeyword) &&
      (hasPrecedingLineBreak() || !is(TokenKind::UsingKeyword)))
  {
    rollback(mark);
    return false;
  }
  if (eat(TokenKind::UsingKeyword)) {
    // `using of`/`using in` is the identifier `using` in a for head.
    result = !hasPrecedingLineBreak() && isAlwaysIdentifier(kind()) &&
             !is(TokenKind::OfKeyword) && !is(TokenKind::InKeyword);
  }
  rollback(mark);
  return result;
}

// ------------------------------------------------------------------ statements

NodeId Parser::parseStatement()
{
  switch (kind()) {
  case TokenKind::OpenBraceToken:
    return parseBlock();
  case TokenKind::SemicolonToken: {
    uint32_t semi = pos();
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::EmptyStatement, semi);
    return m_tree->endNode(node, semi + 1);
  }
  case TokenKind::IfKeyword:
    return parseIfStatement();
  case TokenKind::WhileKeyword:
    return parseWhileStatement();
  case TokenKind::DoKeyword:
    return parseDoStatement();
  case TokenKind::ForKeyword:
    return parseForStatement();
  case TokenKind::BreakKeyword:
    return parseBreakStatement();
  case TokenKind::ContinueKeyword:
    return parseContinueStatement();
  case TokenKind::ReturnKeyword:
    return parseReturnStatement();
  case TokenKind::ThrowKeyword:
    return parseThrowStatement();
  case TokenKind::TryKeyword:
    return parseTryStatement();
  case TokenKind::SwitchKeyword:
    return parseSwitchStatement();
  case TokenKind::WithKeyword:
    return parseWithStatement();
  case TokenKind::DebuggerKeyword: {
    uint32_t firstToken = pos();
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::DebuggerStatement, firstToken);
    return endStatement(node);
  }
  case TokenKind::AtToken:
    return parseDecoratedStatement();
  case TokenKind::ImportKeyword: {
    // `import.meta` and `import(…)` are expressions.
    TokenKind next = peekKind();
    if (next == TokenKind::DotToken || next == TokenKind::OpenParenToken) {
      return parseExpressionOrLabeledStatement();
    }
    return parseImportDeclaration(false);
  }
  case TokenKind::ExportKeyword:
    return parseExportDeclaration(false);
  default:
    break;
  }

  TokenKind k = kind();
  if (k == TokenKind::AsyncKeyword && !nextHasLineBreak() &&
      peekKind() == TokenKind::FunctionKeyword)
  {
    m_scanner.scanOne();
    return parseFunctionDeclaration(true, false);
  }

  if (k == TokenKind::AbstractKeyword && peekKind() == TokenKind::ClassKeyword &&
      !nextHasLineBreak())
  {
    m_scanner.scanOne();
    return parseClassDeclaration(false, true);
  }

  if (k == TokenKind::DeclareKeyword && !nextHasLineBreak()) {
    TokenKind next = peekKind();
    if (next == TokenKind::FunctionKeyword) {
      m_scanner.scanOne();
      return parseFunctionDeclaration(false, true);
    }
    if (next == TokenKind::EnumKeyword ||
        (next == TokenKind::ConstKeyword && peekKind2() == TokenKind::EnumKeyword))
    {
      m_scanner.scanOne();
      return parseEnumDeclaration(true);
    }
    if (next == TokenKind::InterfaceKeyword) {
      m_scanner.scanOne();
      return parseInterfaceDeclaration(true);
    }
    if (next == TokenKind::TypeKeyword) {
      m_scanner.scanOne();
      return parseTypeAlias(true);
    }
    if (next == TokenKind::ClassKeyword || next == TokenKind::AbstractKeyword) {
      Mark probe = begin();
      m_scanner.scanOne();
      if (kind() == TokenKind::AbstractKeyword) {
        m_scanner.scanOne();
      }
      bool classFollows = kind() == TokenKind::ClassKeyword;
      rollback(probe);
      if (classFollows) {
        m_scanner.scanOne();
        bool abstractFlag = eat(TokenKind::AbstractKeyword);
        return parseClassDeclaration(true, abstractFlag);
      }
      // `declare` as a plain identifier.
      return parseExpressionOrLabeledStatement();
    }
    if (next == TokenKind::VarKeyword || next == TokenKind::LetKeyword ||
        next == TokenKind::ConstKeyword || next == TokenKind::UsingKeyword ||
        next == TokenKind::AwaitKeyword)
    {
      m_scanner.scanOne();
      return parseVariableStatement(true);
    }
    if (next == TokenKind::ModuleKeyword || next == TokenKind::NamespaceKeyword ||
        next == TokenKind::GlobalKeyword)
    {
      m_scanner.scanOne();
      return parseModuleDeclaration(true);
    }
  }

  if (k == TokenKind::ConstKeyword && peekKind() == TokenKind::EnumKeyword) {
    return parseEnumDeclaration(false);
  }
  if (k == TokenKind::TypeKeyword && startsTypeAlias()) {
    return parseTypeAlias(false);
  }
  if (k == TokenKind::InterfaceKeyword && wordFollows()) {
    return parseInterfaceDeclaration(false);
  }
  if (k == TokenKind::EnumKeyword && wordFollows()) {
    return parseEnumDeclaration(false);
  }
  if ((k == TokenKind::ModuleKeyword || k == TokenKind::NamespaceKeyword) &&
      (wordFollows() ||
       (k == TokenKind::ModuleKeyword && peekKind() == TokenKind::StringLiteral)))
  {
    return parseModuleDeclaration(false);
  }
  if (k == TokenKind::GlobalKeyword && peekKind() == TokenKind::OpenBraceToken) {
    // `global { … }` augmentation inside an ambient module body.
    return parseModuleDeclaration(false);
  }
  if (k == TokenKind::ConstKeyword || k == TokenKind::VarKeyword ||
      (k == TokenKind::LetKeyword && letStartsDeclaration()) ||
      ((k == TokenKind::UsingKeyword || k == TokenKind::AwaitKeyword) &&
       usingStartsDeclaration()))
  {
    return parseVariableStatement(false);
  }
  if (k == TokenKind::FunctionKeyword) {
    return parseFunctionDeclaration(false, false);
  }
  if (k == TokenKind::ClassKeyword) {
    return parseClassDeclaration(false, false);
  }

  return parseExpressionOrLabeledStatement();
}

NodeId Parser::parseExpressionOrLabeledStatement()
{
  uint32_t firstToken = pos();
  // `name:` is a label; labels cannot be keywords or `let`/`async` heads.
  if (isAlwaysIdentifier(kind()) && kind() != TokenKind::LetKeyword &&
      kind() != TokenKind::AsyncKeyword && peekKind() == TokenKind::ColonToken)
  {
    uint32_t nameIndex = pos();
    m_scanner.scanOne();
    NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
    name = m_tree->endNode(name, nameIndex + 1);
    uint32_t colon = expect(TokenKind::ColonToken);
    (void)colon;
    NodeId label = m_tree->beginNode(NodeKind::LabeledStatement, firstToken);
    m_tree->addChild(name);
    m_tree->addChild(parseStatement());
    return m_tree->endNode(label, pos());
  }

  NodeId expression = parseExpression();
  NodeId node = m_tree->beginNode(NodeKind::ExpressionStatement, firstToken);
  m_tree->addChild(expression);
  return endStatement(node);
}

NodeId Parser::parseBlock()
{
  uint32_t firstToken = pos();
  (void)expect(TokenKind::OpenBraceToken);
  NodeId block = m_tree->beginNode(NodeKind::Block, firstToken);
  detail::FlagScope allowIn(m_disallowIn, false);
  parseList(ListKind::BlockStatements, [&] { return parseStatement(); });
  (void)expect(TokenKind::CloseBraceToken);
  return m_tree->endNode(block, pos());
}

NodeId Parser::parseVariableStatement(bool declareFlag)
{
  uint32_t firstToken = pos() - (declareFlag ? 1 : 0); // the caller ate `declare`
  NodeId list = parseVariableDeclarationList(kind());
  NodeId node = m_tree->beginNode(NodeKind::VariableStatement, firstToken);
  if (declareFlag) {
    m_tree->node(node).flags |= FLAG_AMBIENT;
  }
  m_tree->addChild(list);
  return endStatement(node);
}

/** From `var`/`let`/`const`/`using`/`await using` through the declarators. */
NodeId Parser::parseVariableDeclarationList(TokenKind keyword)
{
  uint32_t firstToken = pos();
  uint32_t flags = FLAG_NONE;
  if (keyword == TokenKind::AwaitKeyword) {
    m_scanner.scanOne();
    flags |= FLAG_AWAIT;
    keyword = TokenKind::UsingKeyword;
  }
  if (keyword == TokenKind::UsingKeyword) {
    flags |= FLAG_USING;
  }
  if (keyword == TokenKind::ConstKeyword) {
    flags |= FLAG_CONST;
  }
  (void)expect(keyword);
  NodeId list = m_tree->beginNode(NodeKind::VariableDeclarationList, firstToken);
  m_tree->node(list).flags |= flags;
  do {
    m_tree->addChild(parseVariableDeclaration());
  } while (eat(TokenKind::CommaToken));
  return m_tree->endNode(list, pos());
}

NodeId Parser::parseVariableDeclaration()
{
  uint32_t firstToken = pos();
  NodeId declaration = m_tree->beginNode(NodeKind::VariableDeclaration, firstToken);
  m_tree->addChild(parseBindingPattern());
  if (is(TokenKind::ExclamationToken)) { // definite assignment `let x!: T`
    uint32_t bang = pos();
    m_scanner.scanOne();
    (void)bang;
  }
  if (eat(TokenKind::ColonToken)) {
    m_tree->addChild(parseType());
  }
  if (eat(TokenKind::EqualsToken)) {
    m_tree->addChild(parseAssignmentExpression());
  }
  return m_tree->endNode(declaration, pos());
}

// ------------------------------------------------------ control-flow statements

NodeId Parser::parseIfStatement()
{
  uint32_t firstToken = pos();
  m_scanner.scanOne();
  NodeId node = m_tree->beginNode(NodeKind::IfStatement, firstToken);
  (void)expect(TokenKind::OpenParenToken);
  m_tree->addChild(parseExpression());
  (void)expect(TokenKind::CloseParenToken);
  m_tree->addChild(parseStatement());
  if (eat(TokenKind::ElseKeyword)) {
    m_tree->addChild(parseStatement());
  }
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseWhileStatement()
{
  uint32_t firstToken = pos();
  m_scanner.scanOne();
  NodeId node = m_tree->beginNode(NodeKind::WhileStatement, firstToken);
  (void)expect(TokenKind::OpenParenToken);
  m_tree->addChild(parseExpression());
  (void)expect(TokenKind::CloseParenToken);
  bool saved = m_inIteration;
  m_inIteration = true;
  m_tree->addChild(parseStatement());
  m_inIteration = saved;
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseDoStatement()
{
  uint32_t firstToken = pos();
  m_scanner.scanOne();
  NodeId node = m_tree->beginNode(NodeKind::DoStatement, firstToken);
  bool saved = m_inIteration;
  m_inIteration = true;
  m_tree->addChild(parseStatement());
  m_inIteration = saved;
  (void)expect(TokenKind::WhileKeyword);
  (void)expect(TokenKind::OpenParenToken);
  m_tree->addChild(parseExpression());
  (void)expect(TokenKind::CloseParenToken);
  // ASI applies after `)` here regardless of line breaks (ES2015 §12.9.1).
  if (!eat(TokenKind::SemicolonToken)) {
    m_tree->node(node).flags |= FLAG_ASI;
  }
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseForStatement()
{
  uint32_t firstToken = pos();
  m_scanner.scanOne();
  bool awaitFlag = eat(TokenKind::AwaitKeyword); // `for await (x of y)`
  (void)expect(TokenKind::OpenParenToken);

  NodeId initializer = kNoNode;
  if (!is(TokenKind::SemicolonToken) && !is(TokenKind::OfKeyword) &&
      !is(TokenKind::InKeyword))
  {
    detail::FlagScope noIn(m_disallowIn, true);
    if (kind() == TokenKind::ConstKeyword || kind() == TokenKind::VarKeyword ||
        (kind() == TokenKind::LetKeyword && letStartsDeclaration()) ||
        ((kind() == TokenKind::UsingKeyword || kind() == TokenKind::AwaitKeyword) &&
         usingStartsDeclaration()))
    {
      initializer = parseVariableDeclarationList(kind());
    } else {
      initializer = parseExpression(true); // `in` is not allowed in for-init
    }
  }

  if (is(TokenKind::OfKeyword) || is(TokenKind::InKeyword)) {
    NodeKind loopKind =
        is(TokenKind::OfKeyword) ? NodeKind::ForOfStatement : NodeKind::ForInStatement;
    m_scanner.scanOne();
    NodeId loop = m_tree->beginNode(loopKind, firstToken);
    if (awaitFlag) {
      m_tree->node(loop).flags |= FLAG_AWAIT;
    }
    if (initializer != kNoNode) {
      m_tree->addChild(initializer);
    }
    m_tree->addChild(parseExpression());
    (void)expect(TokenKind::CloseParenToken);
    bool saved = m_inIteration;
    m_inIteration = true;
    m_tree->addChild(parseStatement());
    m_inIteration = saved;
    return m_tree->endNode(loop, pos());
  }

  NodeId node = m_tree->beginNode(NodeKind::ForStatement, firstToken);
  if (initializer != kNoNode) {
    m_tree->addChild(initializer);
  } else {
    m_tree->addChild(missingNode(NodeKind::OmittedExpression, pos()));
  }
  (void)expect(TokenKind::SemicolonToken);
  if (!is(TokenKind::SemicolonToken)) {
    m_tree->addChild(parseExpression());
  } else {
    m_tree->addChild(missingNode(NodeKind::OmittedExpression, pos()));
  }
  (void)expect(TokenKind::SemicolonToken);
  if (!is(TokenKind::CloseParenToken)) {
    m_tree->addChild(parseExpression());
  } else {
    m_tree->addChild(missingNode(NodeKind::OmittedExpression, pos()));
  }
  (void)expect(TokenKind::CloseParenToken);
  bool saved = m_inIteration;
  m_inIteration = true;
  m_tree->addChild(parseStatement());
  m_inIteration = saved;
  return m_tree->endNode(node, pos());
}

/** A zero-token placeholder child. */
NodeId Parser::missingNode(NodeKind kind, uint32_t at)
{
  NodeId node = m_tree->beginNode(kind, at);
  node = m_tree->endNode(node, at);
  m_tree->node(node).flags |= FLAG_MISSING;
  return node;
}

NodeId Parser::parseBreakStatement()
{
  return parseBreakOrContinue(NodeKind::BreakStatement, TokenKind::BreakKeyword);
}

NodeId Parser::parseContinueStatement()
{
  return parseBreakOrContinue(NodeKind::ContinueStatement, TokenKind::ContinueKeyword);
}

NodeId Parser::parseBreakOrContinue(NodeKind nodeKind, TokenKind keyword)
{
  (void)keyword;
  uint32_t firstToken = pos();
  m_scanner.scanOne();
  NodeId node = m_tree->beginNode(nodeKind, firstToken);
  if (isAlwaysIdentifier(kind()) && !hasPrecedingLineBreak()) {
    uint32_t nameIndex = pos();
    m_scanner.scanOne();
    NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
    m_tree->addChild(m_tree->endNode(name, nameIndex + 1));
  }
  return endStatement(node);
}

NodeId Parser::parseReturnStatement()
{
  uint32_t firstToken = pos();
  m_scanner.scanOne();
  NodeId node = m_tree->beginNode(NodeKind::ReturnStatement, firstToken);
  // Restricted production: the expression must start on the same line.
  if (!hasPrecedingLineBreak() && !is(TokenKind::SemicolonToken) &&
      !is(TokenKind::CloseBraceToken) && !is(TokenKind::EndOfFile))
  {
    m_tree->addChild(parseExpression());
  }
  return endStatement(node);
}

NodeId Parser::parseThrowStatement()
{
  uint32_t firstToken = pos();
  m_scanner.scanOne();
  NodeId node = m_tree->beginNode(NodeKind::ThrowStatement, firstToken);
  if (hasPrecedingLineBreak()) {
    errorAt(token(), 1392, string("Line break not allowed in throw statement."));
  }
  if (!is(TokenKind::SemicolonToken) && !is(TokenKind::CloseBraceToken) &&
      !is(TokenKind::EndOfFile))
  {
    m_tree->addChild(parseExpression());
  }
  return endStatement(node);
}

NodeId Parser::parseTryStatement()
{
  uint32_t firstToken = pos();
  m_scanner.scanOne();
  NodeId node = m_tree->beginNode(NodeKind::TryStatement, firstToken);
  m_tree->addChild(parseBlock());
  if (eat(TokenKind::CatchKeyword)) {
    uint32_t catchFirst = pos() - 1;
    NodeId clause = m_tree->beginNode(NodeKind::CatchClause, catchFirst);
    if (eat(TokenKind::OpenParenToken)) {
      NodeId variable = m_tree->beginNode(NodeKind::VariableDeclaration, pos());
      m_tree->addChild(parseBindingPattern());
      if (eat(TokenKind::ColonToken)) {
        m_tree->addChild(parseType());
      }
      m_tree->addChild(m_tree->endNode(variable, pos()));
      (void)expect(TokenKind::CloseParenToken);
    }
    m_tree->addChild(parseBlock());
    m_tree->addChild(m_tree->endNode(clause, pos()));
  }
  if (eat(TokenKind::FinallyKeyword)) {
    m_tree->addChild(parseBlock());
  }
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseSwitchStatement()
{
  uint32_t firstToken = pos();
  m_scanner.scanOne();
  NodeId node = m_tree->beginNode(NodeKind::SwitchStatement, firstToken);
  (void)expect(TokenKind::OpenParenToken);
  m_tree->addChild(parseExpression());
  (void)expect(TokenKind::CloseParenToken);
  (void)expect(TokenKind::OpenBraceToken);
  bool saved = m_inSwitch;
  m_inSwitch = true;
  parseList(ListKind::SwitchClauses, [&] { return parseSwitchClause(); });
  m_inSwitch = saved;
  (void)expect(TokenKind::CloseBraceToken);
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseSwitchClause()
{
  NodeKind clauseKind =
      is(TokenKind::CaseKeyword) ? NodeKind::CaseClause : NodeKind::DefaultClause;
  uint32_t clauseFirst = pos();
  m_scanner.scanOne();
  NodeId clause = m_tree->beginNode(clauseKind, clauseFirst);
  if (clauseKind == NodeKind::CaseClause) {
    m_tree->addChild(parseExpression());
  }
  (void)expect(TokenKind::ColonToken);
  parseList(ListKind::SwitchClauseStatements, [&] { return parseStatement(); });
  return m_tree->endNode(clause, pos());
}

NodeId Parser::parseWithStatement()
{
  uint32_t firstToken = pos();
  m_scanner.scanOne();
  NodeId node = m_tree->beginNode(NodeKind::WithStatement, firstToken);
  (void)expect(TokenKind::OpenParenToken);
  m_tree->addChild(parseExpression());
  (void)expect(TokenKind::CloseParenToken);
  m_tree->addChild(parseStatement());
  return m_tree->endNode(node, pos());
}

} // namespace fastlint::syntax
