#include "fastlint/syntax/tokens.h"

namespace fastlint::syntax {

namespace {

constexpr const char *kNames[] = {
#define FASTLINT_TOKEN(kind, name) name,
#include "fastlint/syntax/tokens.def"
#undef FASTLINT_TOKEN
};

// Text for punctuation and keywords; empty for word-like tokens whose text is
// the source word itself (identifiers, literals).
constexpr const char *kText[] = {
#define FASTLINT_TOKEN(kind, name) name,
#include "fastlint/syntax/tokens.def"
#undef FASTLINT_TOKEN
};

// Words the scanner maps to keyword kinds, sorted by length for the lookup.
struct KeywordEntry {
  std::string_view word;
  TokenKind kind;
};

constexpr KeywordEntry kKeywords[] = {
#define FASTLINT_TOKEN(kind, name) {name, TokenKind::kind},
#include "fastlint/syntax/tokens.def"
#undef FASTLINT_TOKEN
};

} // namespace

const char *tokenName(TokenKind kind)
{
  return kNames[size_t(kind)];
}

const char *tokenText(TokenKind kind)
{
  // Punctuation and operators carry their text in the table directly. The
  // keyword block sits between the literals and the punctuation, so keywords
  // are also covered by the same table.
  return kText[size_t(kind)];
}

bool tokenIsKeyword(TokenKind kind)
{
  return kind >= TokenKind::AbstractKeyword && kind <= TokenKind::YieldKeyword;
}

TokenKind keywordKind(std::string_view word)
{
  // ~100 fixed strings, one lookup per identifier token; replace with a
  // perfect hash only if it ever shows in a profile.
  for (const KeywordEntry &entry : kKeywords) {
    if (entry.word == word && tokenIsKeyword(entry.kind)) {
      return entry.kind;
    }
  }
  return TokenKind::Identifier;
}

} // fastlint::syntax
