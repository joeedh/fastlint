#include "fastlint/syntax/parser.h"

#include "fastlint/syntax/parser/internal.h"

namespace fastlint::syntax {

using litestl::util::string;

using detail::binaryPrecedence;
using detail::isAlwaysIdentifier;

// ------------------------------------------------- functions, classes, modules

NodeId Parser::parseFunctionDeclaration(bool asyncFlag, bool ambientFlag)
{
  // The caller ate the `async`/`declare` modifier.
  uint32_t firstToken = pos() - (asyncFlag ? 1 : 0) - (ambientFlag ? 1 : 0);
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
  uint32_t flags = m_tree->node(node).flags;
  detail::FlagScope allowYield(m_inYieldContext, hasFlags(flags, FLAG_GENERATOR));
  detail::FlagScope allowAwait(m_allowAwait, hasFlags(flags, FLAG_ASYNC));
  if (is(TokenKind::OpenBraceToken)) {
    m_tree->addChild(parseBlock());
  } else {
    // Ambient declaration or an error-recovery `;`.
    if (expectSemicolon() == Semicolon::Inserted) {
      m_tree->node(node).flags |= FLAG_ASI;
    }
    m_tree->addChild(missingNode(NodeKind::Block, pos()));
  }
}

NodeId Parser::parseBindingPattern()
{
  uint32_t firstToken = pos();
  switch (kind()) {
  case TokenKind::OpenBracketToken: {
    m_scanner.scanOne();
    NodeId pattern = m_tree->beginNode(NodeKind::ArrayBindingPattern, firstToken);
    parseDelimitedList(ListKind::ArrayBindingElements,
                       [&] { return parseBindingElement(true); });
    (void)expect(TokenKind::CloseBracketToken);
    return m_tree->endNode(pattern, pos());
  }
  case TokenKind::OpenBraceToken: {
    m_scanner.scanOne();
    NodeId pattern = m_tree->beginNode(NodeKind::ObjectBindingPattern, firstToken);
    parseDelimitedList(ListKind::ObjectBindingElements,
                       [&] { return parseBindingElement(false); });
    (void)expect(TokenKind::CloseBraceToken);
    return m_tree->endNode(pattern, pos());
  }
  default: {
    NodeId name = kNoNode;
    uint32_t nameFirst = pos();
    if (is(TokenKind::PrivateIdentifier)) {
      name = m_tree->beginNode(NodeKind::PrivateIdentifier, nameFirst);
    } else {
      name = m_tree->beginNode(NodeKind::Identifier, nameFirst);
    }
    // `this` is only a parameter name, but that is the callers' business.
    if (isAlwaysIdentifier(kind()) || is(TokenKind::PrivateIdentifier) ||
        is(TokenKind::ThisKeyword))
    {
      m_scanner.scanOne();
    } else {
      errorAt(token(), 1136, string("Declaration expected."));
      m_tree->node(name).flags |= FLAG_MISSING;
    }
    return m_tree->endNode(name, pos());
  }
  }
}

NodeId Parser::parseBindingElement(bool inArrayPattern)
{
  NodeId element = m_tree->beginNode(NodeKind::BindingElement, pos());
  if (eat(TokenKind::DotDotDotToken)) {
    m_tree->node(element).flags |= FLAG_REST;
  }
  if (!inArrayPattern) {
    m_tree->addChild(parsePropertyName());
  } else if (is(TokenKind::CommaToken)) {
    // Hole in the pattern.
    m_tree->addChild(missingNode(NodeKind::OmittedExpression, pos()));
  } else {
    m_tree->addChild(parseBindingPattern());
  }
  if (eat(TokenKind::ColonToken)) {
    m_tree->addChild(parseBindingPattern());
  }
  if (eat(TokenKind::EqualsToken)) {
    m_tree->addChild(parseAssignmentExpression());
  }
  return m_tree->endNode(element, pos());
}

void Parser::parseParameterList(bool allowModifiers, bool isConstructor)
{
  parseDelimitedList(ListKind::Parameters,
                     [&] { return parseParameter(allowModifiers, isConstructor); });
}

NodeId Parser::parseParameter(bool allowModifiers, bool isConstructor)
{
  uint32_t firstToken = pos();
  (void)allowModifiers;
  (void)isConstructor;
  NodeId parameter = m_tree->beginNode(NodeKind::Parameter, firstToken);
  parseDecorators();
  // Modifiers on constructor parameters become properties; recorded as flags.
  // A modifier word followed by anything but a binding is the parameter name.
  for (;;) {
    uint32_t flag = is(TokenKind::PublicKeyword)      ? FLAG_PUBLIC
                    : is(TokenKind::PrivateKeyword)   ? FLAG_PRIVATE
                    : is(TokenKind::ProtectedKeyword) ? FLAG_PROTECTED
                    : is(TokenKind::ReadOnlyKeyword)  ? FLAG_READONLY
                    : is(TokenKind::OverrideKeyword)  ? FLAG_OVERRIDE
                                                      : FLAG_NONE;
    if (flag == FLAG_NONE) {
      break;
    }
    TokenKind next = peekKind();
    bool bindingFollows =
        isAlwaysIdentifier(next) || next == TokenKind::OpenBraceToken ||
        next == TokenKind::OpenBracketToken || next == TokenKind::DotDotDotToken ||
        next == TokenKind::PublicKeyword || next == TokenKind::PrivateKeyword ||
        next == TokenKind::ProtectedKeyword || next == TokenKind::ReadOnlyKeyword ||
        next == TokenKind::OverrideKeyword || next == TokenKind::ThisKeyword;
    if (!bindingFollows) {
      break;
    }
    m_tree->node(parameter).flags |= flag;
    m_scanner.scanOne();
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
  // The caller ate the `declare`/`abstract` modifiers.
  uint32_t firstToken = pos() - (declareFlag ? 1 : 0) - (abstractFlag ? 1 : 0);
  NodeId node = m_tree->beginNode(NodeKind::ClassDeclaration, firstToken);
  if (declareFlag) {
    m_tree->node(node).flags |= FLAG_AMBIENT;
  }
  if (abstractFlag) {
    m_tree->node(node).flags |= FLAG_ABSTRACT;
  }
  return parseClassLike(node);
}

NodeId Parser::parseClassExpression()
{
  NodeId node = m_tree->beginNode(NodeKind::ClassExpression, pos());
  return parseClassLike(node);
}

/**
 * From the first decorator (when called at `@`) or `class`, through the body.
 */
void Parser::parseDecorators()
{
  while (is(TokenKind::AtToken)) {
    uint32_t firstToken = pos();
    m_scanner.scanOne();
    NodeId decorator = m_tree->beginNode(NodeKind::Decorator, firstToken);
    m_tree->addChild(parseCallChain(parsePrimary()));
    m_tree->addChild(m_tree->endNode(decorator, pos()));
  }
}

/**
 * A statement starting with `@`. Decorators may precede `export`, in which
 * case the class still nests inside the ExportDeclaration and consumes the
 * `export` keyword itself.
 */
NodeId Parser::parseDecoratedStatement()
{
  uint32_t firstToken = pos();
  Mark mark = begin();
  (void)m_tree->beginNode(NodeKind::ClassDeclaration, firstToken);
  parseDecorators();
  bool exported = is(TokenKind::ExportKeyword);
  rollback(mark);
  if (!exported) {
    return parseClassDeclaration(false, false);
  }
  NodeId exportNode = m_tree->beginNode(NodeKind::ExportDeclaration, firstToken);
  NodeId classNode = m_tree->beginNode(NodeKind::ClassDeclaration, firstToken);
  parseDecorators();
  (void)expect(TokenKind::ExportKeyword);
  m_tree->node(exportNode).flags |= FLAG_EXPORTED;
  if (eat(TokenKind::DefaultKeyword)) {
    m_tree->node(exportNode).flags |= FLAG_DEFAULT;
  }
  if (is(TokenKind::AbstractKeyword) && peekKind() == TokenKind::ClassKeyword) {
    m_scanner.scanOne();
    m_tree->node(classNode).flags |= FLAG_ABSTRACT;
  }
  m_tree->addChild(parseClassLike(classNode));
  return m_tree->endNode(exportNode, pos());
}

NodeId Parser::parseClassLike(NodeId node)
{
  parseDecorators();
  (void)expect(TokenKind::ClassKeyword);
  // `class implements A {}` and `class extends B {}` are anonymous.
  if (isAlwaysIdentifier(kind()) && !is(TokenKind::ImplementsKeyword) &&
      !is(TokenKind::ExtendsKeyword))
  {
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
  parseList(ListKind::ClassMembers, [&] { return parseClassMember(); });
  (void)expect(TokenKind::CloseBraceToken);
  return m_tree->endNode(node, pos());
}

/** `name: KeyType]: ValueType`, after the `[`; the key is a Parameter child. */
void Parser::parseIndexSignatureRest()
{
  NodeId key = m_tree->beginNode(NodeKind::Parameter, pos());
  m_tree->addChild(parseBindingPattern());
  if (eat(TokenKind::ColonToken)) {
    m_tree->addChild(parseType());
  }
  m_tree->addChild(m_tree->endNode(key, pos()));
  (void)expect(TokenKind::CloseBracketToken);
  if (eat(TokenKind::ColonToken)) {
    m_tree->addChild(parseType());
  }
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
      if (is(TokenKind::LessThanToken)) {
        m_tree->addChild(parseTypeArgumentList());
      }
    } else {
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
  if (eat(TokenKind::SemicolonToken)) {
    NodeId node = m_tree->beginNode(NodeKind::SemicolonClassElement, firstToken);
    return m_tree->endNode(node, pos());
  }

  // The kind is settled once the name is known; decorators come first, so
  // the node is opened before them.
  NodeId member = m_tree->beginNode(NodeKind::PropertyDeclaration, firstToken);
  parseDecorators();

  uint32_t flags = FLAG_NONE;

  bool isConstructor = false;
  bool isGet = false;

  // Modifier scan: visibility/static/readonly/abstract/override/declare/
  // accessor, then a possible `*` generator star, then the member name.
  for (;;) {
    if ((is(TokenKind::PublicKeyword) || is(TokenKind::PrivateKeyword) ||
         is(TokenKind::ProtectedKeyword)) &&
        nextCanFollowModifier())
    {
      flags |= is(TokenKind::PublicKeyword)    ? FLAG_PUBLIC
               : is(TokenKind::PrivateKeyword) ? FLAG_PRIVATE
                                               : FLAG_PROTECTED;
      m_scanner.scanOne();
      continue;
    }
    // `static`, `get` and `set` may be followed by a line break; the others
    // become the member name then.
    if (is(TokenKind::StaticKeyword) && nextCanFollowModifier(false)) {
      flags |= FLAG_STATIC;
      m_scanner.scanOne();
      continue;
    }
    if (is(TokenKind::ReadOnlyKeyword) && nextCanFollowModifier()) {
      flags |= FLAG_READONLY;
      m_scanner.scanOne();
      continue;
    }
    if (is(TokenKind::AbstractKeyword) && nextCanFollowModifier()) {
      flags |= FLAG_ABSTRACT;
      m_scanner.scanOne();
      continue;
    }
    if (is(TokenKind::OverrideKeyword) && nextCanFollowModifier()) {
      flags |= FLAG_OVERRIDE;
      m_scanner.scanOne();
      continue;
    }
    if (is(TokenKind::DeclareKeyword) && nextCanFollowModifier()) {
      flags |= FLAG_AMBIENT;
      m_scanner.scanOne();
      continue;
    }
    if (is(TokenKind::AccessorKeyword) && nextCanFollowModifier()) {
      flags |= FLAG_ACCESSOR;
      m_scanner.scanOne();
      continue;
    }
    if (is(TokenKind::AsyncKeyword) && !nextHasLineBreak() &&
        (nextStartsPropertyName() || peekKind() == TokenKind::AsteriskToken))
    {
      flags |= FLAG_ASYNC;
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
    m_tree->node(member).kind = NodeKind::IndexSignature;
    m_tree->node(member).flags |= flags;
    parseIndexSignatureRest();
    (void)eat(TokenKind::SemicolonToken);
    return m_tree->endNode(member, pos());
  }
  if (is(TokenKind::OpenParenToken)) {
    m_tree->node(member).kind = NodeKind::CallSignature;
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
    m_tree->node(member).kind = NodeKind::ConstructSignature;
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
  } else if ((is(TokenKind::GetKeyword) || is(TokenKind::SetKeyword)) &&
             nextCanFollowModifier(false))
  {
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

  m_tree->node(member).kind = memberKind;
  m_tree->node(member).flags |= flags;

  // The name (identifier, keyword, string or computed). Accessor keywords
  // were already recognized; consume one before the real name.
  if (isGet || isSet) {
    m_scanner.scanOne();
  }
  if (is(TokenKind::OpenBracketToken)) {
    m_tree->addChild(parsePropertyName());
  } else {
    NodeId name =
        m_tree->beginNode(is(TokenKind::StringLiteral)       ? NodeKind::StringLiteral
                          : is(TokenKind::NumericLiteral)    ? NodeKind::NumericLiteral
                          : is(TokenKind::PrivateIdentifier) ? NodeKind::PrivateIdentifier
                                                             : NodeKind::Identifier,
                          pos());
    if (is(TokenKind::StringLiteral) || is(TokenKind::NumericLiteral) ||
        is(TokenKind::PrivateIdentifier) || isAlwaysIdentifier(kind()) ||
        tokenIsKeyword(kind()))
    {
      m_scanner.scanOne();
    } else {
      errorAt(token(), 1174, string("Property name expected."));
    }
    m_tree->addChild(m_tree->endNode(name, pos()));
  }

  bool methodForm =
      isConstructor || isGet || isSet || memberKind == NodeKind::MethodDeclaration ||
      is(TokenKind::OpenParenToken) || is(TokenKind::LessThanToken) ||
      (is(TokenKind::QuestionToken) && (peekKind() == TokenKind::OpenParenToken ||
                                        peekKind() == TokenKind::LessThanToken));
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
      detail::FlagScope allowYield(m_inYieldContext, hasFlags(flags, FLAG_GENERATOR));
      detail::FlagScope allowAwait(m_allowAwait, hasFlags(flags, FLAG_ASYNC));
      m_tree->addChild(parseBlock());
      return m_tree->endNode(member, pos());
    }
    // Overload signature or abstract/ambient method: no body, a `;` or ASI.
    m_tree->addChild(missingNode(NodeKind::Block, pos()));
    return endStatement(member);
  } else {
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
    return endStatement(member);
  }
}

// -------------------------------------------------------------------- modules

NodeId Parser::parseImportDeclaration(bool ambientFlag)
{
  uint32_t firstToken = pos();
  m_scanner.scanOne(); // `import`

  // `import x = require("…")` / `import x = NS.Name`, possibly `import type x =`
  bool typeOnlyEquals = is(TokenKind::TypeKeyword) && isAlwaysIdentifier(peekKind()) &&
                        peekKind2() == TokenKind::EqualsToken;
  if (typeOnlyEquals ||
      (isAlwaysIdentifier(kind()) && peekKind() == TokenKind::EqualsToken))
  {
    NodeId node = m_tree->beginNode(NodeKind::ImportEqualsDeclaration, firstToken);
    if (ambientFlag) {
      m_tree->node(node).flags |= FLAG_AMBIENT;
    }
    if (typeOnlyEquals) {
      m_scanner.scanOne();
      m_tree->node(node).flags |= FLAG_TYPE_ONLY;
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
        NodeId literal = m_tree->beginNode(NodeKind::StringLiteral, index);
        m_scanner.scanOne();
        m_tree->addChild(m_tree->endNode(literal, index + 1));
      }
      (void)expect(TokenKind::CloseParenToken);
      m_tree->addChild(m_tree->endNode(ref, pos()));
    } else {
      m_tree->addChild(parseEntityName()); // `A.B.C`
    }
    return endStatement(node);
  }

  NodeId node = m_tree->beginNode(NodeKind::ImportDeclaration, firstToken);
  if (ambientFlag) {
    m_tree->node(node).flags |= FLAG_AMBIENT;
  }
  // `import type x from`; `import type from "m"` imports a binding named type.
  if (is(TokenKind::TypeKeyword) && peekKind() != TokenKind::FromKeyword &&
      peekKind() != TokenKind::CommaToken && peekKind() != TokenKind::EqualsToken)
  {
    m_scanner.scanOne();
    m_tree->node(node).flags |= FLAG_TYPE_ONLY;
  }
  if (is(TokenKind::StringLiteral)) {
    // Side-effect import: `import "mod"`.
    uint32_t index = pos();
    NodeId module = m_tree->beginNode(NodeKind::StringLiteral, index);
    m_scanner.scanOne();
    m_tree->addChild(m_tree->endNode(module, index + 1));
  } else {
    NodeId clause = m_tree->beginNode(NodeKind::ImportClause, pos());
    if (isAlwaysIdentifier(kind())) {
      uint32_t nameIndex = pos();
      m_scanner.scanOne();
      NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
      m_tree->addChild(m_tree->endNode(name, nameIndex + 1));
    }
    (void)eat(TokenKind::CommaToken);
    if (is(TokenKind::OpenBraceToken)) {
      m_tree->addChild(parseImportOrExportClause(true));
    } else if (is(TokenKind::AsteriskToken)) {
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
  } else if (eat(TokenKind::AssertKeyword)) {
    m_tree->addChild(parseImportAttributes());
  }
  return endStatement(node);
}

/** `{ type: "json" }`; each attribute is a PropertyAssignment. */
NodeId Parser::parseImportAttributes()
{
  uint32_t firstToken = pos();
  (void)expect(TokenKind::OpenBraceToken);
  NodeId attributes = m_tree->beginNode(NodeKind::ImportAttributes, firstToken);
  parseDelimitedList(ListKind::ImportAttributes, [&] {
    NodeId attribute = m_tree->beginNode(NodeKind::PropertyAssignment, pos());
    m_tree->addChild(parsePropertyName());
    (void)expect(TokenKind::ColonToken);
    uint32_t valueIndex = pos();
    NodeId value = m_tree->beginNode(NodeKind::StringLiteral, valueIndex);
    if (!eat(TokenKind::StringLiteral)) {
      errorAt(token(), 1141, string("String literal expected."));
      m_tree->node(value).flags |= FLAG_MISSING;
    }
    m_tree->addChild(m_tree->endNode(value, pos()));
    return m_tree->endNode(attribute, pos());
  });
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
    m_tree->node(node).kind = NodeKind::ExportAssignment;
    m_tree->addChild(parseAssignmentExpression());
    return endStatement(node);
  }
  if (is(TokenKind::AsKeyword) && peekKind() == TokenKind::NamespaceKeyword) {
    // `export as namespace Lib` (UMD global)
    m_tree->node(node).kind = NodeKind::NamespaceExportDeclaration;
    m_scanner.scanOne();
    m_scanner.scanOne();
    NodeId name = m_tree->beginNode(NodeKind::Identifier, pos());
    if (isAlwaysIdentifier(kind())) {
      m_scanner.scanOne();
    } else {
      errorAt(token(), 1003, string("Identifier expected."));
      m_tree->node(name).flags |= FLAG_MISSING;
    }
    m_tree->addChild(m_tree->endNode(name, pos()));
    return endStatement(node);
  }
  if (eat(TokenKind::DefaultKeyword)) {
    m_tree->node(node).flags |= FLAG_DEFAULT | FLAG_EXPORTED;
    if (kind() == TokenKind::FunctionKeyword || kind() == TokenKind::AsyncKeyword) {
      bool asyncFlag = eat(TokenKind::AsyncKeyword);
      m_tree->addChild(parseFunctionDeclaration(asyncFlag, false));
      return m_tree->endNode(node, pos());
    }
    if (kind() == TokenKind::ClassKeyword || kind() == TokenKind::AtToken ||
        (kind() == TokenKind::AbstractKeyword && peekKind() == TokenKind::ClassKeyword))
    {
      bool abstractFlag = eat(TokenKind::AbstractKeyword);
      m_tree->addChild(parseClassDeclaration(false, abstractFlag));
      return m_tree->endNode(node, pos());
    }
    if (kind() == TokenKind::InterfaceKeyword && wordFollows()) {
      m_tree->addChild(parseInterfaceDeclaration(false));
      return m_tree->endNode(node, pos());
    }
    m_tree->addChild(parseAssignmentExpression());
    return endStatement(node);
  }
  m_tree->node(node).flags |= FLAG_EXPORTED;

  // `export type { … }` / `export type * from`; a type alias keeps its `type`.
  if (is(TokenKind::TypeKeyword) &&
      (peekKind() == TokenKind::OpenBraceToken || peekKind() == TokenKind::AsteriskToken))
  {
    m_scanner.scanOne();
    m_tree->node(node).flags |= FLAG_TYPE_ONLY;
  }
  if (is(TokenKind::AsteriskToken) && peekKind() == TokenKind::AsKeyword) {
    m_tree->addChild(parseNamespaceImportOrExport(false));
  } else if (eat(TokenKind::AsteriskToken)) {
    // `export * from "m"` re-exports without a binding, so no node.
  } else if (is(TokenKind::OpenBraceToken)) {
    m_tree->addChild(parseImportOrExportClause(false));
  } else {
    // A declaration ends itself: `export const …;`, `export function …`.
    m_tree->addChild(parseStatement());
    return m_tree->endNode(node, pos());
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
  return endStatement(node);
}

/** `{ a as b, "c" }` import or export list; the brace was not consumed. */
NodeId Parser::parseImportOrExportClause(bool isImport)
{
  uint32_t firstToken = pos();
  NodeId list = m_tree->beginNode(
      isImport ? NodeKind::NamedImports : NodeKind::NamedExports, firstToken);
  (void)expect(TokenKind::OpenBraceToken);
  parseDelimitedList(ListKind::ImportOrExportSpecifiers,
                     [&] { return parseImportOrExportSpecifier(isImport); });
  (void)expect(TokenKind::CloseBraceToken);
  return m_tree->endNode(list, pos());
}

NodeId Parser::parseImportOrExportSpecifier(bool isImport)
{
  uint32_t firstToken = pos();
  // `type` is a modifier only when a name follows it: `{ type A }`.
  if (is(TokenKind::TypeKeyword)) {
    TokenKind next = peekKind();
    if (next != TokenKind::CommaToken && next != TokenKind::CloseBraceToken &&
        next != TokenKind::AsKeyword)
    {
      m_scanner.scanOne();
    }
  }
  NodeId specifier = m_tree->beginNode(
      isImport ? NodeKind::ImportSpecifier : NodeKind::ExportSpecifier, firstToken);
  auto name = [&] {
    uint32_t nameIndex = pos();
    NodeKind nameKind =
        is(TokenKind::StringLiteral) ? NodeKind::StringLiteral : NodeKind::Identifier;
    NodeId node = m_tree->beginNode(nameKind, nameIndex);
    if (isAlwaysIdentifier(kind()) || tokenIsKeyword(kind()) ||
        is(TokenKind::StringLiteral))
    {
      m_scanner.scanOne();
    } else {
      errorAt(token(), 1003, string("Identifier expected."));
      m_tree->node(node).flags |= FLAG_MISSING;
    }
    m_tree->addChild(m_tree->endNode(node, pos()));
  };
  name();
  if (eat(TokenKind::AsKeyword)) {
    name();
  }
  return m_tree->endNode(specifier, pos());
}

NodeId Parser::parseNamespaceImportOrExport(bool isImport)
{
  uint32_t firstToken = pos();
  (void)expect(TokenKind::AsteriskToken);
  NodeId node = m_tree->beginNode(
      isImport ? NodeKind::NamespaceImport : NodeKind::NamespaceExport, firstToken);
  // `export * from "m"` has no alias; imports and `export * as ns` do.
  if (isImport || is(TokenKind::AsKeyword)) {
    (void)expect(TokenKind::AsKeyword);
    uint32_t nameIndex = pos();
    NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
    if (isAlwaysIdentifier(kind()) || is(TokenKind::StringLiteral)) {
      m_scanner.scanOne();
    } else {
      errorAt(token(), 1003, string("Identifier expected."));
      m_tree->node(name).flags |= FLAG_MISSING;
    }
    m_tree->addChild(m_tree->endNode(name, pos()));
  }
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseModuleDeclaration(bool ambientFlag)
{
  uint32_t firstToken = pos() - (ambientFlag ? 1 : 0); // the caller ate `declare`
  if (eat(TokenKind::ModuleKeyword)) {
    if (is(TokenKind::StringLiteral)) {
      // `module "…" { … }` — an ambient module.
    }
  } else {
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
  } else {
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
    parseList(ListKind::BlockStatements, [&] { return parseStatement(); });
    (void)expect(TokenKind::CloseBraceToken);
    m_tree->addChild(m_tree->endNode(block, pos()));
    return m_tree->endNode(node, pos());
  }
  return endStatement(node); // `declare module A;`
}

NodeId Parser::parseTypeAlias(bool ambientFlag)
{
  uint32_t firstToken = pos() - (ambientFlag ? 1 : 0); // the caller ate `declare`
  m_scanner.scanOne();                                 // `type`
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
  return endStatement(node);
}

NodeId Parser::parseInterfaceDeclaration(bool ambientFlag)
{
  uint32_t firstToken = pos() - (ambientFlag ? 1 : 0); // the caller ate `declare`
  m_scanner.scanOne();                                 // `interface`
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
  } else {
    (void)expect(TokenKind::OpenBraceToken);
    m_tree->addChild(missingNode(NodeKind::TypeLiteral, pos()));
  }
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseEnumDeclaration(bool ambientFlag)
{
  uint32_t firstToken = pos() - (ambientFlag ? 1 : 0); // the caller ate `declare`
  bool constFlag = eat(TokenKind::ConstKeyword);
  (void)expect(TokenKind::EnumKeyword);
  NodeId node = m_tree->beginNode(NodeKind::EnumDeclaration, firstToken);
  if (ambientFlag) {
    m_tree->node(node).flags |= FLAG_AMBIENT;
  }
  if (constFlag) {
    m_tree->node(node).flags |= FLAG_CONST;
  }
  uint32_t nameIndex = pos();
  NodeId name = m_tree->beginNode(NodeKind::Identifier, nameIndex);
  m_scanner.scanOne();
  m_tree->addChild(m_tree->endNode(name, nameIndex + 1));
  (void)expect(TokenKind::OpenBraceToken);
  parseDelimitedList(ListKind::EnumMembers, [&] { return parseEnumMember(); });
  (void)expect(TokenKind::CloseBraceToken);
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseEnumMember()
{
  NodeId member = m_tree->beginNode(NodeKind::EnumMember, pos());
  m_tree->addChild(parsePropertyName());
  if (eat(TokenKind::EqualsToken)) {
    m_tree->addChild(parseAssignmentExpression());
  }
  return m_tree->endNode(member, pos());
}

} // namespace fastlint::syntax
