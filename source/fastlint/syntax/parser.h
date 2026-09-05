#pragma once

// The recursive-descent parser (task 3.2). Drives the Scanner incrementally
// and builds the grammar tree via GrammarTree::beginNode/endNode. Node token
// ranges are indices into the final token array; the tree's token/trivia/
// line-start arrays are copied out of the scanner once parsing finishes, so
// speculation (scanner rewind) needs no special handling.
//
// Every production returns a node. Recovery happens inside the list loops
// (`parseList`/`parseDelimitedList`), which own progress: a token that fits
// no element of the current list is skipped as an ErrorNode, or ends the list
// when an enclosing list would accept it. Productions never unwind.

#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/scanner.h"
#include "fastlint/syntax/tree.h"
#include "util/set.h"

#include <cstdint>

namespace fastlint::syntax {

/** Sentinel token index (same value as kNoNode, different meaning). */
constexpr uint32_t kNoToken = 0xffffffffu;

/**
 * Bracketed or delimited list productions. Each has a sync set: which tokens
 * start an element and which end the list. The enclosing lists' sets decide
 * whether an unexpected token is skipped or ends the current list.
 */
enum class ListKind : uint8_t {
  SourceElements,
  BlockStatements,
  SwitchClauses,
  SwitchClauseStatements,
  ClassMembers,
  TypeMembers,
  EnumMembers,
  ObjectLiteralMembers,
  ArrayLiteralMembers,
  Arguments,
  Parameters,
  TypeParameters,
  TypeArguments,
  TupleElements,
  ObjectBindingElements,
  ArrayBindingElements,
  ImportOrExportSpecifiers,
  ImportAttributes,
};

/** How a statement was terminated. */
enum class Semicolon : uint8_t {
  /** A `;` token was consumed. */
  Written,
  /** Automatic semicolon insertion applied; no token was consumed. */
  Inserted,
  /** Neither; a diagnostic was reported. */
  Missing,
};

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
  /** `expect(>)` that first splits a `>>`-family token closing a type list. */
  uint32_t expectGreaterThan();
  /** True when ASI may end a statement before the current token. */
  bool canInsertSemicolon() const;
  /** Consumes a `;`, applies ASI, or reports that one is missing. */
  Semicolon expectSemicolon();
  /** Ends a statement node after its `;` (or ASI), flagging ASI on the node. */
  NodeId endStatement(NodeId node);
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
  /** True when `<T>(params): R =>` or `(params) =>` starts here. */
  /** `typeContext` probes for a function type instead of an arrow function. */
  bool isArrowHead(bool typeContext = false);
  /** Rejects a `(` by its first tokens before the speculative parse in isArrowHead. */
  bool mayBeArrowHead();
  /** A name, keyword or literal follows on the same line (tsgo's test for `await`/`yield`
   * outside their contexts). */
  bool operandFollowsOnLine();
  /** Parses `<…>` in expression position when what follows allows it. */
  bool tryParseTypeArguments(NodeId &typeArguments);
  bool canFollowTypeArguments();

  // ----------------------------------------------------------------- lists

  /** Parses elements until the list's terminator, recovering in place. */
  template <typename Fn> void parseList(ListKind list, Fn &&element);
  /** Comma-separated form of `parseList`; a trailing comma is accepted. */
  template <typename Fn> void parseDelimitedList(ListKind list, Fn &&element);

  bool isListElement(ListKind list);
  bool isListTerminator(ListKind list);
  /** True when a list other than `list` on the stack would take this token. */
  bool enclosingListAccepts(ListKind list);
  /** Reports the list's "expected" error, then skips one token or ends the list. */
  bool skipOrAbort(ListKind list);
  /** Wraps the current token in an ErrorNode and consumes it. */
  NodeId skipTokenAsError();

  bool isStartOfStatement();
  bool isStartOfExpression();
  bool isStartOfType();
  bool isStartOfClassMember();
  bool isStartOfTypeMember();
  bool isStartOfPropertyName();
  bool isStartOfBinding();
  bool isStartOfParameter();

  // --------------------------------------------------------------- context

  bool m_allowAwait = false; // top level, module, async bodies
  bool m_inYieldContext = false;
  bool m_inIteration = false; // break/continue targets
  bool m_inSwitch = false;
  bool m_ambient = false; // declare blocks/interfaces
  /** Inside the `extends` operand of a conditional type or an `infer` constraint. */
  bool m_noConditionalType = false;
  /** Inside a `for` head, where `in` ends the initializer. */
  bool m_disallowIn = false;
  /** Bit per `ListKind` currently being parsed. */
  uint32_t m_lists = 0;
  /** Probe keys (token position, context) where isArrowHead failed, so nested probes do
   * not repeat it. */
  litestl::util::Set<uint32_t> m_notArrowHead;

  // ---------------------------------------------------------------- helpers

  Diagnostics &m_diagnostics;
  Scanner m_scanner;
  GrammarTree *m_tree = nullptr;
  Options m_options;

  void
  error(uint32_t code, uint32_t offset, uint32_t length, litestl::util::string message);
  void errorAt(const Token &t, uint32_t code, litestl::util::string message);

  // statement/declaration/entry points, defined in parser.cc
  NodeId parseStatement();
  NodeId parseExpressionStatement();
  NodeId parseVariableStatement(bool declare);
  NodeId parseFunctionDeclaration(bool asyncFlag, bool ambientFlag);
  NodeId parseClassDeclaration(bool declareFlag, bool abstractFlag);
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
  /** Member, call and template suffixes; `stopAtCall` leaves `(` for a `new`. */
  NodeId parseCallChain(NodeId expression, bool stopAtCall = false);
  NodeId parseMemberName(TokenKind propertyKind);
  NodeId parsePrimary();
  NodeId parseParenthesizedOrArrow();
  NodeId parseObjectLiteral();

  // JSX (jsx.cc)
  /** Scans one token in `mode`, then restores Normal mode. */
  void scanNext(ScanMode mode);
  NodeId parseJsxElementOrFragment(bool inChildren);
  void parseJsxChildren();
  NodeId parseJsxTagName();
  NodeId parseJsxMemberChain(NodeId expression, uint32_t firstToken);
  NodeId parseJsxIdentifier();
  NodeId parseJsxAttributes();
  NodeId parseJsxExpression(bool inChildren);
  bool isJsxGenericArrowHead();

  NodeId parseTemplateLiteral();
  NodeId parseFunctionExpression(bool asyncFlag);
  NodeId parseClassExpression();
  NodeId parseArguments();

  // shared productions
  NodeId parseVariableDeclarationList(TokenKind keyword);
  NodeId parseVariableDeclaration();
  NodeId parseBindingPattern(); // identifier or pattern
  NodeId parseBindingElement(bool inArrayPattern);
  /** Appends parameters to the innermost open node; there is no wrapper node. */
  void parseParameterList(bool allowModifiers, bool isConstructor);
  NodeId parseParameter(bool allowModifiers, bool isConstructor);
  /** `[name:` or `[name,` (optionally `name?`), the head of an index signature. */
  bool isIndexSignatureStart();
  NodeId parseBlock();
  void parseFunctionCommon(NodeId node);
  NodeId parseClassLike(NodeId node);
  NodeId parseHeritageClause(NodeKind kind);
  void parseIndexSignatureRest();
  NodeId parseClassMember();
  NodeId parseObjectMember();
  NodeId parsePropertyName();
  NodeId parseEnumMember();
  NodeId parseSwitchClause();
  NodeId parseImportOrExportSpecifier(bool isImport);

  // types (TS)
  NodeId parseType();
  NodeId parseTypeOrTypePredicate();
  NodeId parseConditionalType();
  NodeId parseUnionType();
  NodeId parseIntersectionType();
  NodeId parseTypeOperator();
  NodeId parsePostfixType(NodeId type);
  NodeId parsePrimaryType();
  NodeId parseFunctionType(NodeKind kind, uint32_t firstToken);
  NodeId parseTypeReference();
  /** `A` as Identifier, `A.B.C` as QualifiedName over Identifiers; `keywordFirst` admits
   * `default`. */
  NodeId parseEntityName(bool keywordFirst = false);
  /** The name of a type parameter, `infer` binding or mapped-type key. */
  NodeId parseTypeParameterName();
  /** `x` or `this` before `is` in a type predicate. */
  NodeId parsePredicateName();
  NodeId parseTypeParameters();
  NodeId parseTypeParameter();
  NodeId parseTypeArguments(bool speculative);
  NodeId parseTypeArgumentList();
  NodeId parseTupleElement();
  NodeId parseTypeMember(bool isInterface);
  NodeId parseTypeLiteralBody();
  /** `("mod").A.B<T>` after `import`, appended to the open ImportType. */
  void parseImportTypeRest();

  // lookahead helpers
  TokenKind peekKind2();
  bool startsTypeAlias();
  bool wordFollows();
  bool nextHasLineBreak();
  /** True when the token after the current one can name a member. */
  bool nextStartsPropertyName();
  /** True when a modifier here is a modifier rather than the member name. */
  bool nextCanFollowModifier(bool sameLine = true);
  bool letStartsDeclaration();
  /** `using x` / `await using x` with the name on the same line. */
  bool usingStartsDeclaration();
  NodeId parseDecoratedStatement();
  /** Appends Decorator children for each leading `@expr`. */
  void parseDecorators();
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
/** S-expression dump; with `spans` each node prints as `Kind@start-end` (byte offsets).
 */
void dumpTree(GrammarTree &tree, litestl::util::string &out, bool spans = false);

} // namespace fastlint::syntax
