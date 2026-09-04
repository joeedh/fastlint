#include "fastlint/syntax/parser.h"

#include "fastlint/syntax/parser/internal.h"

namespace fastlint::syntax {

using litestl::util::string;

using detail::binaryPrecedence;
using detail::isAlwaysIdentifier;

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

} // namespace fastlint::syntax
