#pragma once

// Helpers shared between the parser's translation units. Not part of the
// public syntax/ API.

#include "fastlint/syntax/parser.h"

namespace fastlint::syntax::detail {

/** Precedence for the binary/assignment operators, 0 = not an operator. */
inline int binaryPrecedence(TokenKind kind, bool disallowIn)
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
/** Names that are only reserved in strict mode or by context (`let`, `yield`, `public`,
 * …). */
inline bool isBindingIdentifier(TokenKind kind);

inline bool isAlwaysIdentifier(TokenKind kind)
{
  switch (kind) {
  case TokenKind::AccessorKeyword:
  case TokenKind::AsyncKeyword:
  case TokenKind::AwaitKeyword:
  case TokenKind::AnyKeyword:
  case TokenKind::BigIntKeyword:
  case TokenKind::AsKeyword:
  case TokenKind::AssertsKeyword:
  case TokenKind::AssertKeyword:
  case TokenKind::BooleanKeyword:
  case TokenKind::ConstructorKeyword:
  case TokenKind::DeclareKeyword:
  case TokenKind::FromKeyword:
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
  case TokenKind::SatisfiesKeyword:
  case TokenKind::SetKeyword:
  case TokenKind::StaticKeyword:
  case TokenKind::StringKeyword:
  case TokenKind::SymbolKeyword:
  case TokenKind::TypeKeyword:
  case TokenKind::UndefinedKeyword:
  case TokenKind::UniqueKeyword:
  case TokenKind::UnknownKeyword:
  case TokenKind::UsingKeyword:
  case TokenKind::Identifier:
    return true;
  default:
    return false;
  }
}

inline bool isBindingIdentifier(TokenKind kind)
{
  return isAlwaysIdentifier(kind) || kind == TokenKind::LetKeyword ||
         kind == TokenKind::YieldKeyword || kind == TokenKind::AbstractKeyword ||
         kind == TokenKind::PublicKeyword || kind == TokenKind::PrivateKeyword ||
         kind == TokenKind::ProtectedKeyword;
}

/** Sets a context flag for the enclosing scope and restores it on exit. */
struct FlagScope {
  bool &flag;
  bool saved;
  FlagScope(bool &target, bool value) : flag(target), saved(target)
  {
    flag = value;
  }
  ~FlagScope()
  {
    flag = saved;
  }
  FlagScope(const FlagScope &) = delete;
  FlagScope &operator=(const FlagScope &) = delete;
};

} // namespace fastlint::syntax::detail

namespace fastlint::syntax {

// Both loops test for an element before a terminator, so a list whose
// terminator set is "anything else" (type arguments) still parses elements.
template <typename Fn> void Parser::parseList(ListKind list, Fn &&element)
{
  uint32_t savedLists = m_lists;
  m_lists |= 1u << uint32_t(list);
  while (!is(TokenKind::EndOfFile)) {
    uint32_t before = pos();
    if (isListElement(list)) {
      m_tree->addChild(element());
      if (pos() != before) {
        continue;
      }
    }
    if (isListTerminator(list) || !skipOrAbort(list)) {
      break;
    }
  }
  m_lists = savedLists;
}

template <typename Fn> void Parser::parseDelimitedList(ListKind list, Fn &&element)
{
  uint32_t savedLists = m_lists;
  m_lists |= 1u << uint32_t(list);
  while (!is(TokenKind::EndOfFile)) {
    uint32_t before = pos();
    if (isListElement(list)) {
      m_tree->addChild(element());
      if (eat(TokenKind::CommaToken)) {
        continue;
      }
      if (isListTerminator(list)) {
        break;
      }
      if (pos() != before) {
        errorAt(token(), 1005, litestl::util::string("',' expected."));
        continue;
      }
    }
    if (isListTerminator(list) || !skipOrAbort(list)) {
      break;
    }
  }
  m_lists = savedLists;
}

} // namespace fastlint::syntax
