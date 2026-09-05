#include "fastlint/syntax/parser.h"

#include "fastlint/syntax/parser/internal.h"

// Sync sets for the list productions and the shared recovery step. Every
// repetition in the parser goes through parseList/parseDelimitedList
// (internal.h), which consult these tables, so recovery behaviour is data
// here rather than logic at each call site.

namespace fastlint::syntax {

using litestl::util::string;

using detail::isAlwaysIdentifier;

namespace {

struct ListError {
  uint32_t code;
  const char *message;
};

/** TS's diagnostic for a token that fits nothing in the list. */
ListError listError(ListKind list)
{
  switch (list) {
  case ListKind::SourceElements:
  case ListKind::BlockStatements:
  case ListKind::SwitchClauseStatements:
    return {1128, "Declaration or statement expected."};
  case ListKind::SwitchClauses:
    return {1130, "'case' or 'default' expected."};
  case ListKind::ClassMembers:
    return {
        1068,
        "Unexpected token. A constructor, method, accessor, or property was expected."};
  case ListKind::TypeMembers:
    return {1131, "Property or signature expected."};
  case ListKind::EnumMembers:
    return {1132, "Enum member expected."};
  case ListKind::ObjectLiteralMembers:
    return {1136, "Property assignment expected."};
  case ListKind::ArrayLiteralMembers:
    return {1137, "Expression or comma expected."};
  case ListKind::Arguments:
    return {1135, "Argument expression expected."};
  case ListKind::Parameters:
    return {1138, "Parameter declaration expected."};
  case ListKind::TypeParameters:
    return {1139, "Type parameter declaration expected."};
  case ListKind::TypeArguments:
    return {1140, "Type argument expected."};
  case ListKind::TupleElements:
    return {1110, "Type expected."};
  case ListKind::ObjectBindingElements:
    return {1180, "Property destructuring pattern expected."};
  case ListKind::ArrayBindingElements:
    return {1181, "Array element destructuring pattern expected."};
  case ListKind::ImportOrExportSpecifiers:
    return {1003, "Identifier expected."};
  case ListKind::ImportAttributes:
    return {1478, "Identifier or string literal expected."};
  }
  return {1128, "Declaration or statement expected."};
}

} // namespace

// ------------------------------------------------------------ start-of tests

bool Parser::isStartOfExpression()
{
  switch (kind()) {
  case TokenKind::NumericLiteral:
  case TokenKind::BigIntLiteral:
  case TokenKind::StringLiteral:
  case TokenKind::RegularExpressionLiteral:
  case TokenKind::NoSubstitutionTemplateLiteral:
  case TokenKind::TemplateHead:
  case TokenKind::BacktickToken:
  case TokenKind::PrivateIdentifier:
  case TokenKind::ThisKeyword:
  case TokenKind::SuperKeyword:
  case TokenKind::NullKeyword:
  case TokenKind::TrueKeyword:
  case TokenKind::FalseKeyword:
  case TokenKind::FunctionKeyword:
  case TokenKind::ClassKeyword:
  case TokenKind::NewKeyword:
  case TokenKind::ImportKeyword:
  case TokenKind::DeleteKeyword:
  case TokenKind::TypeOfKeyword:
  case TokenKind::VoidKeyword:
  case TokenKind::AwaitKeyword:
  case TokenKind::YieldKeyword:
  case TokenKind::OpenParenToken:
  case TokenKind::OpenBracketToken:
  case TokenKind::OpenBraceToken:
  case TokenKind::PlusToken:
  case TokenKind::MinusToken:
  case TokenKind::TildeToken:
  case TokenKind::ExclamationToken:
  case TokenKind::PlusPlusToken:
  case TokenKind::MinusMinusToken:
  case TokenKind::LessThanToken:
  case TokenKind::SlashToken:
  case TokenKind::SlashEqualsToken:
    return true;
  default:
    return isAlwaysIdentifier(kind());
  }
}

bool Parser::isStartOfStatement()
{
  switch (kind()) {
  case TokenKind::SemicolonToken:
  case TokenKind::OpenBraceToken:
  case TokenKind::AtToken:
  case TokenKind::IfKeyword:
  case TokenKind::DoKeyword:
  case TokenKind::WhileKeyword:
  case TokenKind::ForKeyword:
  case TokenKind::ContinueKeyword:
  case TokenKind::BreakKeyword:
  case TokenKind::ReturnKeyword:
  case TokenKind::WithKeyword:
  case TokenKind::SwitchKeyword:
  case TokenKind::ThrowKeyword:
  case TokenKind::TryKeyword:
  case TokenKind::DebuggerKeyword:
  case TokenKind::VarKeyword:
  case TokenKind::LetKeyword:
  case TokenKind::ConstKeyword:
  case TokenKind::UsingKeyword:
  case TokenKind::FunctionKeyword:
  case TokenKind::ClassKeyword:
  case TokenKind::EnumKeyword:
  case TokenKind::ImportKeyword:
  case TokenKind::ExportKeyword:
  case TokenKind::AbstractKeyword:
    return true;
  default:
    return isStartOfExpression();
  }
}

bool Parser::isStartOfType()
{
  switch (kind()) {
  case TokenKind::NumericLiteral:
  case TokenKind::BigIntLiteral:
  case TokenKind::StringLiteral:
  case TokenKind::NoSubstitutionTemplateLiteral:
  case TokenKind::TemplateHead:
  case TokenKind::BacktickToken:
  case TokenKind::ThisKeyword:
  case TokenKind::VoidKeyword:
  case TokenKind::NullKeyword:
  case TokenKind::TrueKeyword:
  case TokenKind::FalseKeyword:
  case TokenKind::TypeOfKeyword:
  case TokenKind::NewKeyword:
  case TokenKind::ImportKeyword:
  case TokenKind::AbstractKeyword:
  case TokenKind::OpenParenToken:
  case TokenKind::OpenBracketToken:
  case TokenKind::OpenBraceToken:
  case TokenKind::LessThanToken:
  case TokenKind::MinusToken:
  case TokenKind::PipeToken:
  case TokenKind::AmpersandToken:
    return true;
  default:
    return isAlwaysIdentifier(kind());
  }
}

bool Parser::isStartOfPropertyName()
{
  switch (kind()) {
  case TokenKind::StringLiteral:
  case TokenKind::NumericLiteral:
  case TokenKind::BigIntLiteral:
  case TokenKind::PrivateIdentifier:
  case TokenKind::OpenBracketToken:
    return true;
  default:
    // Any IdentifierName, reserved words included, may name a property.
    return isAlwaysIdentifier(kind()) || tokenIsKeyword(kind());
  }
}

bool Parser::isStartOfBinding()
{
  return isAlwaysIdentifier(kind()) || is(TokenKind::OpenBracketToken) ||
         is(TokenKind::OpenBraceToken) || is(TokenKind::PrivateIdentifier);
}

bool Parser::isStartOfParameter()
{
  switch (kind()) {
  case TokenKind::AtToken:
  case TokenKind::DotDotDotToken:
  case TokenKind::PublicKeyword:
  case TokenKind::PrivateKeyword:
  case TokenKind::ProtectedKeyword:
  case TokenKind::ThisKeyword:
    return true;
  default:
    return isStartOfBinding();
  }
}

bool Parser::isStartOfClassMember()
{
  switch (kind()) {
  case TokenKind::AtToken:
  case TokenKind::SemicolonToken:
  case TokenKind::AsteriskToken:
  case TokenKind::OpenBracketToken:
  case TokenKind::OpenParenToken:
  case TokenKind::LessThanToken:
  case TokenKind::PublicKeyword:
  case TokenKind::PrivateKeyword:
  case TokenKind::ProtectedKeyword:
    return true;
  default:
    return isStartOfPropertyName();
  }
}

bool Parser::isStartOfTypeMember()
{
  switch (kind()) {
  case TokenKind::OpenBracketToken:
  case TokenKind::OpenParenToken:
  case TokenKind::LessThanToken:
  case TokenKind::NewKeyword:
    return true;
  default:
    return isStartOfPropertyName();
  }
}

// ---------------------------------------------------------------- sync sets

bool Parser::isListElement(ListKind list)
{
  switch (list) {
  case ListKind::SourceElements:
  case ListKind::BlockStatements:
  case ListKind::SwitchClauseStatements:
    return isStartOfStatement();
  case ListKind::SwitchClauses:
    return is(TokenKind::CaseKeyword) || is(TokenKind::DefaultKeyword);
  case ListKind::ClassMembers:
    return isStartOfClassMember();
  case ListKind::TypeMembers:
    return isStartOfTypeMember();
  case ListKind::EnumMembers:
    return isStartOfPropertyName();
  case ListKind::ObjectLiteralMembers:
    return is(TokenKind::DotDotDotToken) || is(TokenKind::AsteriskToken) ||
           isStartOfPropertyName();
  case ListKind::ArrayLiteralMembers:
    return is(TokenKind::CommaToken) || is(TokenKind::DotDotDotToken) ||
           isStartOfExpression();
  case ListKind::Arguments:
    return is(TokenKind::DotDotDotToken) || isStartOfExpression();
  case ListKind::Parameters:
    return isStartOfParameter();
  case ListKind::TypeParameters:
    return is(TokenKind::ConstKeyword) || is(TokenKind::InKeyword) ||
           isAlwaysIdentifier(kind());
  case ListKind::TypeArguments:
    return isStartOfType();
  case ListKind::TupleElements:
    return is(TokenKind::DotDotDotToken) || isStartOfType();
  case ListKind::ObjectBindingElements:
    return is(TokenKind::DotDotDotToken) || isStartOfPropertyName();
  case ListKind::ArrayBindingElements:
    return is(TokenKind::CommaToken) || is(TokenKind::DotDotDotToken) ||
           isStartOfBinding();
  case ListKind::ImportOrExportSpecifiers:
  case ListKind::ImportAttributes:
    return is(TokenKind::StringLiteral) || isAlwaysIdentifier(kind()) ||
           tokenIsKeyword(kind());
  }
  return false;
}

bool Parser::isListTerminator(ListKind list)
{
  switch (list) {
  case ListKind::SourceElements:
    return false;
  case ListKind::BlockStatements:
  case ListKind::SwitchClauses:
  case ListKind::ClassMembers:
  case ListKind::TypeMembers:
  case ListKind::EnumMembers:
  case ListKind::ObjectLiteralMembers:
  case ListKind::ObjectBindingElements:
  case ListKind::ImportOrExportSpecifiers:
  case ListKind::ImportAttributes:
    return is(TokenKind::CloseBraceToken);
  case ListKind::SwitchClauseStatements:
    return is(TokenKind::CloseBraceToken) || is(TokenKind::CaseKeyword) ||
           is(TokenKind::DefaultKeyword);
  case ListKind::ArrayLiteralMembers:
  case ListKind::TupleElements:
  case ListKind::ArrayBindingElements:
    return is(TokenKind::CloseBracketToken);
  case ListKind::Arguments:
    return is(TokenKind::CloseParenToken) || is(TokenKind::SemicolonToken);
  case ListKind::Parameters:
    return is(TokenKind::CloseParenToken) || is(TokenKind::CloseBracketToken);
  case ListKind::TypeParameters:
    return is(TokenKind::GreaterThanToken) || is(TokenKind::OpenParenToken) ||
           is(TokenKind::OpenBraceToken) || is(TokenKind::ExtendsKeyword) ||
           is(TokenKind::ImplementsKeyword);
  case ListKind::TypeArguments:
    // Only a comma continues a type argument list, so `f<a b` stops at `b`.
    return !is(TokenKind::CommaToken);
  }
  return false;
}

bool Parser::enclosingListAccepts(ListKind list)
{
  for (uint32_t bit = 0; bit < 32; ++bit) {
    if ((m_lists & (1u << bit)) == 0 || bit == uint32_t(list)) {
      continue;
    }
    ListKind outer = ListKind(bit);
    if (isListElement(outer) || isListTerminator(outer)) {
      return true;
    }
  }
  return false;
}

bool Parser::skipOrAbort(ListKind list)
{
  ListError e = listError(list);
  errorAt(token(), e.code, string(e.message));
  if (enclosingListAccepts(list)) {
    return false;
  }
  m_tree->addChild(skipTokenAsError());
  return true;
}

NodeId Parser::skipTokenAsError()
{
  NodeId node = m_tree->beginNode(NodeKind::ErrorNode, pos());
  m_scanner.scanOne();
  return m_tree->endNode(node, pos());
}

} // namespace fastlint::syntax
