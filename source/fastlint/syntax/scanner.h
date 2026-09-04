#pragma once

// The scanner: UTF-8 source, byte offsets, mode switching and rescans.
// Produces the token array, the trivia array and the line-start table
// (docs/STRATEGY.md "AST — tokens & trivia"). The parser drives it: it scans
// one token ahead and asks for rescans when a token needs reinterpretation
// (slash → regex, `>` splits, `}` → template middle/tail, JSX text).

#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/tree.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

namespace fastlint::syntax {

/** What the next token means, changed by the parser around rescan points. */
enum class ScanMode : uint8_t {
  Normal,
  /** After `}` in a template: `}` + text + maybe `${` or backtick. */
  TemplateMiddle,
  TemplateTail,
  /** Inside JSX children: text up to `<` or `{`. */
  JsxText,
  /** A JSX element-name part, which may contain `-` and `:`. */
  JsxIdentifier,
  /** Exactly one `>`, for splitting `>>` inside type argument lists. */
  SingleGreaterThan,
};

/**
 * Lexes a whole file into the token/trivia arrays of a grammar tree, or is
 * advanced incrementally by the parser. Rescans rewind into the token stream
 * and re-lex from a token's own start, so nothing is ever re-scanned by
 * copying source around.
 */
class Scanner {
public:
  struct Options {
    /** `.js`/`.jsx`/`.mjs`/`.cjs`: HTML comments, legacy octal tolerance. */
    bool javaScript = false;
  };

  Scanner(std::string_view source, const Options &options, Diagnostics &diagnostics);

  /**
   * Lexes the whole file into `tree`'s token, trivia and line-start arrays.
   * The parser path instead calls scanOne() with mode changes; this is for
   * tools that only need the token stream.
   */
  void scanAll(GrammarTree &tree);

  // ------------------------------------------------------------ parser API

  /** The token currently in front of the parser (already scanned). */
  const Token &current() const
  {
    return m_current;
  }

  /** Index of `current()` in the token array. */
  uint32_t tokenIndex() const
  {
    return m_currentIndex;
  }

  /** Lexes the next token, replacing `current`. */
  void scanOne();

  /** Sets the mode used by the next scanOne() (template tail, JSX text, …). */
  void setMode(ScanMode mode)
  {
    m_mode = mode;
  }

  /**
   * Reinterprets the current token, which must be a `/` or `/=`, as a regular
   * expression literal. Returns false (leaving the token alone) when the
   * regex does not terminate before a newline or EOF — that is division.
   */
  bool rescanSlash();

  /**
   * Splits the current `>`-family token (>>, >>>, >>=, …) so the parser can
   * close a type argument list one `>` at a time. Returns false when the
   * token is already a single `>`.
   */
  bool rescanGreaterThan();

  /**
   * Reinterprets the token after a template substitution: rewinds to the `}`
   * that closed the substitution and re-lexes it as a template middle or tail
   * (the `}` is part of that token, like TS).
   */
  void rescanTemplateTail(bool tail);

  /**
   * Reinterprets the current token as JSX text: rewinds to its start and
   * lexes text up to the next `<` or `{`.
   */
  void rescanJsxText();

  /** Reinterprets the current token as a JSX element-name identifier. */
  void rescanJsxIdentifier();

  // ---------------------------------------------------------- backtracking

  /** Scanner position, for parser speculation. */
  struct State {
    uint32_t tokenIndex;
    uint32_t tokenCount;
    uint32_t triviaCount;
    uint32_t sourcePos;
    ScanMode mode;
    uint32_t diagnosticCount;
  };

  State state() const
  {
    return {m_currentIndex, uint32_t(m_tokens.size()), uint32_t(m_trivia.size()),
            uint32_t(m_pos), m_mode, uint32_t(m_diagnostics->size())};
  }

  /** Back to an earlier state; also rewinds the tree's arrays via counts. */
  void rewind(const State &state)
  {
    m_currentIndex = state.tokenIndex;
    m_nextIndex = state.tokenCount;
    m_tokens.resize(state.tokenCount);
    m_trivia.resize(state.triviaCount);
    m_pos = state.sourcePos;
    m_mode = state.mode;
    m_gtRemaining = 0;
    m_diagnostics->resize(state.diagnosticCount);
    if (m_currentIndex == kNoTokenIndex || m_currentIndex >= m_tokens.size()) {
      m_current = Token{};
    } else {
      m_current = m_tokens[m_currentIndex];
    }
  }

  // -------------------------------------------------------------- accessors

  std::string_view source() const
  {
    return m_source;
  }

  span<const Token> tokens()
  {
    return {m_tokens.data(), m_tokens.size()};
  }
  span<const Trivia> trivia()
  {
    return {m_trivia.data(), m_trivia.size()};
  }
  span<const uint32_t> lineStarts()
  {
    return {m_lineStarts.data(), m_lineStarts.size()};
  }

  std::string_view text(const Token &token) const
  {
    return m_source.substr(token.offset, token.length);
  }

private:
  // Character helpers over UTF-8. `peekChar` decodes the code point at pos.
  uint32_t peekChar() const;
  uint32_t peekCharAt(size_t ahead) const;
  /** Length in bytes of the UTF-8 sequence at pos. */
  size_t charLength() const;
  void advanceChar();

  bool atEnd() const
  {
    return m_pos >= m_source.size();
  }
  char byte(size_t offset) const
  {
    return m_source[offset];
  }
  bool startsWith(std::string_view text) const
  {
    return m_source.substr(m_pos, text.size()) == text;
  }

  void addTrivia(Trivia::Kind kind, uint32_t offset, uint32_t length, bool lineBreak);
  void addToken(TokenKind kind, uint32_t offset, bool unterminated = false);
  void error(uint32_t code, uint32_t offset, uint32_t length, string message);

  /** Skips whitespace, newlines and comments; records them as trivia. */
  void skipTrivia(bool &sawLineBreak);
  void scanIdentifier(uint32_t start, bool isPrivate);
  void scanNumber(uint32_t start);
  void scanString(uint32_t start);
  void scanTemplate(uint32_t start, ScanMode mode);
  bool scanRegex(uint32_t start);
  void scanJsxText(uint32_t start);
  void scanJsxIdentifier(uint32_t start);
  void scanPunctuation(uint32_t start);

  /** Terminates `current`/token bookkeeping before scanning the next token. */
  void finishCurrent(uint32_t index);
  /** finishCurrent() at the next free slot, then bumps that slot. */
  void commit();

  std::string_view m_source;
  Options m_options;
  Diagnostics *m_diagnostics = nullptr;

  Vector<Token> m_tokens;
  Vector<Trivia> m_trivia;
  Vector<uint32_t> m_lineStarts;

  size_t m_pos = 0;
  ScanMode m_mode = ScanMode::Normal;
  Token m_current;
  /** Index of `m_current` in the token array; kNoToken before the first scan. */
  static constexpr uint32_t kNoTokenIndex = 0xffffffffu;
  uint32_t m_currentIndex = kNoTokenIndex;
  /** Next free slot in the token array; a scanOne() commits here. */
  uint32_t m_nextIndex = 0;
  /** Trivia recorded so far belongs to the next token until it is finished. */
  uint32_t m_lastTriviaEnd = 0;
  bool m_sawLineBreak = false;
  /** Template nesting seen by scanAll(), which auto-rescans `}` into
   * middle/tail tokens while keeping the CloseBrace token too. */
  uint32_t m_templateDepth = 0;
  /** State for rescanGreaterThan(): base offset of the `>` run and how many
   * are left to hand out, one per call. */
  uint32_t m_gtBase = 0;
  uint32_t m_gtRemaining = 0;

};

} // namespace fastlint::syntax
