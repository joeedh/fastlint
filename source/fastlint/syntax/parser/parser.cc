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

uint32_t Parser::expectGreaterThan()
{
  (void)m_scanner.rescanGreaterThan();
  return expect(TokenKind::GreaterThanToken);
}

bool Parser::canInsertSemicolon() const
{
  return is(TokenKind::EndOfFile) || is(TokenKind::CloseBraceToken) ||
         hasPrecedingLineBreak();
}

Semicolon Parser::expectSemicolon()
{
  if (eat(TokenKind::SemicolonToken)) {
    return Semicolon::Written;
  }
  if (canInsertSemicolon()) {
    return Semicolon::Inserted;
  }
  errorAt(token(), 1005, string("';' expected."));
  return Semicolon::Missing;
}

NodeId Parser::endStatement(NodeId node)
{
  if (expectSemicolon() == Semicolon::Inserted) {
    m_tree->node(node).flags |= FLAG_ASI;
  }
  return m_tree->endNode(node, pos());
}

void Parser::error(uint32_t code, uint32_t offset, uint32_t length, string message)
{
  // Recovery can fail several productions at one token; the first report
  // there is the useful one.
  const Vector<Diagnostic> &items = m_diagnostics.items();
  if (!items.isEmpty() && items[int(items.size()) - 1].offset == offset) {
    return;
  }
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
    : m_diagnostics(diagnostics),
      m_scanner(source, Scanner::Options{options.javaScript}, diagnostics),
      m_options(options)
{
}

void Parser::parseFile(GrammarTree &tree)
{
  m_tree = &tree;
  m_allowAwait = true;
  tree.setSourceForBuild(m_scanner.source());

  // pos() is kNoToken until the first token is scanned
  m_scanner.scanOne();
  NodeId root = tree.beginNode(NodeKind::SourceFile, pos());

  parseList(ListKind::SourceElements, [&] { return parseStatement(); });
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
