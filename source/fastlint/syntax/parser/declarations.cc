#include "fastlint/syntax/parser.h"

#include "fastlint/syntax/parser/internal.h"

namespace fastlint::syntax {

using litestl::util::string;

using detail::binaryPrecedence;
using detail::isAlwaysIdentifier;

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

} // namespace fastlint::syntax
