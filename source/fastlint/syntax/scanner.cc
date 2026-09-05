#include "fastlint/syntax/scanner.h"

#include "fastlint/syntax/unicode.h"
#include "util/string.h"
#include <algorithm>

namespace fastlint::syntax {

using litestl::util::string;

namespace {

// TS diagnostic codes we reuse so our diagnostics stay comparable with tsgo's.
constexpr uint32_t kCodeUnterminatedString = 1002;
constexpr uint32_t kCodeUnterminatedTemplate = 1160;
constexpr uint32_t kCodeInvalidCharacter = 1127;
constexpr uint32_t kCodeUnicodeEscape = 1126;
constexpr uint32_t kCodeConflictMarker = 1402;
constexpr uint32_t kCodeIdentifierAfterNumber = 1499;
constexpr uint32_t kCodeJsxUnexpectedToken = 1381;
constexpr uint32_t kCodeCommentNotTerminated = 0;
constexpr uint32_t kCodeSeparator = 0;

bool isDigit(char c)
{
  return c >= '0' && c <= '9';
}
bool isHexDigit(char c)
{
  return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
int hexValue(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return c - 'A' + 10;
}
bool isAsciiIdentifierStart(uint32_t c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '$' || c == '_';
}
bool isAsciiIdentifierPart(uint32_t c)
{
  return isAsciiIdentifierStart(c) || (c >= '0' && c <= '9');
}

} // namespace

Scanner::Scanner(std::string_view source,
                 const Options &options,
                 Diagnostics &diagnostics)
    : m_source(source), m_options(options), m_diagnostics(&diagnostics)
{
  m_lineStarts.append(0);
}

uint32_t Scanner::peekChar() const
{
  if (atEnd())
    return 0;
  unsigned char c = uint8_t(byte(m_pos));
  if (c < 0x80)
    return c;
  size_t len = charLength();
  uint32_t cp = 0;
  switch (len) {
  case 2:
    cp = c & 0x1f;
    break;
  case 3:
    cp = c & 0x0f;
    break;
  case 4:
    cp = c & 0x07;
    break;
  default:
    return 0xfffd;
  }
  for (size_t i = 1; i < len && m_pos + i < m_source.size(); ++i) {
    cp = (cp << 6) | (uint32_t(uint8_t(byte(m_pos + i))) & 0x3f);
  }
  return cp;
}

uint32_t Scanner::peekCharAt(size_t ahead) const
{
  size_t pos = m_pos + ahead;
  if (pos >= m_source.size())
    return 0;
  unsigned char c = uint8_t(byte(pos));
  if (c < 0x80)
    return c;
  size_t len = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
  uint32_t cp = 0;
  switch (len) {
  case 2:
    cp = c & 0x1f;
    break;
  case 3:
    cp = c & 0x0f;
    break;
  case 4:
    cp = c & 0x07;
    break;
  default:
    return 0xfffd;
  }
  for (size_t i = 1; i < len && pos + i < m_source.size(); ++i) {
    cp = (cp << 6) | (uint32_t(uint8_t(byte(pos + i))) & 0x3f);
  }
  return cp;
}

size_t Scanner::charLength() const
{
  unsigned char c = uint8_t(byte(m_pos));
  if (c < 0x80)
    return 1;
  if (c >= 0xf0)
    return 4;
  if (c >= 0xe0)
    return 3;
  if (c >= 0xc0)
    return 2;
  return 1;
}

void Scanner::advanceChar()
{
  // A multibyte sequence cut off by the end of the file still ends there.
  m_pos = std::min(m_pos + charLength(), m_source.size());
}

void Scanner::error(uint32_t code, uint32_t offset, uint32_t length, string message)
{
  m_diagnostics->report(code, offset, length, std::move(message));
}

void Scanner::addTrivia(Trivia::Kind kind,
                        uint32_t offset,
                        uint32_t length,
                        bool lineBreak)
{
  Trivia trivia;
  trivia.kind = kind;
  trivia.offset = offset;
  trivia.length = length;
  trivia.lineBreak = lineBreak;
  m_trivia.append(trivia);
}

void Scanner::addToken(TokenKind kind, uint32_t offset, bool unterminated)
{
  m_current.kind = kind;
  m_current.offset = offset;
  m_current.length = uint32_t(m_pos - offset);
  m_current.unterminated = unterminated;
  // Trivia bookkeeping is finished by finishCurrent().
}

void Scanner::finishCurrent(uint32_t index)
{
  m_current.leadingTriviaStart = m_lastTriviaEnd;
  uint32_t count = uint32_t(m_trivia.size()) - m_lastTriviaEnd;
  // A trailing whitespace run is not part of a token's leading trivia; only
  // comments and newlines are.
  while (count > 0 &&
         m_trivia[m_lastTriviaEnd + count - 1].kind == Trivia::Kind::Whitespace)
    count--;
  m_current.leadingTriviaCount = count;
  m_current.precedingLineBreak = m_sawLineBreak;
  m_lastTriviaEnd = uint32_t(m_trivia.size());
  m_sawLineBreak = false;
  m_currentIndex = index;
  if (m_tokens.size() == index) {
    m_tokens.append(m_current);
  } else {
    m_tokens[index] = m_current;
  }
}

void Scanner::commit()
{
  finishCurrent(m_nextIndex);
  m_nextIndex = uint32_t(m_tokens.size());
}

void Scanner::scanOne()
{
  // Scanning past EOF keeps the same EOF token rather than appending another.
  if (m_currentIndex != kNoTokenIndex && m_current.kind == TokenKind::EndOfFile) {
    return;
  }
  m_current = Token{};
  m_gtRemaining = 0;
  uint32_t start = uint32_t(m_pos);
  switch (m_mode) {
  case ScanMode::TemplateMiddle:
  case ScanMode::TemplateTail:
    // Template and JSX continuations own their text, whitespace included.
    scanTemplate(start, m_mode);
    commit();
    return;
  case ScanMode::JsxText:
    scanJsxText(start);
    commit();
    return;
  case ScanMode::JsxIdentifier:
    scanJsxIdentifier(start);
    commit();
    return;
  case ScanMode::SingleGreaterThan:
    if (startsWith(">")) {
      m_pos += 1;
      addToken(TokenKind::GreaterThanToken, start);
      commit();
      return;
    }
    [[fallthrough]];
  case ScanMode::Normal:
    break;
  }

  bool sawLineBreak = false;
  skipTrivia(sawLineBreak);
  m_sawLineBreak = m_sawLineBreak || sawLineBreak;
  start = uint32_t(m_pos);

  if (atEnd()) {
    addToken(TokenKind::EndOfFile, start);
    commit();
    return;
  }

  char c = byte(m_pos);
  switch (c) {
  case '#':
    if (m_pos + 1 < m_source.size() &&
        (isAsciiIdentifierStart(uint32_t(byte(m_pos + 1))) || byte(m_pos + 1) == '\\'))
    {
      m_pos += 1;
      scanIdentifier(start, true);
    } else {
      m_pos += 1;
      addToken(TokenKind::HashToken, start);
    }
    break;
  case '"':
  case '\'':
    scanString(start);
    break;
  case '`':
    m_pos += 1;
    scanTemplate(start, ScanMode::Normal);
    break;
  default:
    if (c >= '0' && c <= '9') {
      scanNumber(start);
    } else if (c == '\\' || isAsciiIdentifierStart(uint32_t(c)) || uint32_t(c) >= 0x80) {
      scanIdentifier(start, false);
    } else {
      scanPunctuation(start);
    }
    break;
  }
  commit();
}

/** ES WhiteSpace beyond ASCII: NBSP, ZWNBSP (the BOM) and the Zs category. */
static bool isUnicodeWhiteSpace(uint32_t cp)
{
  switch (cp) {
  case 0x0085: // NEL, single-line whitespace as in tsgo
  case 0x00a0:
  case 0x1680:
  case 0x202f:
  case 0x205f:
  case 0x3000:
  case 0xfeff:
    return true;
  default:
    return cp >= 0x2000 && cp <= 0x200a;
  }
}

void Scanner::skipTrivia(bool &sawLineBreak)
{
  if (m_pos == 0 && startsWith("#!")) {
    while (!atEnd() && byte(m_pos) != '\n' && byte(m_pos) != '\r') {
      m_pos += 1;
    }
    addTrivia(Trivia::Kind::Shebang, 0, uint32_t(m_pos), false);
  }
  for (;;) {
    if (atEnd())
      return;
    char c = byte(m_pos);
    if (c == ' ' || c == '\t' || c == '\f' || c == '\v') {
      uint32_t start = uint32_t(m_pos);
      while (!atEnd()) {
        char w = byte(m_pos);
        if (w == ' ' || w == '\t' || w == '\f' || w == '\v') {
          m_pos += 1;
        } else {
          break;
        }
      }
      addTrivia(Trivia::Kind::Whitespace, start, uint32_t(m_pos - start), false);
      continue;
    }
    if (c == '\r' || c == '\n') {
      uint32_t start = uint32_t(m_pos);
      if (c == '\r' && m_pos + 1 < m_source.size() && byte(m_pos + 1) == '\n') {
        m_pos += 2;
      } else {
        m_pos += 1;
      }
      sawLineBreak = true;
      m_sawLineBreak = true;
      m_lineStarts.append(start + 1);
      addTrivia(Trivia::Kind::NewLine, start, uint32_t(m_pos - start), true);
      continue;
    }
    uint32_t cp = peekChar();
    if (cp == 0x2028 || cp == 0x2029) {
      uint32_t start = uint32_t(m_pos);
      advanceChar();
      sawLineBreak = true;
      m_sawLineBreak = true;
      m_lineStarts.append(start + 1);
      addTrivia(Trivia::Kind::NewLine, start, uint32_t(m_pos - start), true);
      continue;
    }
    if (isUnicodeWhiteSpace(cp)) {
      uint32_t start = uint32_t(m_pos);
      while (!atEnd() && isUnicodeWhiteSpace(peekChar())) {
        advanceChar();
      }
      addTrivia(Trivia::Kind::Whitespace, start, uint32_t(m_pos - start), false);
      continue;
    }
    if (c == '/' && m_pos + 1 < m_source.size() && byte(m_pos + 1) == '/') {
      uint32_t start = uint32_t(m_pos);
      while (!atEnd() && byte(m_pos) != '\n' && byte(m_pos) != '\r') {
        m_pos += 1;
      }
      addTrivia(Trivia::Kind::SingleLineComment, start, uint32_t(m_pos - start), false);
      continue;
    }
    if (c == '/' && m_pos + 1 < m_source.size() && byte(m_pos + 1) == '*') {
      uint32_t start = uint32_t(m_pos);
      m_pos += 2;
      bool closed = false;
      bool broke = false;
      while (!atEnd()) {
        if (byte(m_pos) == '\n' || byte(m_pos) == '\r' || peekChar() == 0x2028 ||
            peekChar() == 0x2029)
        {
          broke = true;
          sawLineBreak = true;
          m_sawLineBreak = true;
        }
        if (byte(m_pos) == '*' && m_pos + 1 < m_source.size() && byte(m_pos + 1) == '/') {
          m_pos += 2;
          closed = true;
          break;
        }
        if (byte(m_pos) == '\n') {
          m_lineStarts.append(uint32_t(m_pos) + 1);
        }
        m_pos += 1;
      }
      if (!closed) {
        error(kCodeCommentNotTerminated,
              start,
              uint32_t(m_pos - start),
              string("Comment not terminated."));
      }
      addTrivia(Trivia::Kind::MultiLineComment, start, uint32_t(m_pos - start), broke);
      continue;
    }
    if (m_options.javaScript && startsWith("<!--")) {
      uint32_t start = uint32_t(m_pos);
      while (!atEnd() && byte(m_pos) != '\n' && byte(m_pos) != '\r') {
        m_pos += 1;
      }
      addTrivia(Trivia::Kind::SingleLineComment, start, uint32_t(m_pos - start), false);
      continue;
    }
    if (startsWith("<<<<<<<") || startsWith("=======") || startsWith(">>>>>>>")) {
      uint32_t start = uint32_t(m_pos);
      while (!atEnd() && byte(m_pos) != '\n' && byte(m_pos) != '\r') {
        m_pos += 1;
      }
      error(kCodeConflictMarker,
            start,
            uint32_t(m_pos - start),
            string("File appears to have a merge conflict marker."));
      addTrivia(Trivia::Kind::Whitespace, start, uint32_t(m_pos - start), false);
      continue;
    }
    return;
  }
}

void Scanner::scanIdentifierChars()
{
  for (;;) {
    if (atEnd())
      break;
    uint32_t cp = peekChar();
    if (isAsciiIdentifierPart(cp) || cp == '$' || cp == '_') {
      advanceChar();
      continue;
    }
    if (cp == '\\') {
      size_t escapePos = m_pos;
      m_pos += 2; // backslash + 'u'
      if (escapePos + 1 >= m_source.size() || byte(escapePos + 1) != 'u') {
        m_pos = escapePos;
        break;
      }
      if (!atEnd() && byte(m_pos) == '{') {
        // \u{...}
        m_pos += 1;
        uint32_t value = 0;
        bool any = false;
        bool bad = false;
        while (!atEnd() && byte(m_pos) != '}') {
          if (!isHexDigit(byte(m_pos))) {
            bad = true;
            break;
          }
          value = value * 16 + uint32_t(hexValue(byte(m_pos)));
          m_pos += 1;
          any = true;
        }
        if (!atEnd() && byte(m_pos) == '}')
          m_pos += 1;
        if (bad || !any || value > 0x10ffff) {
          error(kCodeUnicodeEscape,
                uint32_t(escapePos),
                2,
                string("Invalid Unicode escape sequence"));
        }
        continue;
      }
      bool ok = true;
      for (int i = 0; i < 4; ++i) {
        if (atEnd() || !isHexDigit(byte(m_pos))) {
          ok = false;
          break;
        }
        m_pos += 1;
      }
      if (!ok) {
        error(kCodeUnicodeEscape,
              uint32_t(escapePos),
              2,
              string("Invalid Unicode escape sequence"));
      }
      continue;
    }
    if (cp >= 0x80 && (idStartContains(cp) || idContinueContains(cp))) {
      advanceChar();
      continue;
    }
    break;
  }
}

void Scanner::scanIdentifier(uint32_t start, bool isPrivate)
{
  scanIdentifierChars();

  // A non-ASCII character that cannot start a name; every token must
  // consume something or the parser never advances.
  if (m_pos == start) {
    advanceChar();
    error(kCodeInvalidCharacter,
          start,
          uint32_t(m_pos - start),
          string("Invalid character."));
    addToken(TokenKind::Identifier, start);
    return;
  }

  if (isPrivate) {
    addToken(TokenKind::PrivateIdentifier, start);
    return;
  }
  std::string_view word = m_source.substr(start, m_pos - start);
  addToken(keywordKind(word), start);
}

void Scanner::scanNumber(uint32_t start)
{
  bool isBigInt = false;
  char first = byte(m_pos);

  // Scans a digit run with separators; `pred` decides what a digit is.
  auto scanDigits = [&](bool (*pred)(char), bool &any) {
    any = false;
    bool lastWasSeparator = false;
    while (!atEnd()) {
      char d = byte(m_pos);
      if (d == '_') {
        if (lastWasSeparator) {
          error(kCodeSeparator,
                uint32_t(m_pos),
                1,
                string("Numeric separators are not allowed here."));
        }
        lastWasSeparator = true;
        m_pos += 1;
        if (atEnd() || !pred(byte(m_pos))) {
          error(kCodeSeparator,
                uint32_t(m_pos) - 1,
                1,
                string("Numeric separators are not allowed here."));
        }
        continue;
      }
      lastWasSeparator = false;
      if (pred(d)) {
        m_pos += 1;
        any = true;
      } else {
        break;
      }
    }
  };

  if (first == '0' && m_pos + 1 < m_source.size()) {
    char next = byte(m_pos + 1);
    if (next == 'x' || next == 'X') {
      m_pos += 2;
      bool any = false;
      scanDigits(isHexDigit, any);
      if (!any)
        error(1125, start, 2, string("Hexadecimal digit expected."));
    } else if (next == 'o' || next == 'O') {
      m_pos += 2;
      bool any = false;
      scanDigits([](char c) { return c >= '0' && c <= '7'; }, any);
      if (!any)
        error(1178, start, 2, string("Octal digit expected."));
    } else if (next == 'b' || next == 'B') {
      m_pos += 2;
      bool any = false;
      scanDigits([](char c) { return c == '0' || c == '1'; }, any);
      if (!any)
        error(1177, start, 2, string("Binary digit expected."));
    } else if (next >= '0' && next <= '9') {
      // Legacy octal, or a legacy decimal such as 08.
      bool any = false;
      scanDigits([](char c) { return c >= '0' && c <= '7'; }, any);
      if (!atEnd() && (byte(m_pos) == '8' || byte(m_pos) == '9')) {
        while (!atEnd() && isDigit(byte(m_pos)))
          m_pos += 1;
      } else {
        m_current.legacyOctal = true;
      }
    } else {
      // Plain 0 falls through to the decimal path below for fractions.
      bool any = false;
      scanDigits([](char c) { return c >= '0' && c <= '9'; }, any);
    }
  } else {
    bool any = false;
    scanDigits([](char c) { return c >= '0' && c <= '9'; }, any);
  }

  if (!atEnd() && byte(m_pos) == '.') {
    if (m_pos + 1 < m_source.size() && isDigit(byte(m_pos + 1))) {
      m_pos += 1;
      bool any = false;
      scanDigits([](char c) { return c >= '0' && c <= '9'; }, any);
    }
  }

  if (!atEnd() && (byte(m_pos) == 'e' || byte(m_pos) == 'E')) {
    size_t save = m_pos;
    m_pos += 1;
    if (!atEnd() && (byte(m_pos) == '+' || byte(m_pos) == '-'))
      m_pos += 1;
    bool any = false;
    scanDigits([](char c) { return c >= '0' && c <= '9'; }, any);
    if (!any)
      m_pos = save;
  }

  if (!atEnd() && byte(m_pos) == 'n') {
    m_pos += 1;
    isBigInt = true;
  }

  if (!atEnd() && (isAsciiIdentifierStart(uint32_t(byte(m_pos))) || byte(m_pos) == '\\'))
  {
    error(kCodeIdentifierAfterNumber,
          start,
          uint32_t(m_pos - start),
          string("An identifier cannot follow a number."));
    // The number ends here; the identifier becomes its own token so the
    // token stream stays well-formed.
    addToken(isBigInt ? TokenKind::BigIntLiteral : TokenKind::NumericLiteral, start);
    commit();
    scanIdentifier(uint32_t(m_pos), false);
    return;
  }

  addToken(isBigInt ? TokenKind::BigIntLiteral : TokenKind::NumericLiteral, start);
}

void Scanner::scanString(uint32_t start)
{
  char quote = byte(m_pos);
  m_pos += 1;
  bool terminated = false;
  for (;;) {
    if (atEnd() || byte(m_pos) == '\n' || byte(m_pos) == '\r') {
      error(kCodeUnterminatedString,
            start,
            uint32_t(m_pos - start),
            string("Unterminated string constant."));
      break;
    }
    char c = byte(m_pos);
    if (c == quote) {
      m_pos += 1;
      terminated = true;
      break;
    }
    if (c == '\\') {
      m_pos += 1;
      if (atEnd()) {
        error(kCodeUnterminatedString,
              start,
              uint32_t(m_pos - start),
              string("Unterminated string constant."));
        break;
      }
      terminated = false;
      char e = byte(m_pos);
      if (e == '\r') {
        uint32_t br = uint32_t(m_pos);
        m_pos += 1;
        if (!atEnd() && byte(m_pos) == '\n') {
          m_pos += 1;
        }
        m_lineStarts.append(br + 1);
        m_sawLineBreak = true;
        continue;
      }
      if (e == '\n') {
        m_lineStarts.append(uint32_t(m_pos) + 1);
        m_pos += 1;
        m_sawLineBreak = true;
        continue;
      }
      if (e == 'x') {
        m_pos += 1;
        for (int i = 0; i < 2; ++i) {
          if (atEnd() || !isHexDigit(byte(m_pos))) {
            error(1125,
                  start,
                  uint32_t(m_pos - start),
                  string("Hexadecimal digit expected."));
            break;
          }
          m_pos += 1;
        }
        continue;
      }
      if (e == 'u') {
        m_pos += 1;
        if (!atEnd() && byte(m_pos) == '{') {
          m_pos += 1;
          bool any = false;
          while (!atEnd() && byte(m_pos) != '}') {
            if (!isHexDigit(byte(m_pos))) {
              error(kCodeUnicodeEscape,
                    start,
                    uint32_t(m_pos - start),
                    string("Invalid Unicode escape sequence"));
              break;
            }
            m_pos += 1;
            any = true;
          }
          if (!atEnd() && byte(m_pos) == '}')
            m_pos += 1;
          if (!any) {
            error(kCodeUnicodeEscape,
                  start,
                  uint32_t(m_pos - start),
                  string("Invalid Unicode escape sequence"));
          }
          continue;
        }
        for (int i = 0; i < 4; ++i) {
          if (atEnd() || !isHexDigit(byte(m_pos))) {
            error(kCodeUnicodeEscape,
                  start,
                  uint32_t(m_pos - start),
                  string("Invalid Unicode escape sequence"));
            break;
          }
          m_pos += 1;
        }
        continue;
      }
      if (e >= '0' && e <= '7') {
        m_current.legacyOctal = true;
        m_pos += 1;
        if (e != '0') {
          for (int i = 0; i < 2 && !atEnd() && byte(m_pos) >= '0' && byte(m_pos) <= '7';
               ++i)
          {
            m_pos += 1;
          }
        }
        continue;
      }
      m_pos += 1;
      continue;
    }
    m_pos += 1;
  }
  addToken(TokenKind::StringLiteral, start, !terminated);
}

void Scanner::scanTemplate(uint32_t start, ScanMode mode)
{
  // In middle/tail mode the token starts at the `}` that closed the
  // substitution; consume it, since the token includes it (as in TS).
  if (mode != ScanMode::Normal) {
    m_pos += 1;
  }
  for (;;) {
    if (atEnd()) {
      error(kCodeUnterminatedTemplate,
            start,
            uint32_t(m_pos - start),
            string("Unterminated template literal."));
      TokenKind kind = mode == ScanMode::Normal ? TokenKind::NoSubstitutionTemplateLiteral
                                                : TokenKind::TemplateInvalid;
      addToken(kind, start, true);
      return;
    }
    char c = byte(m_pos);
    if (c == '`') {
      m_pos += 1;
      if (mode == ScanMode::Normal) {
        addToken(TokenKind::NoSubstitutionTemplateLiteral, start);
      } else {
        addToken(mode == ScanMode::TemplateTail ? TokenKind::TemplateTail
                                                : TokenKind::TemplateMiddle,
                 start);
      }
      return;
    }
    if (c == '\\' && m_pos + 1 < m_source.size()) {
      m_pos += 2;
      continue;
    }
    if (c == '$' && m_pos + 1 < m_source.size() && byte(m_pos + 1) == '{') {
      m_pos += 2;
      addToken(mode == ScanMode::Normal ? TokenKind::TemplateHead
                                        : TokenKind::TemplateMiddle,
               start);
      return;
    }
    if (c == '\n') {
      m_lineStarts.append(uint32_t(m_pos) + 1);
      m_sawLineBreak = true;
    } else if (c == '\r') {
      m_lineStarts.append(uint32_t(m_pos) + 1);
      m_sawLineBreak = true;
      if (m_pos + 1 < m_source.size() && byte(m_pos + 1) == '\n') {
        m_pos += 1;
      }
    }
    m_pos += 1;
  }
}

bool Scanner::scanRegex(uint32_t) /* start */
{
  m_pos += 1; // leading slash
  bool inClass = false;
  for (;;) {
    if (atEnd() || byte(m_pos) == '\n' || byte(m_pos) == '\r' || peekChar() == 0x2028 ||
        peekChar() == 0x2029)
    {
      return false; // division, not a regex
    }
    char c = byte(m_pos);
    if (c == '\\') {
      m_pos += 2;
      continue;
    }
    if (c == '[') {
      inClass = true;
    } else if (c == ']') {
      inClass = false;
    } else if (c == '/' && !inClass) {
      m_pos += 1;
      while (!atEnd() && isAsciiIdentifierPart(uint32_t(byte(m_pos)))) {
        m_pos += 1;
      }
      return true;
    }
    m_pos += 1;
  }
}

void Scanner::scanJsxText(uint32_t start)
{
  if (atEnd()) {
    addToken(TokenKind::EndOfFile, start);
    return;
  }
  while (!atEnd() && byte(m_pos) != '<' && byte(m_pos) != '{') {
    char c = byte(m_pos);
    if (c == '\n') {
      m_lineStarts.append(uint32_t(m_pos) + 1);
      m_sawLineBreak = true;
    } else if (c == '}' || c == '>') {
      // Text as in tsgo, but the character must be written as an entity.
      error(kCodeJsxUnexpectedToken,
            uint32_t(m_pos),
            1,
            string(c == '}' ? "Unexpected token. Did you mean `{'}'}` or `&rbrace;`?"
                            : "Unexpected token. Did you mean `{'>'}` or `&gt;`?"));
    }
    m_pos += 1;
  }
  addToken(TokenKind::JsxText, start);
}

void Scanner::scanJsxIdentifier(uint32_t start)
{
  // Identifier segments joined by `-`; escapes scan as in plain identifiers.
  for (;;) {
    scanIdentifierChars();
    if (!atEnd() && byte(m_pos) == '-') {
      m_pos += 1;
      continue;
    }
    break;
  }
  if (m_pos == start) {
    advanceChar();
    error(kCodeInvalidCharacter,
          start,
          uint32_t(m_pos - start),
          string("Invalid character."));
  }
  addToken(TokenKind::JsxIdentifier, start);
}

void Scanner::scanPunctuation(uint32_t start)
{
  auto tryMatch = [&](std::string_view text, TokenKind kind) -> bool {
    if (startsWith(text)) {
      m_pos += text.size();
      addToken(kind, start);
      return true;
    }
    return false;
  };

  char c = byte(m_pos);
  switch (c) {
  case '{':
    m_pos += 1;
    addToken(TokenKind::OpenBraceToken, start);
    return;
  case '}':
    m_pos += 1;
    addToken(TokenKind::CloseBraceToken, start);
    return;
  case '(':
    m_pos += 1;
    addToken(TokenKind::OpenParenToken, start);
    return;
  case ')':
    m_pos += 1;
    addToken(TokenKind::CloseParenToken, start);
    return;
  case '[':
    m_pos += 1;
    addToken(TokenKind::OpenBracketToken, start);
    return;
  case ']':
    m_pos += 1;
    addToken(TokenKind::CloseBracketToken, start);
    return;
  case ';':
    m_pos += 1;
    addToken(TokenKind::SemicolonToken, start);
    return;
  case ',':
    m_pos += 1;
    addToken(TokenKind::CommaToken, start);
    return;
  case '~':
    m_pos += 1;
    addToken(TokenKind::TildeToken, start);
    return;
  case '@':
    m_pos += 1;
    addToken(TokenKind::AtToken, start);
    return;
  case ':':
    if (tryMatch("::", TokenKind::ColonColonToken))
      return;
    m_pos += 1;
    addToken(TokenKind::ColonToken, start);
    return;
  case '.':
    if (tryMatch("...", TokenKind::DotDotDotToken))
      return;
    if (m_pos + 1 < m_source.size() && isDigit(byte(m_pos + 1))) {
      scanNumber(start);
      return;
    }
    m_pos += 1;
    addToken(TokenKind::DotToken, start);
    return;
  case '?':
    if (tryMatch("\?\?=", TokenKind::QuestionQuestionEqualsToken))
      return;
    if (startsWith("?.")) {
      // `a?.1:x` is a ternary with a dot, not optional chaining.
      if (m_pos + 2 < m_source.size() && isDigit(byte(m_pos + 2))) {
        m_pos += 1;
        addToken(TokenKind::QuestionToken, start);
        return;
      }
      m_pos += 2;
      addToken(TokenKind::QuestionDotToken, start);
      return;
    }
    if (tryMatch("??", TokenKind::QuestionQuestionToken))
      return;
    m_pos += 1;
    addToken(TokenKind::QuestionToken, start);
    return;
  case '<':
    if (tryMatch("<<=", TokenKind::LessThanLessThanEqualsToken))
      return;
    if (tryMatch("<<", TokenKind::LessThanLessThanToken))
      return;
    if (tryMatch("<=", TokenKind::LessThanEqualsToken))
      return;
    m_pos += 1;
    addToken(TokenKind::LessThanToken, start);
    return;
  case '>':
    if (tryMatch(">>>=", TokenKind::GreaterThanGreaterThanGreaterThanEqualsToken))
      return;
    if (tryMatch(">>>", TokenKind::GreaterThanGreaterThanGreaterThanToken))
      return;
    if (tryMatch(">>=", TokenKind::GreaterThanGreaterThanEqualsToken))
      return;
    if (tryMatch(">>", TokenKind::GreaterThanGreaterThanToken))
      return;
    if (tryMatch(">=", TokenKind::GreaterThanEqualsToken))
      return;
    m_pos += 1;
    addToken(TokenKind::GreaterThanToken, start);
    return;
  case '=':
    if (tryMatch("===", TokenKind::EqualsEqualsEqualsToken))
      return;
    if (tryMatch("=>", TokenKind::EqualsGreaterThanToken))
      return;
    if (tryMatch("==", TokenKind::EqualsEqualsToken))
      return;
    m_pos += 1;
    addToken(TokenKind::EqualsToken, start);
    return;
  case '!':
    if (tryMatch("!==", TokenKind::ExclamationEqualsEqualsToken))
      return;
    if (tryMatch("!=", TokenKind::ExclamationEqualsToken))
      return;
    m_pos += 1;
    addToken(TokenKind::ExclamationToken, start);
    return;
  case '+':
    if (tryMatch("+=", TokenKind::PlusEqualsToken))
      return;
    if (tryMatch("++", TokenKind::PlusPlusToken))
      return;
    m_pos += 1;
    addToken(TokenKind::PlusToken, start);
    return;
  case '-':
    if (tryMatch("-=", TokenKind::MinusEqualsToken))
      return;
    if (tryMatch("--", TokenKind::MinusMinusToken))
      return;
    m_pos += 1;
    addToken(TokenKind::MinusToken, start);
    return;
  case '*':
    if (tryMatch("**=", TokenKind::AsteriskAsteriskEqualsToken))
      return;
    if (tryMatch("**", TokenKind::AsteriskAsteriskToken))
      return;
    if (tryMatch("*=", TokenKind::AsteriskEqualsToken))
      return;
    m_pos += 1;
    addToken(TokenKind::AsteriskToken, start);
    return;
  case '/':
    if (tryMatch("/=", TokenKind::SlashEqualsToken))
      return;
    m_pos += 1;
    addToken(TokenKind::SlashToken, start);
    return;
  case '%':
    if (tryMatch("%=", TokenKind::PercentEqualsToken))
      return;
    m_pos += 1;
    addToken(TokenKind::PercentToken, start);
    return;
  case '&':
    if (tryMatch("&&=", TokenKind::AmpersandAmpersandEqualsToken))
      return;
    if (tryMatch("&&", TokenKind::AmpersandAmpersandToken))
      return;
    if (tryMatch("&=", TokenKind::AmpersandEqualsToken))
      return;
    m_pos += 1;
    addToken(TokenKind::AmpersandToken, start);
    return;
  case '|':
    if (tryMatch("||=", TokenKind::PipePipeEqualsToken))
      return;
    if (tryMatch("||", TokenKind::PipePipeToken))
      return;
    if (tryMatch("|=", TokenKind::PipeEqualsToken))
      return;
    m_pos += 1;
    addToken(TokenKind::PipeToken, start);
    return;
  case '^':
    if (tryMatch("^=", TokenKind::CaretEqualsToken))
      return;
    m_pos += 1;
    addToken(TokenKind::CaretToken, start);
    return;
  default:
    // Unknown character: one diagnostic, one broken token.
    advanceChar();
    error(kCodeInvalidCharacter,
          start,
          uint32_t(m_pos - start),
          string("Invalid character."));
    addToken(TokenKind::Identifier, start);
    return;
  }
}

// ---------------------------------------------------------------- rescans

bool Scanner::rescanSlash()
{
  if (m_current.kind != TokenKind::SlashToken &&
      m_current.kind != TokenKind::SlashEqualsToken)
  {
    return false;
  }
  uint32_t offset = m_current.offset;
  size_t savePos = m_pos;
  uint32_t saveDiags = uint32_t(m_diagnostics->size());
  m_pos = offset;
  bool ok = scanRegex(offset);
  if (!ok) {
    m_diagnostics->resize(saveDiags);
    m_pos = savePos;
    return false;
  }
  addToken(TokenKind::RegularExpressionLiteral, offset);
  m_tokens[m_currentIndex] = m_current;
  return true;
}

bool Scanner::rescanGreaterThan()
{
  if (m_gtRemaining == 0) {
    if (!isPunctuation(m_current.kind))
      return false;
    std::string_view t = text(m_current);
    uint32_t n = 0;
    while (n < t.size() && t[n] == '>')
      n++;
    if (n < 2)
      return false;
    m_gtBase = m_current.offset;
    m_gtRemaining = n;
  }
  m_pos = m_gtBase + 1;
  addToken(TokenKind::GreaterThanToken, m_gtBase);
  m_tokens[m_currentIndex] = m_current;
  m_gtBase += 1;
  m_gtRemaining -= 1;
  return true;
}

void Scanner::rescanTemplateTail(bool tail)
{
  uint32_t offset = m_current.offset;
  m_pos = offset;
  scanTemplate(offset, tail ? ScanMode::TemplateTail : ScanMode::TemplateMiddle);
  m_tokens[m_currentIndex] = m_current;
}

void Scanner::rescanJsxText()
{
  uint32_t offset = m_current.offset;
  m_pos = offset;
  scanJsxText(offset);
  m_tokens[m_currentIndex] = m_current;
}

void Scanner::rescanJsxIdentifier()
{
  uint32_t offset = m_current.offset;
  m_pos = offset;
  scanJsxIdentifier(offset);
  m_tokens[m_currentIndex] = m_current;
}

void Scanner::rescanJsxAttributeString()
{
  uint32_t offset = m_current.offset;
  m_pos = offset;
  // The first scan stopped at a line break and reported the string unterminated.
  while (m_diagnostics->size() > 0 &&
         m_diagnostics->items()[int(m_diagnostics->size()) - 1].offset >= offset)
  {
    m_diagnostics->resize(m_diagnostics->size() - 1);
  }
  char quote = byte(m_pos);
  m_pos += 1;
  bool terminated = false;
  while (!atEnd()) {
    char c = byte(m_pos);
    if (c == '\n') {
      m_lineStarts.append(uint32_t(m_pos) + 1);
    }
    m_pos += 1;
    if (c == quote) {
      terminated = true;
      break;
    }
  }
  if (!terminated) {
    error(kCodeUnterminatedString,
          offset,
          uint32_t(m_pos - offset),
          string("Unterminated string constant."));
  }
  addToken(TokenKind::StringLiteral, offset, !terminated);
  m_tokens[m_currentIndex] = m_current;
}

// ---------------------------------------------------------------- scanAll

void Scanner::scanAll(GrammarTree &tree)
{
  scanOne();
  while (m_current.kind != TokenKind::EndOfFile) {
    // A `}` closing a template substitution is a CloseBrace token, and the
    // template continuation (}`…${` or }`…`) is scanned right after it, the
    // way the parser would after a rescanTemplateTail() — both tokens kept.
    if (m_current.kind == TokenKind::CloseBraceToken && m_templateDepth > 0) {
      uint32_t offset = m_current.offset;
      m_pos = offset;
      scanTemplate(offset, ScanMode::TemplateMiddle);
      if (byte(m_pos - 1) == '`') {
        // The backtick closed the template, so this is the tail.
        m_current.kind = TokenKind::TemplateTail;
        m_templateDepth--;
      }
      commit();
      continue;
    }
    if (m_current.kind == TokenKind::TemplateHead)
      m_templateDepth++;
    scanOne();
  }
  for (const Token &token : m_tokens) {
    tree.tokenArray().append(token);
  }
  for (const Trivia &item : m_trivia) {
    tree.triviaArray().append(item);
  }
  for (uint32_t start : m_lineStarts) {
    tree.lineStartsForBuild().append(start);
  }
}

} // namespace fastlint::syntax
