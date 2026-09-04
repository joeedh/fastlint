#pragma once

// The recursive-descent parser (task 3.2). Drives the Scanner incrementally
// and builds the grammar tree via GrammarTree::beginNode/endNode. Node token
// ranges are indices into the final token array; the tree's token/trivia/
// line-start arrays are copied out of the scanner once parsing finishes, so
// speculation (scanner rewind) needs no special handling.

#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/scanner.h"
#include "fastlint/syntax/tree.h"
#include "fastlint/syntax/parser/errors.h"
#include "fastlint/syntax/parser/errors.h"

using litestl::util::ValueOrError;
using litestl::util::SuccessOrError;

#include <cstdint>

namespace fastlint::syntax {

/** Sentinel token index (same value as kNoNode, different meaning). */
constexpr uint32_t kNoToken = 0xffffffffu;

class Parser {
public:
  struct Options {
    /** `.js`/`.jsx`: the scanner's JS lexing plus JS-only statement forms. */
    bool javaScript = false;
    /** `.tsx`/`.jsx`: JSX elements are expressions. */
    bool jsx = false;
  };

  Parser(std::string_view source, const Options &options, Diagnostics &diagnostics);

  /** Parses a whole file into `tree` (nodes, tokens, trivia, line starts). */
  void parseFile(GrammarTree &tree);

private:
  // ------------------------------------------------------------ token access

  TokenKind kind() const
  {
    return m_scanner.current().kind;
  }
  const Token &token() const
  {
    return m_scanner.current();
  }
  /** Index of the current token in the token array. */
  uint32_t pos() const
  {
    return m_scanner.tokenIndex();
  }
  bool is(TokenKind k) const
  {
    return m_scanner.current().kind == k;
  }
  /** A word token (identifier or keyword) usable as a name in this context. */
  bool isWord() const;

  /** Eats the token when it matches. */
  bool eat(TokenKind k);
  /** Consumes and returns the token index, or reports Missing with kNoToken. */
  uint32_t expect(TokenKind k);
  /** `;` or ASI; returns the semicolon index or kNoToken. */
  uint32_t expectSemicolon();
  /** The keyword's spelling as an identifier, in identifier contexts. */
  bool isIdentifierKind(TokenKind k) const;

  // ------------------------------------------------------------- speculation

  struct Mark {
    Scanner::State scanner;
    GrammarTree::BuildState build;
    size_t diagnostics;
  };
  Mark begin();
  void commit(const Mark &m);
  void rollback(const Mark &m);
  /** The kind of the token after the current one, via scanner rewind. */
  TokenKind peekKind();

  // --------------------------------------------------------------- context

  bool m_allowAwait = false;   // top level, module, async bodies
  bool m_inYieldContext = false;
  bool m_inIteration = false;  // break/continue targets
  bool m_inSwitch = false;
  bool m_ambient = false;      // declare blocks/interfaces

  // ---------------------------------------------------------------- helpers

  Diagnostics &m_diagnostics;
  Scanner m_scanner;
  GrammarTree *m_tree = nullptr;
  Options m_options;

  void error(uint32_t code, uint32_t offset, uint32_t length, litestl::util::string message);
  void errorAt(const Token &t, uint32_t code, litestl::util::string message);

  // statement/declaration/entry points, defined in parser.cc
  NodeId parseStatement();
  NodeId parseExpressionStatement();
  NodeId parseVariableStatement(bool declare);
  NodeId parseFunctionDeclaration(bool asyncFlag, bool ambientFlag);
  ParseClassLikeRet parseClassDeclaration(bool declareFlag, bool abstractFlag);
  NodeId parseExpressionOrLabeledStatement();
  NodeId parseIfStatement();
  NodeId parseWhileStatement();
  NodeId parseDoStatement();
  NodeId parseForStatement();
  NodeId parseBreakStatement();
  NodeId parseContinueStatement();
  NodeId parseReturnStatement();
  NodeId parseThrowStatement();
  NodeId parseTryStatement();
  NodeId parseSwitchStatement();
  NodeId parseWithStatement();
  NodeId parseDebuggerStatement();
  NodeId parseImportDeclaration(bool ambientFlag);
  NodeId parseExportDeclaration(bool ambientFlag);
  NodeId parseModuleDeclaration(bool ambientFlag);
  NodeId parseTypeAlias(bool ambientFlag);
  NodeId parseInterfaceDeclaration(bool ambientFlag);
  NodeId parseEnumDeclaration(bool ambientFlag);

  // expressions
  NodeId parseExpression(bool disallowComma = false);
  NodeId parseAssignmentExpression();
  NodeId parseArrowFunction(bool asyncFlag);
  NodeId parseConditional();
  NodeId parseBinary(int minPrecedence);
  NodeId parseUnary();
  NodeId parsePostfix();
  NodeId parseCallChain(NodeId expression);
  NodeId parseMemberName(TokenKind propertyKind);
  ParsePrimaryRet parsePrimary();
  NodeId parseParenthesizedOrArrow();
  NodeId parseArrayLiteral();
  NodeId parseObjectLiteral();
  NodeId parseTemplateLiteral(bool tagged);
  NodeId parseFunctionExpression(bool asyncFlag);
  ParseClassLikeRet parseClassExpression();
  NodeId parseArguments();

  // shared productions
  NodeId parseVariableDeclarationList(TokenKind keyword);
  NodeId parseVariableDeclaration(bool disallowIn);
  NodeId parseBindingPattern();  // identifier or pattern
  NodeId parseParameterList(bool allowModifiers, bool isConstructor);
  NodeId parseParameter(bool allowModifiers, bool isConstructor);
  NodeId parseBlock();
  NodeId parseFunctionBody(bool allowAwait, bool allowYield);
  void parseFunctionCommon(NodeId node);
  ParseClassLikeRet parseClassLike(NodeId node);
  NodeId parseHeritageClause(NodeKind kind);
  ParseClassMethodRet parseClassMember();
  NodeId parseObjectMember();
  NodeId parsePropertyName();

  // types (TS)
  NodeId parseType();
  NodeId parseTypeOrTypePredicate();
  NodeId parseUnionType();
  NodeId parseIntersectionType();
  NodeId parseTypeOperator();
  NodeId parsePostfixType(NodeId type);
  NodeId parsePrimaryType();
  NodeId parseTypeReference();
  NodeId parseTypeParameters();
  NodeId parseTypeArguments(bool speculative);
  NodeId parseTypeArgumentList();
  NodeId parseTypeMember(bool isInterface);
  NodeId parseTypeLiteralBody();
  NodeId parseImportTypeRest(uint32_t firstToken);

  // lookahead helpers
  TokenKind peekKind2();
  bool startsTypeAlias();
  bool wordFollows();
  bool letStartsDeclaration();
  NodeId missingNode(NodeKind kind, uint32_t at);
  NodeId parseBreakOrContinue(NodeKind kind, TokenKind keyword);
  NodeId parseImportOrExportClause(bool isImport);
  NodeId parseNamespaceImportOrExport(bool isImport);
  NodeId parseImportAttributes();

  bool hasPrecedingLineBreak() const
  {
    return m_scanner.current().precedingLineBreak;
  }
};

/** S-expression debug dump of a grammar tree (task 3.3's debugging aid). */
void dumpTree(GrammarTree &tree, litestl::util::string &out);

} // namespace fastlint::syntax
