#pragma once

// Token kinds table. tokens.def is the single source of truth; this header
// builds the enum and the name/text lookup tables from it.

#include "util/span.h"
#include "util/string.h"

#include <cstdint>
#include <string_view>

namespace fastlint::syntax {

enum class TokenKind : uint8_t {
#define FASTLINT_TOKEN(kind, name) kind,
#include "fastlint/syntax/tokens.def"
#undef FASTLINT_TOKEN
};

/** snake_case name of the kind, for dumps and test output. */
const char *tokenName(TokenKind kind);

/** Punctuation/keyword text, or nullptr for word/literal tokens. */
const char *tokenText(TokenKind kind);

/** True for the keyword kinds, contextual or reserved. */
bool tokenIsKeyword(TokenKind kind);

/**
 * Keyword kind for a word, or `TokenKind::Identifier` if it is not a keyword.
 * Includes every contextual keyword; whether a context may treat the result
 * as an identifier is the parser's call.
 */
TokenKind keywordKind(std::string_view word);

inline bool isPunctuation(TokenKind kind)
{
  // Punctuation kinds are laid out after the keywords in tokens.def.
  return kind >= TokenKind::OpenBraceToken;
}

/** How many token kinds there are (one past the last valid value). */
constexpr int tokenKindCount = 0
#define FASTLINT_TOKEN(kind, name) +1
#include "fastlint/syntax/tokens.def"
#undef FASTLINT_TOKEN
    ;

/** One token of the token array. */
struct Token {
  TokenKind kind = TokenKind::EndOfFile;
  uint32_t offset = 0;
  uint32_t length = 0;
  /** Index into the trivia array of this token's leading run. */
  uint32_t leadingTriviaStart = 0;
  uint32_t leadingTriviaCount = 0;
  /** A newline appears in the token's leading trivia (drives ASI). */
  bool precedingLineBreak = false;
  /** Numeric literal with a legacy octal (`0123`) or string with one (`\01`). */
  bool legacyOctal = false;
  /** Unterminated string/template/regex/comment; the token still ends at EOF or EOL. */
  bool unterminated = false;
};

} // namespace fastlint::syntax
