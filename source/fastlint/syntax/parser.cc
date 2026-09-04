#include "fastlint/syntax/parser.h"
#include <cstdio>
#include <cstdlib>

namespace fastlint::syntax {

using litestl::util::string;

namespace {

/** Precedence for the binary/assignment operators, 0 = not an operator. */
int binaryPrecedence(TokenKind kind, bool disallowIn)
{
  switch (kind) {
  case TokenKind::EqualsToken:
  case TokenKind::PlusEqualsToken:
  case TokenKind::MinusEqualsToken:
  case TokenKind::AsteriskEqualsToken:
  case TokenKind::AsteriskAsteriskEqualsToken:
  case TokenKind::SlashEqualsToken:
  case TokenKind::PercentEqualsToken:
  case TokenKind::AmpersandEqualsToken:
  case TokenKind::PipeEqualsToken:
  case TokenKind::CaretEqualsToken:
  case TokenKind::LessThanLessThanEqualsToken:
  case TokenKind::GreaterThanGreaterThanEqualsToken:
  case TokenKind::GreaterThanGreaterThanGreaterThanEqualsToken:
  case TokenKind::QuestionQuestionEqualsToken:
  case TokenKind::AmpersandAmpersandEqualsToken:
  case TokenKind::PipePipeEqualsToken:
    return 2;
  case TokenKind::QuestionQuestionToken:
    return 3;
  case TokenKind::PipePipeToken:
    return 4;
  case TokenKind::AmpersandAmpersandToken:
    return 5;
  case TokenKind::PipeToken:
    return 6;
  case TokenKind::CaretToken:
    return 7;
  case TokenKind::AmpersandToken:
    return 8;
  case TokenKind::EqualsEqualsEqualsToken:
  case TokenKind::ExclamationEqualsEqualsToken:
  case TokenKind::EqualsEqualsToken:
  case TokenKind::ExclamationEqualsToken:
    return 9;
  case TokenKind::LessThanToken:
  case TokenKind::LessThanEqualsToken:
  case TokenKind::GreaterThanToken:
  case TokenKind::GreaterThanEqualsToken:
  case TokenKind::InstanceOfKeyword:
    return 10;
  case TokenKind::InKeyword:
    return disallowIn ? 0 : 10;
  case TokenKind::LessThanLessThanToken:
  case TokenKind::GreaterThanGreaterThanToken:
  case TokenKind::GreaterThanGreaterThanGreaterThanToken:
    return 11;
  case TokenKind::PlusToken:
  case TokenKind::MinusToken:
    return 12;
  case TokenKind::AsteriskToken:
  case TokenKind::SlashToken:
  case TokenKind::PercentToken:
    return 13;
  case TokenKind::AsteriskAsteriskToken:
    return 14;
  default:
    return 0;
  }
}

/** Keyword kinds that may always stand where an identifier is wanted. */
bool isAlwaysIdentifier(TokenKind kind)
{
  switch (kind) {
  case TokenKind::AsyncKeyword:
  case TokenKind::AwaitKeyword:
  case TokenKind::AnyKeyword:
  case TokenKind::AssertsKeyword:
  case TokenKind::AssertKeyword:
  case TokenKind::BooleanKeyword:
  case TokenKind::ConstructorKeyword:
  case TokenKind::DeclareKeyword:
  case TokenKind::GetKeyword:
  case TokenKind::GlobalKeyword:
  case TokenKind::ImplementsKeyword:
  case TokenKind::InferKeyword:
  case TokenKind::InterfaceKeyword:
  case TokenKind::IntrinsicKeyword:
  case TokenKind::IsKeyword:
  case TokenKind::KeyOfKeyword:
  case TokenKind::ModuleKeyword:
  case TokenKind::NamespaceKeyword:
  case TokenKind::NeverKeyword:
  case TokenKind::NumberKeyword:
  case TokenKind::ObjectKeyword:
  case TokenKind::OfKeyword:
  case TokenKind::OutKeyword:
  case TokenKind::OverrideKeyword:
  case TokenKind::ReadOnlyKeyword:
  case TokenKind::RequireKeyword:
  case TokenKind::SetKeyword:
  case TokenKind::StaticKeyword:
  case TokenKind::StringKeyword:
  case TokenKind::SymbolKeyword:
  case TokenKind::TypeKeyword:
  case TokenKind::UndefinedKeyword:
  case TokenKind::UniqueKeyword:
  case TokenKind::UnknownKeyword:
  case TokenKind::Identifier:
    return true;
  default:
    return false;
  }
}

} // namespace

bool Parser::isWord() const
{
  return isAlwaysIdentifier(kind());
}

bool Parser::isIdentifierKind(TokenKind k) const
{
  return isAlwaysIdentifier(k);
}

bool Parser::eat(TokenKind k)
{
  if (!is(k)) {
    return false;
  }
  m_scanner.scanOne();
  return true;
}

uint32_t Parser::expect(TokenKind k)
{
  if (is(k)) {
    uint32_t index = pos();
    m_scanner.scanOne();
    return index;
  }
  errorAt(token(), 1005, string("';', ')', ']' or an operator was expected."));
  return kNoToken;
}

uint32_t Parser::expectSemicolon()
{
  if (eat(TokenKind::SemicolonToken)) {
    return pos() - 1;
  }
  // ASI: end of file, `}` or a line break before the next token.
  if (is(TokenKind::EndOfFile) || is(TokenKind::CloseBraceToken) || hasPrecedingLineBreak()) {
    return kNoToken;
  }
  errorAt(token(), 1005, string("';' expected."));
  return kNoToken;
}

void Parser::error(uint32_t code, uint32_t offset, uint32_t length, string message)
{
  m_diagnostics.report(code, offset, length, std::move(message));
}

void Parser::errorAt(const Token &t, uint32_t code, string message)
{
  error(code, t.offset, t.length ? t.length : 1, std::move(message));
}

Parser::Mark Parser::begin()
{
  return {m_scanner.state(), m_tree->buildState(), m_diagnostics.size()};
}

void Parser::commit(const Mark &m)
{
  (void)m;
}

void Parser::rollback(const Mark &m)
{
  m_scanner.rewind(m.scanner);
  m_tree->restoreBuild(m.build);
}

TokenKind Parser::peekKind()
{
  Mark mark = begin();
  m_scanner.scanOne();
  TokenKind result = kind();
  rollback(mark);
  return result;
}

/** Kind two tokens ahead. */
TokenKind Parser::peekKind2()
{
  Mark mark = begin();
  m_scanner.scanOne();
  m_scanner.scanOne();
  TokenKind result = kind();
  rollback(mark);
  return result;
}

Parser::Parser(std::string_view source, const Options &options, Diagnostics &diagnostics)
    : m_diagnostics(diagnostics), m_scanner(source, Scanner::Options{options.javaScript}, diagnostics),
      m_options(options)
{
}

void Parser::parseFile(GrammarTree &tree)
{
  m_tree = &tree;
  m_allowAwait = true;
  tree.setSourceForBuild(m_scanner.source());

  uint32_t firstToken = pos();
  m_scanner.scanOne();
  NodeId root = tree.beginNode(NodeKind::SourceFile, firstToken);

  while (!is(TokenKind::EndOfFile)) {
    NodeId statement = parseStatement();
    if (statement != kNoNode) {
      tree.addChild(statement);
    }
  }

  tree.endNode(root, m_scanner.tokenIndex() + 1);
  tree.setRoot(root);

  // Copy the final arrays; speculation may have made the scanner rewind.
  tree.tokenArray().clear();
  for (const Token &t : m_scanner.tokens()) {
    tree.tokenArray().append(t);
  }
  tree.triviaArray().clear();
  for (const Trivia &t : m_scanner.trivia()) {
    tree.triviaArray().append(t);
  }
  tree.lineStartsForBuild().clear();
  for (uint32_t offset : m_scanner.lineStarts()) {
    tree.lineStartsForBuild().append(offset);
  }
}

// ------------------------------------------------------------------ lookahead

/** True when the current word token begins a `type` alias (contextual check). */
bool Parser::startsTypeAlias()
{
  return kind() == TokenKind::TypeKeyword && !hasPrecedingLineBreak() &&
         isAlwaysIdentifier(peekKind()) && peekKind2() == TokenKind::EqualsToken;
}

/** True when the current contextual keyword is followed by a name. */
bool Parser::wordFollows()
{
  return !hasPrecedingLineBreak() && isAlwaysIdentifier(peekKind());
}

bool Parser::letStartsDeclaration()
{
  TokenKind next = peekKind();
  return isAlwaysIdentifier(next) || next == TokenKind::OpenBracketToken ||
         next == TokenKind::OpenBraceToken;
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
    uint32_t semi = expectSemicolon();
    NodeId node = m_tree->beginNode(NodeKind::DebuggerStatement, firstToken);
    return m_tree->endNode(node, semi == kNoToken ? pos() : semi + 1);
  }
  case TokenKind::AtToken:
    return parseClassDeclaration(false, false);
  case TokenKind::ImportKeyword:
    return parseImportDeclaration(false);
  case TokenKind::ExportKeyword:
    return parseExportDeclaration(false);
  default:
    break;
  }

  TokenKind k = kind();
  if (k == TokenKind::AsyncKeyword && !hasPrecedingLineBreak() &&
      peekKind() == TokenKind::FunctionKeyword) {
    m_scanner.scanOne();
    return parseFunctionDeclaration(true, false);
  }
  if (k == TokenKind::AbstractKeyword && peekKind() == TokenKind::ClassKeyword &&
      !hasPrecedingLineBreak()) {
    m_scanner.scanOne();
    return parseClassDeclaration(false, true);
  }
  if (k == TokenKind::DeclareKeyword && !hasPrecedingLineBreak()) {
    TokenKind next = peekKind();
    if (next == TokenKind::FunctionKeyword) {
      m_scanner.scanOne();
      return parseFunctionDeclaration(false, true);
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
        if (kind() == TokenKind::AbstractKeyword) {
          m_scanner.scanOne();
        }
        return parseClassDeclaration(true, false);
      }
      // `declare` as a plain identifier.
      return parseExpressionOrLabeledStatement();
    }
    if (next == TokenKind::VarKeyword || next == TokenKind::LetKeyword ||
        next == TokenKind::ConstKeyword) {
      m_scanner.scanOne();
      return parseVariableStatement(true);
    }
    if (next == TokenKind::ModuleKeyword || next == TokenKind::NamespaceKeyword ||
        next == TokenKind::GlobalKeyword) {
      m_scanner.scanOne();
      return parseModuleDeclaration(true);
    }
  }
  if (k == TokenKind::ConstKeyword && peekKind() == TokenKind::EnumKeyword) {
    m_scanner.scanOne();
    return parseEnumDeclaration(false);
  }
  if (k == TokenKind::TypeKeyword && startsTypeAlias()) {
    return parseTypeAlias(false);
  }
  if (k == TokenKind::InterfaceKeyword && wordFollows() && !hasPrecedingLineBreak()) {
    return parseInterfaceDeclaration(false);
  }
  if (k == TokenKind::EnumKeyword && wordFollows() && !hasPrecedingLineBreak()) {
    return parseEnumDeclaration(false);
  }
  if ((k == TokenKind::ModuleKeyword || k == TokenKind::NamespaceKeyword) && wordFollows() &&
      !hasPrecedingLineBreak()) {
    return parseModuleDeclaration(false);
  }
  if (k == TokenKind::ConstKeyword || k == TokenKind::VarKeyword ||
      (k == TokenKind::LetKeyword && letStartsDeclaration())) {
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
      kind() != TokenKind::AsyncKeyword && peekKind() == TokenKind::ColonToken) {
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
  uint32_t semi = expectSemicolon();
  NodeId node = m_tree->beginNode(NodeKind::ExpressionStatement, firstToken);
  m_tree->addChild(expression);
  return m_tree->endNode(node, semi == kNoToken ? pos() : semi + 1);
}

NodeId Parser::parseBlock()
{
  uint32_t firstToken = pos();
  (void)expect(TokenKind::OpenBraceToken);
  NodeId block = m_tree->beginNode(NodeKind::Block, firstToken);
  while (!is(TokenKind::CloseBraceToken) && !is(TokenKind::EndOfFile)) {
    NodeId statement = parseStatement();
    if (statement != kNoNode) {
      m_tree->addChild(statement);
    }
  }
  uint32_t close = expect(TokenKind::CloseBraceToken);
  return m_tree->endNode(block, close == kNoToken ? pos() : close + 1);
}

NodeId Parser::parseVariableStatement(bool declareFlag)
{
  uint32_t firstToken = pos();
  NodeId list = parseVariableDeclarationList(kind());
  NodeId node = m_tree->beginNode(NodeKind::VariableStatement, firstToken);
  if (declareFlag) {
    m_tree->node(node).flags |= FLAG_AMBIENT;
  }
  m_tree->addChild(list);
  uint32_t semi = expectSemicolon();
  return m_tree->endNode(node, semi == kNoToken ? pos() : semi + 1);
}

NodeId Parser::parseVariableDeclarationList(TokenKind keyword)
{
  uint32_t firstToken = pos();
  (void)expect(keyword);
  NodeId list = m_tree->beginNode(NodeKind::VariableDeclarationList, firstToken);
  if (keyword == TokenKind::ConstKeyword) {
    m_tree->node(list).flags |= FLAG_CONST;
  }
  do {
    m_tree->addChild(parseVariableDeclaration(false));
  } while (eat(TokenKind::CommaToken));
  return m_tree->endNode(list, pos());
}

NodeId Parser::parseVariableDeclaration(bool disallowIn)
{
  (void)disallowIn;
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
  uint32_t close = expect(TokenKind::CloseParenToken);
  expectSemicolon(); // optional per ASI rules on `do … while`
  return m_tree->endNode(node, close == kNoToken ? pos() : close + 1);
}

NodeId Parser::parseForStatement()
{
  uint32_t firstToken = pos();
  m_scanner.scanOne();
  (void)expect(TokenKind::OpenParenToken);

  NodeId initializer = kNoNode;
  if (!is(TokenKind::SemicolonToken) && !is(TokenKind::OfKeyword) && !is(TokenKind::InKeyword)) {
    if (kind() == TokenKind::ConstKeyword || kind() == TokenKind::VarKeyword ||
        (kind() == TokenKind::LetKeyword && letStartsDeclaration())) {
      initializer = parseVariableDeclarationList(kind());
    }
    else if ((kind() == TokenKind::AwaitKeyword &&
              peekKind() == TokenKind::UsingKeyword) ||
             (kind() == TokenKind::UsingKeyword && peekKind() == TokenKind::Identifier)) {
      uint32_t listFirst = pos();
      bool awaitFlag = eat(TokenKind::AwaitKeyword);
      (void)awaitFlag;
      initializer = parseVariableDeclarationList(kind());
      m_tree->node(initializer).flags |= FLAG_AMBIENT;
      (void)listFirst;
    }
    else {
      initializer = parseExpression(true); // `in` is not allowed in for-init
    }
  }

  if (is(TokenKind::OfKeyword) || is(TokenKind::InKeyword)) {
    NodeKind loopKind =
        is(TokenKind::OfKeyword) ? NodeKind::ForOfStatement : NodeKind::ForInStatement;
    m_scanner.scanOne();
    NodeId loop = m_tree->beginNode(loopKind, firstToken);
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
  }
  else {
    m_tree->addChild(missingNode(NodeKind::OmittedExpression, pos()));
  }
  (void)expect(TokenKind::SemicolonToken);
  if (!is(TokenKind::SemicolonToken)) {
    m_tree->addChild(parseExpression());
  }
  else {
    m_tree->addChild(missingNode(NodeKind::OmittedExpression, pos()));
  }
  (void)expect(TokenKind::SemicolonToken);
  if (!is(TokenKind::CloseParenToken)) {
    m_tree->addChild(parseExpression());
  }
  else {
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
  uint32_t semi = expectSemicolon();
  return m_tree->endNode(node, semi == kNoToken ? pos() : semi + 1);
}

NodeId Parser::parseReturnStatement()
{
  uint32_t firstToken = pos();
  m_scanner.scanOne();
  NodeId node = m_tree->beginNode(NodeKind::ReturnStatement, firstToken);
  // Restricted production: the expression must start on the same line.
  if (!hasPrecedingLineBreak() && !is(TokenKind::SemicolonToken) && !is(TokenKind::CloseBraceToken) &&
      !is(TokenKind::EndOfFile)) {
    m_tree->addChild(parseExpression());
  }
  uint32_t semi = expectSemicolon();
  return m_tree->endNode(node, semi == kNoToken ? pos() : semi + 1);
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
      !is(TokenKind::EndOfFile)) {
    m_tree->addChild(parseExpression());
  }
  uint32_t semi = expectSemicolon();
  return m_tree->endNode(node, semi == kNoToken ? pos() : semi + 1);
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
      m_tree->addChild(parseBindingPattern());
      if (eat(TokenKind::ColonToken)) {
        m_tree->addChild(parseType());
      }
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
  while (!is(TokenKind::CloseBraceToken) && !is(TokenKind::EndOfFile)) {
    NodeKind clauseKind =
        is(TokenKind::CaseKeyword) ? NodeKind::CaseClause : NodeKind::DefaultClause;
    uint32_t clauseFirst = pos();
    m_scanner.scanOne();
    NodeId clause = m_tree->beginNode(clauseKind, clauseFirst);
    if (clauseKind == NodeKind::CaseClause) {
      m_tree->addChild(parseExpression());
    }
    (void)expect(TokenKind::ColonToken);
    while (!is(TokenKind::CaseKeyword) && !is(TokenKind::DefaultKeyword) &&
           !is(TokenKind::CloseBraceToken) && !is(TokenKind::EndOfFile)) {
      m_tree->addChild(parseStatement());
    }
    m_tree->addChild(m_tree->endNode(clause, pos()));
  }
  m_inSwitch = saved;
  (void)expect(TokenKind::CloseBraceToken);
  return m_tree->endNode(node, pos());
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

// ------------------------------------------------- functions, classes, modules

NodeId Parser::parseFunctionDeclaration(bool asyncFlag, bool ambientFlag)
{
  uint32_t firstToken = pos() - (asyncFlag ? 1 : 0);
  NodeId node = m_tree->beginNode(NodeKind::FunctionDeclaration, firstToken);
  if (asyncFlag) {
    m_tree->node(node).flags |= FLAG_ASYNC;
  }
  if (ambientFlag) {
    m_tree->node(node).flags |= FLAG_AMBIENT;
  }
  parseFunctionCommon(node);
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseFunctionExpression(bool asyncFlag)
{
  uint32_t firstToken = pos() - (asyncFlag ? 1 : 0);
  NodeId node = m_tree->beginNode(NodeKind::FunctionExpression, firstToken);
  if (asyncFlag) {
    m_tree->node(node).flags |= FLAG_ASYNC;
  }
  parseFunctionCommon(node);
  return m_tree->endNode(node, pos());
}

/**
 * Everything from `function` to the end of the body (or to `;` for ambient).
 * The kind node was already opened by the caller.
 */
void Parser::parseFunctionCommon(NodeId node)
{
  (void)expect(TokenKind::FunctionKeyword);
  if (eat(TokenKind::AsteriskToken)) {
    m_tree->node(node).flags |= FLAG_GENERATOR;
  }
  if (isAlwaysIdentifier(kind())) {
    uint32_t nameIndex = pos();
    m_scanner.scanOne();
    NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
    m_tree->addChild(m_tree->endNode(name, nameIndex + 1));
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
  bool savedYield = m_inYieldContext;
  bool savedAwait = m_allowAwait;
  m_inYieldContext = false;
  m_allowAwait = false;
  if (is(TokenKind::OpenBraceToken)) {
    m_tree->addChild(parseBlock());
  }
  else {
    // Ambient declaration or an error-recovery `;`.
    uint32_t semi = expectSemicolon();
    m_tree->addChild(missingNode(NodeKind::Block, pos()));
    if (semi == kNoToken && !is(TokenKind::EndOfFile)) {
      errorAt(token(), 1136, string("Invalid character."));
    }
  }
  m_inYieldContext = savedYield;
  m_allowAwait = savedAwait;
}

NodeId Parser::parseBindingPattern()
{
  uint32_t firstToken = pos();
  switch (kind()) {
  case TokenKind::OpenBracketToken: {
    m_scanner.scanOne();
    NodeId pattern = m_tree->beginNode(NodeKind::ArrayBindingPattern, firstToken);
    while (!is(TokenKind::CloseBracketToken) && !is(TokenKind::EndOfFile)) {
      NodeId element = m_tree->beginNode(NodeKind::BindingElement, pos());
      if (eat(TokenKind::DotDotDotToken)) {
        m_tree->node(element).flags |= FLAG_REST;
      }
      if (is(TokenKind::CommaToken)) {
        // Hole in the pattern.
        m_tree->addChild(missingNode(NodeKind::OmittedExpression, pos()));
      }
      else {
        m_tree->addChild(parseBindingPattern());
      }
      if (eat(TokenKind::ColonToken)) {
        // Renaming inside an object pattern.
        m_tree->addChild(parseBindingPattern());
      }
      if (eat(TokenKind::EqualsToken)) {
        m_tree->addChild(parseAssignmentExpression());
      }
      m_tree->addChild(m_tree->endNode(element, pos()));
      if (!eat(TokenKind::CommaToken)) {
        break;
      }
    }
    (void)expect(TokenKind::CloseBracketToken);
    return m_tree->endNode(pattern, pos());
  }
  case TokenKind::OpenBraceToken: {
    m_scanner.scanOne();
    NodeId pattern = m_tree->beginNode(NodeKind::ObjectBindingPattern, firstToken);
    while (!is(TokenKind::CloseBraceToken) && !is(TokenKind::EndOfFile)) {
      NodeId element = m_tree->beginNode(NodeKind::BindingElement, pos());
      if (eat(TokenKind::DotDotDotToken)) {
        m_tree->node(element).flags |= FLAG_REST;
      }
      m_tree->addChild(parsePropertyName());
      if (eat(TokenKind::ColonToken)) {
        m_tree->addChild(parseBindingPattern());
      }
      if (eat(TokenKind::EqualsToken)) {
        m_tree->addChild(parseAssignmentExpression());
      }
      m_tree->addChild(m_tree->endNode(element, pos()));
      if (!eat(TokenKind::CommaToken)) {
        break;
      }
    }
    (void)expect(TokenKind::CloseBraceToken);
    return m_tree->endNode(pattern, pos());
  }
  default: {
    NodeId name = kNoNode;
    uint32_t nameFirst = pos();
    if (is(TokenKind::PrivateIdentifier)) {
      name = m_tree->beginNode(NodeKind::PrivateIdentifier, nameFirst);
    }
    else {
      name = m_tree->beginNode(NodeKind::Identifier, nameFirst);
    }
    if (isAlwaysIdentifier(kind()) || is(TokenKind::PrivateIdentifier)) {
      m_scanner.scanOne();
    }
    else {
      errorAt(token(), 1136, string("Declaration expected."));
    }
    return m_tree->endNode(name, pos());
  }
  }
}

NodeId Parser::parseParameterList(bool allowModifiers, bool isConstructor)
{
  // Parameters are appended straight to the enclosing node's children; there
  // is no wrapper node.
  while (!is(TokenKind::CloseParenToken) && !is(TokenKind::EndOfFile)) {
    m_tree->addChild(parseParameter(allowModifiers, isConstructor));
    if (!eat(TokenKind::CommaToken)) {
      break;
    }
  }
  (void)allowModifiers;
  (void)isConstructor;
  return kNoNode;
}

NodeId Parser::parseParameter(bool allowModifiers, bool isConstructor)
{
  uint32_t firstToken = pos();
  (void)allowModifiers;
  (void)isConstructor;
  NodeId parameter = m_tree->beginNode(NodeKind::Parameter, firstToken);
  // Modifiers on constructor parameters become properties; recorded as flags.
  for (;;) {
    if (is(TokenKind::PublicKeyword) || is(TokenKind::PrivateKeyword) ||
        is(TokenKind::ProtectedKeyword)) {
      m_tree->node(parameter).flags |= FLAG_READONLY; // placeholder; visibility has no flag yet
      m_scanner.scanOne();
      continue;
    }
    if (is(TokenKind::ReadOnlyKeyword) && peekKind() != TokenKind::CommaToken &&
        peekKind() != TokenKind::CloseParenToken && peekKind() != TokenKind::ColonToken) {
      m_tree->node(parameter).flags |= FLAG_READONLY;
      m_scanner.scanOne();
      continue;
    }
    break;
  }
  if (eat(TokenKind::DotDotDotToken)) {
    m_tree->node(parameter).flags |= FLAG_REST;
  }
  m_tree->addChild(parseBindingPattern());
  if (eat(TokenKind::QuestionToken)) {
    m_tree->node(parameter).flags |= FLAG_OPTIONAL;
  }
  if (eat(TokenKind::ColonToken)) {
    m_tree->addChild(parseTypeOrTypePredicate());
  }
  if (eat(TokenKind::EqualsToken)) {
    m_tree->addChild(parseAssignmentExpression());
  }
  return m_tree->endNode(parameter, pos());
}

NodeId Parser::parseClassDeclaration(bool declareFlag, bool abstractFlag)
{
  uint32_t firstToken = pos();
  NodeId node = m_tree->beginNode(NodeKind::ClassDeclaration, firstToken);
  if (declareFlag) {
    m_tree->node(node).flags |= FLAG_AMBIENT;
  }
  if (abstractFlag) {
    m_tree->node(node).flags |= FLAG_ABSTRACT;
  }
  parseClassLike(node);
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseClassExpression()
{
  NodeId node = m_tree->beginNode(NodeKind::ClassExpression, pos());
  parseClassLike(node);
  return m_tree->endNode(node, pos());
}

/**
 * From the first decorator (when called at `@`) or `class`, through the body.
 */
void Parser::parseClassLike(NodeId node)
{
  (void)node;
  while (is(TokenKind::AtToken)) {
    uint32_t firstToken = pos();
    m_scanner.scanOne();
    NodeId decorator = m_tree->beginNode(NodeKind::Decorator, firstToken);
    m_tree->addChild(parseCallChain(parsePrimary()));
    m_tree->addChild(m_tree->endNode(decorator, pos()));
  }
  (void)expect(TokenKind::ClassKeyword);
  if (isAlwaysIdentifier(kind())) {
    uint32_t nameIndex = pos();
    m_scanner.scanOne();
    NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
    m_tree->addChild(m_tree->endNode(name, nameIndex + 1));
  }
  if (is(TokenKind::LessThanToken)) {
    m_tree->addChild(parseTypeParameters());
  }
  while (is(TokenKind::ExtendsKeyword) || is(TokenKind::ImplementsKeyword)) {
    m_tree->addChild(parseHeritageClause(NodeKind::ClassDeclaration));
  }
  (void)expect(TokenKind::OpenBraceToken);
  while (!is(TokenKind::CloseBraceToken) && !is(TokenKind::EndOfFile)) {
    if (eat(TokenKind::SemicolonToken)) {
      continue;
    }
    m_tree->addChild(parseClassMember());
  }
  (void)expect(TokenKind::CloseBraceToken);
}

NodeId Parser::parseHeritageClause(NodeKind kind)
{
  TokenKind keyword = this->kind();
  uint32_t firstToken = pos();
  m_scanner.scanOne();
  NodeId clause = m_tree->beginNode(NodeKind::HeritageClause, firstToken);
  do {
    NodeId type = m_tree->beginNode(NodeKind::ExpressionWithTypeArguments, pos());
    if (keyword == TokenKind::ExtendsKeyword) {
      m_tree->addChild(parseCallChain(parsePrimary()));
    }
    else {
      m_tree->addChild(parseTypeReference());
    }
    m_tree->addChild(m_tree->endNode(type, pos()));
  } while (eat(TokenKind::CommaToken));
  (void)kind;
  return m_tree->endNode(clause, pos());
}

// ------------------------------------------------------------- class members

NodeId Parser::parseClassMember()
{
  uint32_t firstToken = pos();
  uint16_t flags = FLAG_NONE;

  bool isConstructor = false;
  bool isGet = false;

  // Modifier scan: static/readonly/abstract/override/declare/accessor, then
  // a possible `*` generator star, then the member name.
  for (;;) {
    if (is(TokenKind::StaticKeyword) && peekKind() != TokenKind::OpenParenToken &&
        peekKind() != TokenKind::EqualsToken && peekKind() != TokenKind::SemicolonToken &&
        peekKind() != TokenKind::ColonToken && peekKind() != TokenKind::QuestionToken) {
      flags |= FLAG_STATIC;
      m_scanner.scanOne();
      continue;
    }
    if (is(TokenKind::ReadOnlyKeyword) && wordFollows()) {
      flags |= FLAG_READONLY;
      m_scanner.scanOne();
      continue;
    }
    if (is(TokenKind::AbstractKeyword) && wordFollows()) {
      flags |= FLAG_ABSTRACT;
      m_scanner.scanOne();
      continue;
    }
    if (is(TokenKind::OverrideKeyword) && wordFollows()) {
      flags |= FLAG_OVERRIDE;
      m_scanner.scanOne();
      continue;
    }
    if (is(TokenKind::DeclareKeyword) && wordFollows()) {
      flags |= FLAG_AMBIENT;
      m_scanner.scanOne();
      continue;
    }
    if (is(TokenKind::AccessorKeyword) && wordFollows()) {
      flags |= FLAG_ACCESSOR;
      m_scanner.scanOne();
      continue;
    }
    break;
  }

  if (eat(TokenKind::AsteriskToken)) {
    flags |= FLAG_GENERATOR;
  }

  // Index signatures, call and construct signatures.
  if (is(TokenKind::OpenBracketToken) && peekKind2() == TokenKind::ColonToken) {
    m_scanner.scanOne();
    NodeId member = m_tree->beginNode(NodeKind::IndexSignature, firstToken);
    m_tree->addChild(parseBindingPattern());
    (void)expect(TokenKind::CloseBracketToken);
    if (eat(TokenKind::ColonToken)) {
      m_tree->addChild(parseType());
    }
    eat(TokenKind::SemicolonToken);
    return m_tree->endNode(member, pos());
  }
  if (is(TokenKind::OpenParenToken)) {
    NodeId member = m_tree->beginNode(NodeKind::CallSignature, firstToken);
    if (is(TokenKind::LessThanToken)) {
      m_tree->addChild(parseTypeParameters());
    }
    (void)expect(TokenKind::OpenParenToken);
    parseParameterList(false, false);
    (void)expect(TokenKind::CloseParenToken);
    if (eat(TokenKind::ColonToken)) {
      m_tree->addChild(parseTypeOrTypePredicate());
    }
    eat(TokenKind::SemicolonToken);
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
    eat(TokenKind::SemicolonToken);
    return m_tree->endNode(member, pos());
  }

  bool isSet = false;
  if (is(TokenKind::ConstructorKeyword)) {
    isConstructor = true;
  }
  else if ((is(TokenKind::GetKeyword) || is(TokenKind::SetKeyword)) && wordFollows()) {
    isGet = is(TokenKind::GetKeyword);
    isSet = !isGet;
  }

  NodeKind memberKind = isConstructor ? NodeKind::ConstructorNode
                        : isGet       ? NodeKind::GetAccessor
                        : isSet       ? NodeKind::SetAccessor
                                      : NodeKind::PropertyDeclaration;
  if (hasFlags(flags, FLAG_GENERATOR) && memberKind == NodeKind::PropertyDeclaration) {
    memberKind = NodeKind::MethodDeclaration;
  }

  NodeId member = m_tree->beginNode(memberKind, firstToken);
  m_tree->node(member).flags |= flags;

  // The name (identifier, keyword, string or computed). Accessor keywords
  // were already recognized; consume one before the real name.
  if (isGet || isSet) {
    m_scanner.scanOne();
  }
  if (is(TokenKind::OpenBracketToken)) {
    m_tree->addChild(parsePropertyName());
  }
  else {
    NodeId name = m_tree->beginNode(
        is(TokenKind::StringLiteral)       ? NodeKind::StringLiteral
        : is(TokenKind::NumericLiteral)    ? NodeKind::NumericLiteral
        : is(TokenKind::PrivateIdentifier) ? NodeKind::PrivateIdentifier
                                           : NodeKind::Identifier,
        pos());
    if (is(TokenKind::StringLiteral) || is(TokenKind::NumericLiteral) ||
        is(TokenKind::PrivateIdentifier) || isAlwaysIdentifier(kind())) {
      m_scanner.scanOne();
    }
    else {
      errorAt(token(), 1174, string("Property name expected."));
    }
    m_tree->addChild(m_tree->endNode(name, pos()));
  }

  bool methodForm = isConstructor || isGet || isSet ||
                    memberKind == NodeKind::MethodDeclaration || is(TokenKind::OpenParenToken) ||
                    is(TokenKind::LessThanToken);
  if (methodForm && memberKind == NodeKind::PropertyDeclaration) {
    m_tree->node(member).kind = NodeKind::MethodDeclaration;
  }

  if (methodForm) {
    if (eat(TokenKind::QuestionToken)) {
      m_tree->node(member).flags |= FLAG_OPTIONAL;
    }
    if (is(TokenKind::LessThanToken)) {
      m_tree->addChild(parseTypeParameters());
    }
    (void)expect(TokenKind::OpenParenToken);
    parseParameterList(isConstructor, isConstructor);
    (void)expect(TokenKind::CloseParenToken);
    if (eat(TokenKind::ColonToken)) {
      m_tree->addChild(parseTypeOrTypePredicate());
    }
    if (is(TokenKind::OpenBraceToken)) {
      bool savedYield = m_inYieldContext;
      bool savedAwait = m_allowAwait;
      m_inYieldContext = hasFlags(flags, FLAG_GENERATOR);
      m_allowAwait = true;
      m_tree->addChild(parseBlock());
      m_inYieldContext = savedYield;
      m_allowAwait = savedAwait;
    }
    else {
      m_tree->addChild(missingNode(NodeKind::Block, pos()));
    }
  }
  else {
    // Property: optional `?`/`!`, optional type, optional initializer.
    if (eat(TokenKind::QuestionToken)) {
      m_tree->node(member).flags |= FLAG_OPTIONAL;
    }
    if (eat(TokenKind::ExclamationToken)) {
      // definite assignment
    }
    if (eat(TokenKind::ColonToken)) {
      m_tree->addChild(parseType());
    }
    if (eat(TokenKind::EqualsToken)) {
      m_tree->addChild(parseAssignmentExpression());
    }
    expectSemicolon();
  }
  return m_tree->endNode(member, pos());
}

// -------------------------------------------------------------------- modules

NodeId Parser::parseImportDeclaration(bool ambientFlag)
{
  uint32_t firstToken = pos();
  m_scanner.scanOne(); // `import`

  // `import x = require("…")` / `import x = NS.Name`
  if (isAlwaysIdentifier(kind()) && peekKind() == TokenKind::EqualsToken) {
    NodeId node = m_tree->beginNode(NodeKind::ImportEqualsDeclaration, firstToken);
    if (ambientFlag) {
      m_tree->node(node).flags |= FLAG_AMBIENT;
    }
    uint32_t nameIndex = pos();
    m_scanner.scanOne();
    NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
    m_tree->addChild(m_tree->endNode(name, nameIndex + 1));
    (void)expect(TokenKind::EqualsToken);
    if (eat(TokenKind::RequireKeyword)) {
      NodeId ref = m_tree->beginNode(NodeKind::ExternalModuleReference, pos() - 1);
      (void)expect(TokenKind::OpenParenToken);
      if (is(TokenKind::StringLiteral)) {
        uint32_t index = pos();
        NodeId name = m_tree->beginNode(NodeKind::StringLiteral, index);
        m_scanner.scanOne();
        m_tree->addChild(m_tree->endNode(name, index + 1));
      }
      (void)expect(TokenKind::CloseParenToken);
      m_tree->addChild(m_tree->endNode(ref, pos()));
    }
    else {
      // Namespace import type: `A.B.C`.
      NodeId type = parseTypeReference();
      m_tree->addChild(type);
    }
    uint32_t semi = expectSemicolon();
    return m_tree->endNode(node, semi == kNoToken ? pos() : semi + 1);
  }

  NodeId node = m_tree->beginNode(NodeKind::ImportDeclaration, firstToken);
  if (ambientFlag) {
    m_tree->node(node).flags |= FLAG_AMBIENT;
  }
  if (eat(TokenKind::TypeKeyword)) {
    // `import type …`
  }
  if (is(TokenKind::StringLiteral)) {
    // Side-effect import: `import "mod"`.
    uint32_t index = pos();
    NodeId module = m_tree->beginNode(NodeKind::StringLiteral, index);
    m_scanner.scanOne();
    m_tree->addChild(m_tree->endNode(module, index + 1));
  }
  else {
    NodeId clause = m_tree->beginNode(NodeKind::ImportClause, pos());
    if (isAlwaysIdentifier(kind())) {
      uint32_t nameIndex = pos();
      m_scanner.scanOne();
      NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
      m_tree->addChild(m_tree->endNode(name, nameIndex + 1));
    }
    if (eat(TokenKind::CommaToken)) {
      m_tree->addChild(parseImportOrExportClause(true));
    }
    else if (is(TokenKind::AsteriskToken)) {
      m_tree->addChild(parseNamespaceImportOrExport(true));
    }
    m_tree->addChild(m_tree->endNode(clause, pos()));
    (void)expect(TokenKind::FromKeyword);
    if (is(TokenKind::StringLiteral)) {
      uint32_t index = pos();
      NodeId module = m_tree->beginNode(NodeKind::StringLiteral, index);
      m_scanner.scanOne();
      m_tree->addChild(m_tree->endNode(module, index + 1));
    }
  }
  if (eat(TokenKind::WithKeyword)) {
    m_tree->addChild(parseImportAttributes());
  }
  else if (eat(TokenKind::AssertKeyword)) {
    m_tree->addChild(parseImportAttributes());
  }
  uint32_t semi = expectSemicolon();
  return m_tree->endNode(node, semi == kNoToken ? pos() : semi + 1);
}

NodeId Parser::parseImportAttributes()
{
  uint32_t firstToken = pos();
  (void)expect(TokenKind::OpenBraceToken);
  NodeId attributes = m_tree->beginNode(NodeKind::ImportAttributes, firstToken);
  while (!is(TokenKind::CloseBraceToken) && !is(TokenKind::EndOfFile)) {
    m_scanner.scanOne();
    if (!eat(TokenKind::CommaToken)) {
      break;
    }
  }
  (void)expect(TokenKind::CloseBraceToken);
  return m_tree->endNode(attributes, pos());
}

NodeId Parser::parseExportDeclaration(bool ambientFlag)
{
  uint32_t firstToken = pos();
  m_scanner.scanOne(); // `export`
  NodeId node = m_tree->beginNode(NodeKind::ExportDeclaration, firstToken);
  (void)ambientFlag;

  if (eat(TokenKind::EqualsToken)) {
    // `export = expr`
    m_tree->addChild(parseAssignmentExpression());
    uint32_t semi = expectSemicolon();
    return m_tree->endNode(node, semi == kNoToken ? pos() : semi + 1);
  }
  if (eat(TokenKind::DefaultKeyword)) {
    m_tree->node(node).flags |= FLAG_DEFAULT | FLAG_EXPORTED;
    if (kind() == TokenKind::FunctionKeyword || kind() == TokenKind::AsyncKeyword) {
      bool asyncFlag = eat(TokenKind::AsyncKeyword);
      m_tree->addChild(parseFunctionDeclaration(asyncFlag, false));
    }
    else if (kind() == TokenKind::ClassKeyword || kind() == TokenKind::AtToken ||
             (kind() == TokenKind::AbstractKeyword && peekKind() == TokenKind::ClassKeyword)) {
      m_tree->addChild(parseClassDeclaration(false, false));
    }
    else {
      m_tree->addChild(parseAssignmentExpression());
    }
    uint32_t semi = expectSemicolon();
    return m_tree->endNode(node, semi == kNoToken ? pos() : semi + 1);
  }
  m_tree->node(node).flags |= FLAG_EXPORTED;

  if (eat(TokenKind::TypeKeyword)) {
    // `export type …`
  }
  if (is(TokenKind::AsteriskToken)) {
    m_tree->addChild(parseNamespaceImportOrExport(false));
  }
  else if (is(TokenKind::OpenBraceToken)) {
    m_tree->addChild(parseImportOrExportClause(false));
  }
  else {
    // A declaration: `export const …`, `export function …`, etc.
    m_tree->addChild(parseStatement());
  }

  bool fromClause = false;
  if (is(TokenKind::FromKeyword)) {
    m_scanner.scanOne();
    fromClause = true;
    if (is(TokenKind::StringLiteral)) {
      uint32_t index = pos();
      NodeId module = m_tree->beginNode(NodeKind::StringLiteral, index);
      m_scanner.scanOne();
      m_tree->addChild(m_tree->endNode(module, index + 1));
    }
  }
  (void)fromClause;
  uint32_t semi = expectSemicolon();
  return m_tree->endNode(node, semi == kNoToken ? pos() : semi + 1);
}

/** `{ a as b, "c" }` import or export list; the brace was not consumed. */
NodeId Parser::parseImportOrExportClause(bool isImport)
{
  uint32_t firstToken = pos();
  NodeId list = m_tree->beginNode(isImport ? NodeKind::NamedImports : NodeKind::NamedExports,
                                  firstToken);
  (void)expect(TokenKind::OpenBraceToken);
  while (!is(TokenKind::CloseBraceToken) && !is(TokenKind::EndOfFile)) {
    uint32_t specFirst = pos();
    if (eat(TokenKind::TypeKeyword) && peekKind() == TokenKind::CommaToken) {
      // `type` in `import { type A }` — treat as the specifier name below.
    }
    NodeId specifier = m_tree->beginNode(isImport ? NodeKind::ImportSpecifier
                                                   : NodeKind::ExportSpecifier,
                                         specFirst);
    if (isAlwaysIdentifier(kind()) || is(TokenKind::StringLiteral)) {
      uint32_t nameIndex = pos();
      NodeKind nameKind =
          is(TokenKind::StringLiteral) ? NodeKind::StringLiteral : NodeKind::Identifier;
      NodeId name = m_tree->beginNode(nameKind, nameIndex);
      m_scanner.scanOne();
      m_tree->addChild(m_tree->endNode(name, nameIndex + 1));
    }
    if (eat(TokenKind::AsKeyword)) {
      if (isAlwaysIdentifier(kind()) || is(TokenKind::StringLiteral)) {
        m_scanner.scanOne();
      }
    }
    m_tree->addChild(m_tree->endNode(specifier, pos()));
    if (!eat(TokenKind::CommaToken)) {
      break;
    }
  }
  (void)expect(TokenKind::CloseBraceToken);
  return m_tree->endNode(list, pos());
}

NodeId Parser::parseNamespaceImportOrExport(bool isImport)
{
  uint32_t firstToken = pos();
  (void)expect(TokenKind::AsteriskToken);
  (void)expect(TokenKind::AsKeyword);
  NodeId node = m_tree->beginNode(isImport ? NodeKind::NamespaceImport : NodeKind::NamespaceExport,
                                  firstToken);
  if (isAlwaysIdentifier(kind())) {
    m_scanner.scanOne();
  }
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseModuleDeclaration(bool ambientFlag)
{
  uint32_t firstToken = pos();
  if (eat(TokenKind::ModuleKeyword)) {
    if (is(TokenKind::StringLiteral)) {
      // `module "…" { … }` — an ambient module.
    }
  }
  else {
    (void)eat(TokenKind::NamespaceKeyword);
  }
  NodeId node = m_tree->beginNode(NodeKind::ModuleDeclaration, firstToken);
  if (ambientFlag) {
    m_tree->node(node).flags |= FLAG_AMBIENT;
  }
  if (is(TokenKind::StringLiteral)) {
    uint32_t index = pos();
    NodeId name = m_tree->beginNode(NodeKind::StringLiteral, index);
    m_scanner.scanOne();
    m_tree->addChild(m_tree->endNode(name, index + 1));
  }
  else {
    uint32_t nameIndex = pos();
    NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
    m_scanner.scanOne();
    m_tree->addChild(m_tree->endNode(name, nameIndex + 1));
    // dotted name: `namespace A.B { … }`
    while (eat(TokenKind::DotToken)) {
      uint32_t next = pos();
      NodeId part = m_tree->beginNode(NodeKind::Identifier, next);
      m_scanner.scanOne();
      m_tree->addChild(m_tree->endNode(part, next + 1));
    }
  }
  if (eat(TokenKind::OpenBraceToken)) {
    NodeId block = m_tree->beginNode(NodeKind::Block, pos() - 1);
    while (!is(TokenKind::CloseBraceToken) && !is(TokenKind::EndOfFile)) {
      m_tree->addChild(parseStatement());
    }
    (void)expect(TokenKind::CloseBraceToken);
    m_tree->addChild(m_tree->endNode(block, pos()));
  }
  else {
    (void)expectSemicolon(); // `declare module A;`
  }
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseTypeAlias(bool ambientFlag)
{
  uint32_t firstToken = pos();
  m_scanner.scanOne(); // `type`
  NodeId node = m_tree->beginNode(NodeKind::TypeAliasDeclaration, firstToken);
  if (ambientFlag) {
    m_tree->node(node).flags |= FLAG_AMBIENT;
  }
  uint32_t nameIndex = pos();
  NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
  m_scanner.scanOne();
  m_tree->addChild(m_tree->endNode(name, nameIndex + 1));
  if (is(TokenKind::LessThanToken)) {
    m_tree->addChild(parseTypeParameters());
  }
  (void)expect(TokenKind::EqualsToken);
  m_tree->addChild(parseType());
  uint32_t semi = expectSemicolon();
  return m_tree->endNode(node, semi == kNoToken ? pos() : semi + 1);
}

NodeId Parser::parseInterfaceDeclaration(bool ambientFlag)
{
  uint32_t firstToken = pos();
  m_scanner.scanOne(); // `interface`
  NodeId node = m_tree->beginNode(NodeKind::InterfaceDeclaration, firstToken);
  if (ambientFlag) {
    m_tree->node(node).flags |= FLAG_AMBIENT;
  }
  uint32_t nameIndex = pos();
  NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
  m_scanner.scanOne();
  m_tree->addChild(m_tree->endNode(name, nameIndex + 1));
  if (is(TokenKind::LessThanToken)) {
    m_tree->addChild(parseTypeParameters());
  }
  while (is(TokenKind::ExtendsKeyword)) {
    m_tree->addChild(parseHeritageClause(NodeKind::InterfaceDeclaration));
  }
  if (is(TokenKind::OpenBraceToken)) {
    m_tree->addChild(parseTypeLiteralBody());
  }
  else {
    (void)expect(TokenKind::OpenBraceToken);
    m_tree->addChild(missingNode(NodeKind::TypeLiteral, pos()));
  }
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseEnumDeclaration(bool ambientFlag)
{
  uint32_t firstToken = pos();
  if (is(TokenKind::ConstKeyword)) {
    m_scanner.scanOne();
  }
  (void)expect(TokenKind::EnumKeyword);
  NodeId node = m_tree->beginNode(NodeKind::EnumDeclaration, firstToken);
  if (ambientFlag) {
    m_tree->node(node).flags |= FLAG_AMBIENT;
  }
  uint32_t nameIndex = pos();
  NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
  m_scanner.scanOne();
  m_tree->addChild(m_tree->endNode(name, nameIndex + 1));
  (void)expect(TokenKind::OpenBraceToken);
  NodeId body = m_tree->beginNode(NodeKind::ObjectLiteralExpression, pos());
  while (!is(TokenKind::CloseBraceToken) && !is(TokenKind::EndOfFile)) {
    NodeId member = m_tree->beginNode(NodeKind::EnumMember, pos());
    m_tree->addChild(parsePropertyName());
    if (eat(TokenKind::EqualsToken)) {
      m_tree->addChild(parseAssignmentExpression());
    }
    m_tree->addChild(m_tree->endNode(member, pos()));
    if (!eat(TokenKind::CommaToken)) {
      break;
    }
  }
  (void)expect(TokenKind::CloseBraceToken);
  m_tree->addChild(m_tree->endNode(body, pos()));
  return m_tree->endNode(node, pos());
}

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
        !is(TokenKind::EndOfFile) && !is(TokenKind::ColonToken)) {
      m_tree->addChild(parseAssignmentExpression());
    }
    return m_tree->endNode(node, pos());
  }

  // Arrow function heads: `x =>`, `async x =>`, `async (…) =>`, `(…) =>`.
  if (is(TokenKind::AsyncKeyword) && !hasPrecedingLineBreak()) {
    TokenKind next = peekKind();
    if (next == TokenKind::FunctionKeyword) {
      m_scanner.scanOne();
      return parseFunctionExpression(true);
    }
    if (isAlwaysIdentifier(next) || next == TokenKind::OpenParenToken) {
      return parseArrowFunction(true);
    }
  }
  else if (isAlwaysIdentifier(kind()) && peekKind() == TokenKind::EqualsGreaterThanToken) {
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

/**
 * Speculative arrow parse of `(...)`: if the parens hold a valid parameter
 * list and `=>` follows, returns the built ArrowFunction; otherwise returns
 * kNoNode and the caller rolls back.
 */
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
  }
  else {
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
  }
  else {
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
  uint32_t firstToken = pos();
  NodeId left = parseUnary();
  for (;;) {
    int precedence = binaryPrecedence(kind(), false);
    if (precedence <= minPrecedence || precedence <= 2) { // assignment is handled higher
      return left;
    }
    if (kind() == TokenKind::InstanceOfKeyword) {
      m_scanner.scanOne();
      NodeId node = m_tree->beginNode(NodeKind::BinaryExpression, firstToken);
      m_tree->addChild(left);
      m_tree->addChild(parseTypeReference()); // right operand is a type
      left = m_tree->endNode(node, pos());
      continue;
    }
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::BinaryExpression, firstToken);
    m_tree->addChild(left);
    m_tree->addChild(parseBinary(precedence - 1)); // left-associative
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
    NodeKind expressionKind = kind() == TokenKind::DeleteKeyword ? NodeKind::DeleteExpression
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
        m_tree->addChild(parseType());
        (void)expect(TokenKind::GreaterThanToken);
        NodeId node = m_tree->beginNode(NodeKind::TypeAssertionExpression, firstToken);
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
         !hasPrecedingLineBreak()) {
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
  switch (kind()) {
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
    NodeKind literal = is(TokenKind::TrueKeyword) ? NodeKind::TrueLiteral : NodeKind::FalseLiteral;
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(literal, firstToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::NumericLiteral: {
    NodeKind literal = is(TokenKind::BigIntLiteral) ? NodeKind::BigIntLiteral
                                                    : NodeKind::NumericLiteral;
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(literal, firstToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::StringLiteral: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::StringLiteral, firstToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::RegularExpressionLiteral: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::RegularExpressionLiteral, firstToken);
    return m_tree->endNode(node, pos());
  }
  case TokenKind::BacktickToken:
  case TokenKind::TemplateHead:
  case TokenKind::NoSubstitutionTemplateLiteral:
    return parseTemplateLiteral(false);
  case TokenKind::OpenParenToken:
    return parseParenthesizedOrArrow();
  case TokenKind::OpenBracketToken: {
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::ArrayLiteralExpression, firstToken);
    while (!is(TokenKind::CloseBracketToken) && !is(TokenKind::EndOfFile)) {
      if (is(TokenKind::CommaToken)) {
        m_tree->addChild(missingNode(NodeKind::OmittedExpression, pos()));
      }
      else if (eat(TokenKind::DotDotDotToken)) {
        NodeId spread = m_tree->beginNode(NodeKind::SpreadElement, firstToken);
        m_tree->addChild(parseAssignmentExpression());
        m_tree->addChild(m_tree->endNode(spread, pos()));
      }
      else {
        m_tree->addChild(parseAssignmentExpression());
      }
      if (!eat(TokenKind::CommaToken)) {
        break;
      }
    }
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
    NodeId node = m_tree->beginNode(NodeKind::NewExpression, firstToken);
    m_tree->addChild(parseCallChain(parsePrimary()));
    if (is(TokenKind::OpenParenToken)) {
      m_tree->addChild(parseArguments());
    }
    // Trailing member accesses and calls belong to the new expression's
    // callee in TS's shape; we keep the outer chain outside for now.
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
  default: {
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
  }
}

NodeId Parser::parseParenthesizedOrArrow()
{
  // `(…)` — either a parenthesized expression/comma list or an arrow head.
  // Speculate on the arrow: parse parameters, then decide on `=>`.
  Mark mark = begin();
  uint32_t firstToken = pos();
  (void)expect(TokenKind::OpenParenToken);
  bool paramsOk = true;
  while (!is(TokenKind::CloseParenToken) && !is(TokenKind::EndOfFile)) {
    if (eat(TokenKind::DotDotDotToken)) {
      // rest parameter is a strong arrow signal but also valid in parens
    }
    if (!parseBindingPattern()) {
      paramsOk = false;
      break;
    }
    if (eat(TokenKind::QuestionToken)) {
      // optional parameter — only arrows have those bare
    }
    if (eat(TokenKind::ColonToken)) {
      parseTypeOrTypePredicate();
    }
    if (eat(TokenKind::EqualsToken)) {
      parseAssignmentExpression();
    }
    if (!eat(TokenKind::CommaToken)) {
      break;
    }
  }
  bool closed = eat(TokenKind::CloseParenToken);
  if (paramsOk && closed && is(TokenKind::EqualsGreaterThanToken)) {
    rollback(mark);
    return parseArrowFunction(false);
  }
  rollback(mark);

  NodeId node = m_tree->beginNode(NodeKind::ParenthesizedExpression, firstToken);
  (void)expect(TokenKind::OpenParenToken);
  m_tree->addChild(parseExpression());
  (void)expect(TokenKind::CloseParenToken);
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseArrayLiteral()
{
  return parsePrimary(); // arrays are handled in parsePrimary
}

NodeId Parser::parseObjectLiteral()
{
  uint32_t firstToken = pos();
  (void)expect(TokenKind::OpenBraceToken);
  NodeId node = m_tree->beginNode(NodeKind::ObjectLiteralExpression, firstToken);
  while (!is(TokenKind::CloseBraceToken) && !is(TokenKind::EndOfFile)) {
    m_tree->addChild(parseObjectMember());
    if (!eat(TokenKind::CommaToken)) {
      break;
    }
  }
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
  bool asyncFlag = is(TokenKind::AsyncKeyword) && !hasPrecedingLineBreak() &&
                   (peekKind() == TokenKind::Identifier || peekKind() == TokenKind::OpenBracketToken ||
                    isAlwaysIdentifier(peekKind()));
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
      NodeId member = m_tree->beginNode(isGet ? NodeKind::GetAccessor : NodeKind::SetAccessor,
                                        firstToken);
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
  if (asyncFlag) {
    m_scanner.scanOne();
    if (is(TokenKind::AsteriskToken)) {
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
      m_tree->addChild(parseBlock());
      return m_tree->endNode(member, pos());
    }
  }

  // Property, shorthand property, or method.
  Mark mark = begin();
  NodeId name = parsePropertyName();
  bool methodForm = is(TokenKind::OpenParenToken) || is(TokenKind::LessThanToken) ||
                    (asyncFlag && is(TokenKind::Identifier));
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
    // Computed property name: its expression is the child.
    m_scanner.scanOne();
    NodeId node = m_tree->beginNode(NodeKind::Identifier, firstToken);
    m_tree->addChild(parseAssignmentExpression());
    (void)expect(TokenKind::CloseBracketToken);
    return m_tree->endNode(node, pos());
  }
  NodeKind nameKind = is(TokenKind::StringLiteral)  ? NodeKind::StringLiteral
                      : is(TokenKind::NumericLiteral) ? NodeKind::NumericLiteral
                      : is(TokenKind::PrivateIdentifier) ? NodeKind::PrivateIdentifier
                                                        : NodeKind::Identifier;
  NodeId node = m_tree->beginNode(nameKind, firstToken);
  if (is(TokenKind::StringLiteral) || is(TokenKind::NumericLiteral) ||
      is(TokenKind::PrivateIdentifier) || isAlwaysIdentifier(kind())) {
    m_scanner.scanOne();
  }
  else {
    errorAt(token(), 1174, string("Property name expected."));
  }
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseArguments()
{
  uint32_t firstToken = pos();
  (void)expect(TokenKind::OpenParenToken);
  NodeId node = m_tree->beginNode(NodeKind::OmittedExpression, firstToken); // arguments run
  while (!is(TokenKind::CloseParenToken) && !is(TokenKind::EndOfFile)) {
    if (eat(TokenKind::DotDotDotToken)) {
      NodeId spread = m_tree->beginNode(NodeKind::SpreadElement, firstToken);
      m_tree->addChild(parseAssignmentExpression());
      m_tree->addChild(m_tree->endNode(spread, pos()));
    }
    else {
      m_tree->addChild(parseAssignmentExpression());
    }
    if (!eat(TokenKind::CommaToken)) {
      break;
    }
  }
  (void)expect(TokenKind::CloseParenToken);
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseTemplateLiteral(bool tagged)
{
  uint32_t firstToken = pos();
  NodeKind wrapper = tagged ? NodeKind::TaggedTemplateExpression : NodeKind::TemplateExpression;
  // The scanner produced TemplateHead (or NoSubstitution) tokens; the parser
  // drives the rescan boundary after each substitution's `}`.
  if (is(TokenKind::NoSubstitutionTemplateLiteral)) {
    NodeId node = m_tree->beginNode(tagged ? NodeKind::TaggedTemplateExpression
                                           : NodeKind::NoSubstitutionTemplateLiteral,
                                    firstToken);
    m_scanner.scanOne();
    return m_tree->endNode(node, pos());
  }
  (void)expect(TokenKind::TemplateHead);
  NodeId node = m_tree->beginNode(wrapper, firstToken);
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

NodeId Parser::parseCallChain(NodeId expression)
{
  for (;;) {
    switch (kind()) {
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
      m_tree->addChild(parseExpression());
      (void)expect(TokenKind::CloseBracketToken);
      expression = m_tree->endNode(node, pos());
      break;
    }
    case TokenKind::OpenParenToken: {
      NodeKind kind =
          hasFlags(m_tree->node(expression).flags, FLAG_OPTIONAL_CHAIN) ? NodeKind::OptionalCallExpression
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
      }
      else if (is(TokenKind::OpenBracketToken)) {
        m_scanner.scanOne();
        NodeId node = m_tree->beginNode(NodeKind::ElementAccessExpression, firstToken);
        m_tree->node(node).flags |= FLAG_OPTIONAL_CHAIN;
        m_tree->addChild(expression);
        m_tree->addChild(parseAssignmentExpression());
        (void)expect(TokenKind::CloseBracketToken);
        expression = m_tree->endNode(node, pos());
      }
      else {
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
  if (isAlwaysIdentifier(kind()) || is(TokenKind::PrivateIdentifier) ||
      is(TokenKind::NumericLiteral) || is(TokenKind::StringLiteral)) {
    m_scanner.scanOne();
  }
  else {
    errorAt(token(), 1003, string("Identifier expected."));
    m_tree->node(node).flags |= FLAG_MISSING;
    return m_tree->endNode(node, firstToken);
  }
  return m_tree->endNode(node, pos());
}

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
    while (!is(TokenKind::CloseBracketToken) && !is(TokenKind::EndOfFile)) {
      if (eat(TokenKind::DotDotDotToken)) {
        NodeId rest = m_tree->beginNode(NodeKind::RestType, pos() - 1);
        m_tree->addChild(parseType());
        m_tree->addChild(m_tree->endNode(rest, pos()));
      }
      else {
        if (isAlwaysIdentifier(kind()) && peekKind() == TokenKind::ColonToken) {
          // named tuple member: `name: T`
          uint32_t nameIndex = pos();
          NodeId member = m_tree->beginNode(NodeKind::NamedTupleMember, nameIndex);
          m_scanner.scanOne();
          m_scanner.scanOne();
          m_tree->addChild(parseType());
          m_tree->addChild(m_tree->endNode(member, pos()));
        }
        else {
          bool optionalType = eat(TokenKind::QuestionToken);
          NodeId element = m_tree->beginNode(optionalType ? NodeKind::OptionalType
                                                           : NodeKind::TypeReference,
                                              pos());
          m_tree->addChild(parseType());
          m_tree->addChild(m_tree->endNode(element, pos()));
        }
      }
      if (!eat(TokenKind::CommaToken)) {
        break;
      }
    }
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
    while (!is(TokenKind::CloseBraceToken) && !is(TokenKind::EndOfFile)) {
      uint32_t before = pos();
      m_tree->addChild(parseTypeMember(false));
      if (pos() == before) {
        errorAt(token(), 1174, string("Member expected."));
        NodeId errorNode = m_tree->beginNode(NodeKind::ErrorNode, before);
        m_scanner.scanOne();
        m_tree->addChild(m_tree->endNode(errorNode, pos()));
      }
    }
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
  while (!is(TokenKind::GreaterThanToken) && !is(TokenKind::EndOfFile)) {
    uint32_t paramFirst = pos();
    NodeId param = m_tree->beginNode(NodeKind::TypeParameter, paramFirst);
    if (eat(TokenKind::ConstKeyword)) {
      // `const T` type parameter
    }
    if (isAlwaysIdentifier(kind())) {
      m_scanner.scanOne();
    }
    if (eat(TokenKind::ExtendsKeyword)) {
      m_tree->addChild(parseType());
    }
    if (eat(TokenKind::EqualsToken)) {
      m_tree->addChild(parseType());
    }
    m_tree->addChild(m_tree->endNode(param, pos()));
    if (!eat(TokenKind::CommaToken)) {
      break;
    }
  }
  (void)expect(TokenKind::GreaterThanToken);
  return m_tree->endNode(node, pos());
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
  m_tree->node(node).kind = NodeKind::TypeReference;
  while (!is(TokenKind::GreaterThanToken) && !is(TokenKind::EndOfFile)) {
    m_tree->addChild(parseType());
    if (!eat(TokenKind::CommaToken)) {
      break;
    }
  }
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
  if (eat(TokenKind::NewKeyword) && peekKind() == TokenKind::OpenParenToken) {
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
  while (!is(TokenKind::CloseBraceToken) && !is(TokenKind::EndOfFile)) {
    uint32_t before = pos();
    m_tree->addChild(parseTypeMember(true));
    if (pos() == before) {
      // No progress on garbage: consume one token as an error node.
      errorAt(token(), 1174, string("Member expected."));
      NodeId errorNode = m_tree->beginNode(NodeKind::ErrorNode, before);
      m_scanner.scanOne();
      m_tree->addChild(m_tree->endNode(errorNode, pos()));
    }
  }
  (void)expect(TokenKind::CloseBraceToken);
  return m_tree->endNode(node, pos());
}


// ----------------------------------------------------------------- debug dump

namespace {

struct TreeWriter {
    GrammarTree &tree;
    std::string &out;

    void text(std::string_view t)
    {
      out.append(t);
    }

    void walk(NodeId id, int depth)
    {
      const Node &node = tree.nodes()[id];
      for (int i = 0; i < depth; ++i) {
        text("  ");
      }
      text("(");
      text(nodeKindName(node.kind));
      if (node.flags) {
        text(" :");
        if (node.flags & FLAG_MISSING) {
          text(" missing");
        }
        if (node.flags & FLAG_ERROR) {
          text(" error");
        }
        if (node.flags & FLAG_AMBIENT) {
          text(" ambient");
        }
        if (node.flags & FLAG_EXPORTED) {
          text(" exported");
        }
        if (node.flags & FLAG_DEFAULT) {
          text(" default");
        }
        if (node.flags & FLAG_ASYNC) {
          text(" async");
        }
        if (node.flags & FLAG_CONST) {
          text(" const");
        }
        if (node.flags & FLAG_READONLY) {
          text(" readonly");
        }
        if (node.flags & FLAG_STATIC) {
          text(" static");
        }
        if (node.flags & FLAG_ABSTRACT) {
          text(" abstract");
        }
        if (node.flags & FLAG_OVERRIDE) {
          text(" override");
        }
        if (node.flags & FLAG_ACCESSOR) {
          text(" accessor");
        }
        if (node.flags & FLAG_OPTIONAL) {
          text(" optional");
        }
        if (node.flags & FLAG_REST) {
          text(" rest");
        }
        if (node.flags & FLAG_GENERATOR) {
          text(" generator");
        }
        if (node.flags & FLAG_OPTIONAL_CHAIN) {
          text(" optional-chain");
        }
      }
      // Tokens not owned by any child belong to this node directly.
      uint32_t tokenEnd = node.firstToken + node.tokenCount;
      auto children = tree.children(id);
      for (uint32_t i = node.firstToken; i < tokenEnd; ++i) {
        if (tree.tokenAt(i).kind == TokenKind::EndOfFile) {
          break;
        }
        bool owned = false;
        for (NodeId child : children) {
          const Node &c = tree.nodes()[child];
          if (i >= c.firstToken && i < c.firstToken + c.tokenCount) {
            owned = true;
            break;
          }
        }
        if (owned) {
          continue;
        }
        text(" \"");
        text(tree.tokenText(tree.tokenAt(i)));
        text("\"");
      }
      text("\n");
      for (NodeId child : tree.children(id)) {
        walk(child, depth + 1);
      }
      for (int i = 0; i < depth; ++i) {
        text("  ");
      }
      text(")\n");
    }
};

} // namespace
void dumpTree(GrammarTree &tree, litestl::util::string &out)
{
  std::string buffer;
  // S-expression dump: (Kind flags? "token" children…). Tokens print their
  // source text; nodes recurse. Used by parser tests and debugging.

  TreeWriter writer{tree, buffer};
  if (tree.root() != kNoNode) {
    writer.walk(tree.root(), 0);
  }
  out += buffer;
}

} // namespace fastlint::syntax
