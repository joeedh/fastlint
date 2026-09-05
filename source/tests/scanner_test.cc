#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/scanner.h"
#include "fastlint/syntax/tokens.h"
#include "fastlint/syntax/tree.h"
#include "fastlint/syntax/unicode.h"
#include "testing/snapshot.h"
#include "testing/test.h"

using litestl::util::span;
using litestl::util::string;
using namespace fastlint;
using fastlint::syntax::TokenKind;

namespace {

/** Scans a whole file up front; `tokens` excludes the trailing EOF. */
struct Scanned {
  syntax::Diagnostics diagnostics;
  syntax::Scanner scanner;
  syntax::GrammarTree tree;

  explicit Scanned(std::string_view source, syntax::Scanner::Options options = {})
      : scanner(source, options, diagnostics)
  {
    scanner.scanAll(tree);
  }

  size_t count()
  {
    return scanner.tokens().size() - 1; // minus EOF
  }
  const syntax::Token &token(size_t index)
  {
    return scanner.tokens()[index];
  }
};

/** Scanner stepped manually, for tests that drive scanOne()/rescans. */
struct Stepped {
  syntax::Diagnostics diagnostics;
  syntax::Scanner scanner;

  explicit Stepped(std::string_view source, syntax::Scanner::Options options = {})
      : scanner(source, options, diagnostics)
  {
  }

  /** Steps forward until the current token has the given kind (or EOF). */
  void to(TokenKind kind)
  {
    if (!started) {
      started = true;
      scanner.scanOne();
    }
    while (scanner.current().kind != kind &&
           scanner.current().kind != TokenKind::EndOfFile)
    {
      scanner.scanOne();
    }
  }

  bool started = false;
};

std::string_view textOf(std::string_view source, const syntax::Token &token)
{
  return source.substr(token.offset, token.length);
}

// Lexes the whole source and returns just the kind names — easy to snapshot.
string kindNames(std::string_view source)
{
  Scanned scan(source);
  string out;
  for (size_t i = 0; i < scan.scanner.tokens().size(); i++) {
    if (i != 0)
      out += (" ");
    out += (syntax::tokenName(scan.token(i).kind));
  }
  return out;
}

} // namespace

TEST(scanner, tokenizes_punctuation_and_operators)
{
  SNAPSHOT(kindNames("a + b - c ** d = e === f !== g => ...x ?. y ?? z"));
}

TEST(scanner, splits_multi_char_punctuation_greedily)
{
  // `>>=` is one token in normal mode; the parser rescans for type lists.
  Scanned scan("a >>= b >>> c");
  CHECK_EQ(scan.token(1).kind, TokenKind::GreaterThanGreaterThanEqualsToken);
  CHECK_EQ(scan.token(3).kind, TokenKind::GreaterThanGreaterThanGreaterThanToken);
}

TEST(scanner, identifiers_keywords_and_private_names)
{
  std::string_view source = "const class foo #bar _baz $qux";
  Scanned scan(source);
  CHECK_EQ(scan.count(), 6);
  CHECK_EQ(scan.token(0).kind, TokenKind::ConstKeyword);
  CHECK_EQ(scan.token(1).kind, TokenKind::ClassKeyword);
  CHECK_EQ(scan.token(2).kind, TokenKind::Identifier);
  CHECK_EQ(scan.token(3).kind, TokenKind::PrivateIdentifier);
  CHECK_EQ(textOf(source, scan.token(3)), "#bar");
  CHECK_EQ(scan.token(4).kind, TokenKind::Identifier);
  CHECK_EQ(scan.token(5).kind, TokenKind::Identifier);
}

TEST(scanner, unicode_whitespace_is_trivia)
{
  // A BOM, then NBSP and an ideographic space between tokens.
  Scanned scan("\xef\xbb\xbf"
               "a\xc2\xa0+\xe3\x80\x80"
               "b");
  CHECK(scan.diagnostics.empty());
  CHECK_EQ(scan.count(), 3);
  CHECK_EQ(scan.token(0).kind, TokenKind::Identifier);
  CHECK_EQ(scan.token(0).offset, 3u);
  CHECK_EQ(scan.token(1).kind, TokenKind::PlusToken);
  CHECK_EQ(scan.token(2).kind, TokenKind::Identifier);
}

TEST(scanner, non_identifier_character_is_one_bad_token)
{
  // `§` cannot start a name: one diagnostic, one two-byte token, then `a`.
  Scanned scan("\xc2\xa7"
               "a");
  CHECK_EQ(scan.diagnostics.size(), 1u);
  CHECK_EQ(scan.diagnostics.items()[0].code, 1127u);
  CHECK_EQ(scan.count(), 2);
  CHECK_EQ(scan.token(0).length, 2u);
  CHECK_EQ(scan.token(1).kind, TokenKind::Identifier);
  CHECK_EQ(scan.token(1).offset, 2u);
}

TEST(scanner, shebang_line_is_trivia)
{
  Scanned scan("#!/usr/bin/env node\nconst x = 1;");
  CHECK(scan.diagnostics.empty());
  CHECK_EQ(scan.token(0).kind, TokenKind::ConstKeyword);
  CHECK_EQ(scan.token(0).offset, 20u);
  CHECK_EQ(scan.scanner.trivia()[0].kind, syntax::Trivia::Kind::Shebang);
  CHECK_EQ(scan.scanner.trivia()[0].length, 19u);
  // Only the first byte of the file starts one; `#!` later is a hash and a bang.
  Scanned later("a\n#!x");
  CHECK_EQ(later.token(1).kind, TokenKind::HashToken);
}

TEST(scanner, contextual_keywords_map_to_their_kinds)
{
  Scanned scan("as type from of get set");
  CHECK_EQ(scan.count(), 6);
  CHECK_EQ(scan.token(0).kind, TokenKind::AsKeyword);
  CHECK_EQ(scan.token(1).kind, TokenKind::TypeKeyword);
  CHECK_EQ(scan.token(2).kind, TokenKind::FromKeyword);
  CHECK_EQ(scan.token(3).kind, TokenKind::OfKeyword);
  CHECK_EQ(scan.token(4).kind, TokenKind::GetKeyword);
  CHECK_EQ(scan.token(5).kind, TokenKind::SetKeyword);
}

TEST(scanner, numeric_literals)
{
  std::string_view source = "0 42 3.14 1e10 0xFF 0o17 0b1010 1_000_000 123n";
  Scanned scan(source);
  CHECK_EQ(scan.count(), 9);
  for (size_t i = 0; i < scan.count() - 1; i++) {
    CHECK_EQ(scan.token(i).kind, TokenKind::NumericLiteral);
  }
  CHECK_EQ(scan.token(scan.count() - 1).kind, TokenKind::BigIntLiteral);
  CHECK(!scan.token(4).legacyOctal); // 0xFF
  CHECK(!scan.token(5).legacyOctal); // 0o17 is the modern form
}

TEST(scanner, legacy_octal_is_flagged_only_for_the_leading_zero_form)
{
  Scanned scan("0123 089 1.2");
  CHECK(scan.token(0).legacyOctal);
  CHECK(!scan.token(1).legacyOctal);
  CHECK(!scan.token(2).legacyOctal);
}

TEST(scanner, numeric_separator_must_be_between_digits)
{
  Scanned scan("1_000 1__0 _1 1_");
  CHECK_EQ(scan.diagnostics.size(), 3);
}

TEST(scanner, identifiers_cannot_follow_numbers)
{
  Scanned scan("123abc");
  CHECK_EQ(scan.diagnostics.size(), 1);
  CHECK_EQ(scan.token(0).kind, TokenKind::NumericLiteral);
  CHECK_EQ(scan.token(1).kind, TokenKind::Identifier);
}

TEST(scanner, strings)
{
  std::string_view source = "\"hi\" 'there' \"\\u{1F600}\" \"\\x41\" \"tab\\tend\"";
  Scanned scan(source);
  CHECK_EQ(scan.count(), 5);
  for (size_t i = 0; i < scan.count(); i++) {
    CHECK_EQ(scan.token(i).kind, TokenKind::StringLiteral);
  }
}

TEST(scanner, unterminated_string_reports_and_ends_at_newline)
{
  std::string_view source = "\"abc\ndef";
  Scanned scan(source);
  CHECK_EQ(scan.diagnostics.size(), 1);
  CHECK(scan.token(0).unterminated);
  CHECK_EQ(textOf(source, scan.token(0)), "\"abc");
  CHECK_EQ(scan.token(1).kind, TokenKind::Identifier);
}

TEST(scanner, templates)
{
  std::string_view source = "`plain` `head${x}mid${y}tail`";
  Scanned scan(source);
  CHECK_EQ(scan.count(), 8);
  CHECK_EQ(scan.token(0).kind, TokenKind::NoSubstitutionTemplateLiteral);
  CHECK_EQ(scan.token(1).kind, TokenKind::TemplateHead);
  CHECK_EQ(scan.token(3).kind, TokenKind::CloseBraceToken);
  CHECK_EQ(scan.token(4).kind, TokenKind::TemplateMiddle);
  CHECK_EQ(scan.token(6).kind, TokenKind::CloseBraceToken);
  CHECK_EQ(scan.token(7).kind, TokenKind::TemplateTail);
}

TEST(scanner, unterminated_template_reports)
{
  Scanned scan("`abc");
  CHECK_EQ(scan.diagnostics.size(), 1);
  CHECK(scan.token(0).unterminated);
  CHECK_EQ(scan.token(0).kind, TokenKind::NoSubstitutionTemplateLiteral);
}

TEST(scanner, line_breaks_drive_the_preceding_flag)
{
  Scanned scan("a\nb\r\nc d");
  CHECK(!scan.token(0).precedingLineBreak);
  CHECK(scan.token(1).precedingLineBreak);
  CHECK(scan.token(2).precedingLineBreak);
  CHECK(!scan.token(3).precedingLineBreak);
}

TEST(scanner, trivia_kinds_are_recorded)
{
  std::string_view source = "// line\n/* multi\nline */ x";
  Scanned scan(source);
  span<const syntax::Trivia> trivia = scan.scanner.trivia();
  CHECK_EQ(trivia.size(), 4);
  CHECK_EQ(trivia[0].kind, syntax::Trivia::Kind::SingleLineComment);
  CHECK_EQ(trivia[1].kind, syntax::Trivia::Kind::NewLine);
  CHECK_EQ(trivia[2].kind, syntax::Trivia::Kind::MultiLineComment);
  CHECK(trivia[2].lineBreak);
  CHECK_EQ(trivia[3].kind, syntax::Trivia::Kind::Whitespace);
}

TEST(scanner, leading_trivia_attaches_to_the_next_token)
{
  std::string_view source = "/* lead */ x";
  Scanned scan(source);
  const syntax::Token &x = scan.token(0);
  CHECK_EQ(x.leadingTriviaCount, 1);
  CHECK_EQ(scan.scanner.trivia()[x.leadingTriviaStart].kind,
           syntax::Trivia::Kind::MultiLineComment);
}

TEST(scanner, line_start_table_tracks_every_line_kind)
{
  std::string_view source = "a\nb\r\nc\u2028d";
  Scanned scan(source);
  span<const uint32_t> starts = scan.scanner.lineStarts();
  CHECK_EQ(starts.size(), 4);
  CHECK_EQ(starts[1], 2);
  CHECK_EQ(starts[2], 4);
  CHECK_EQ(starts[3], 7); // U+2028 is 3 bytes
}

span<const syntax::Token> treeTokens(syntax::GrammarTree &tree)
{
  return tree.tokens();
}

TEST(scanner, scan_all_fills_the_tree)
{
  std::string_view source = "let x = 1;\n";
  Scanned scan(source);
  CHECK_EQ(treeTokens(scan.tree).size(), scan.count() + 1);
  CHECK_EQ(scan.tree.trivia().size(), scan.scanner.trivia().size());
  CHECK_EQ(scan.tree.lineStarts().size(), 2);
}

TEST(scanner, rescan_slash_turns_division_into_regex)
{
  std::string_view source = "a /b[/]c/g + 1";
  Stepped scan(source);
  scan.to(TokenKind::SlashToken);
  CHECK_EQ(scan.scanner.current().kind, TokenKind::SlashToken);
  CHECK(scan.scanner.rescanSlash());
  CHECK_EQ(scan.scanner.current().kind, TokenKind::RegularExpressionLiteral);
  CHECK_EQ(scan.scanner.text(scan.scanner.current()), "/b[/]c/g");
}

TEST(scanner, rescan_slash_keeps_division_when_regex_cannot_terminate)
{
  std::string_view source = "a = b / c";
  Stepped scan(source);
  scan.to(TokenKind::SlashToken);
  CHECK(!scan.scanner.rescanSlash());
  CHECK_EQ(scan.scanner.current().kind, TokenKind::SlashToken);
}

TEST(scanner, rescan_slash_handles_slash_equals)
{
  std::string_view source = "a /=b/";
  Stepped scan(source);
  scan.to(TokenKind::SlashEqualsToken);
  CHECK(scan.scanner.rescanSlash());
  CHECK_EQ(scan.scanner.current().kind, TokenKind::RegularExpressionLiteral);
}

TEST(scanner, rescan_greater_than_splits_token_by_token)
{
  std::string_view source = "a >> b";
  Stepped scan(source);
  scan.to(TokenKind::GreaterThanGreaterThanToken);
  CHECK_EQ(scan.scanner.current().kind, TokenKind::GreaterThanGreaterThanToken);
  CHECK(scan.scanner.rescanGreaterThan());
  CHECK_EQ(scan.scanner.current().kind, TokenKind::GreaterThanToken);
  CHECK_EQ(scan.scanner.text(scan.scanner.current()), ">");
  // The second `>` of the pair.
  CHECK(scan.scanner.rescanGreaterThan());
  CHECK_EQ(scan.scanner.text(scan.scanner.current()), ">");
  // A single `>` does not split further.
  CHECK(!scan.scanner.rescanGreaterThan());
}

TEST(scanner, rescan_template_tail)
{
  std::string_view source = "`a${x}b`";
  Stepped scan(source);
  scan.scanner.scanOne(); // head
  CHECK_EQ(scan.scanner.current().kind, TokenKind::TemplateHead);
  scan.scanner.scanOne(); // x
  scan.scanner.scanOne(); // the `}` closing the substitution
  CHECK_EQ(scan.scanner.current().kind, TokenKind::CloseBraceToken);
  scan.scanner.rescanTemplateTail(false);
  CHECK_EQ(scan.scanner.current().kind, TokenKind::TemplateMiddle);
  CHECK_EQ(scan.scanner.text(scan.scanner.current()), "}b`");
  scan.scanner.rescanTemplateTail(true);
  CHECK_EQ(scan.scanner.current().kind, TokenKind::TemplateTail);
}

TEST(scanner, rescan_jsx_text_and_identifier)
{
  std::string_view source = "<My-El some text";
  Stepped scan(source);
  scan.scanner.scanOne(); // `<` (LessThanToken)
  scan.scanner.setMode(syntax::ScanMode::JsxIdentifier);
  scan.scanner.scanOne();
  CHECK_EQ(scan.scanner.current().kind, TokenKind::JsxIdentifier);
  CHECK_EQ(scan.scanner.text(scan.scanner.current()), "My-El");
  scan.scanner.setMode(syntax::ScanMode::JsxText);
  scan.scanner.scanOne();
  CHECK_EQ(scan.scanner.current().kind, TokenKind::JsxText);
  CHECK_EQ(scan.scanner.text(scan.scanner.current()), " some text");
}

TEST(scanner, speculation_rewinds_tokens_trivia_and_diagnostics)
{
  std::string_view source = "let /* c */ x = 1";
  Stepped scan(source);
  scan.scanner.scanOne(); // let
  auto state = scan.scanner.state();
  size_t diags = scan.diagnostics.size();
  scan.scanner.scanOne(); // x
  scan.scanner.scanOne(); // =
  scan.diagnostics.report(0, 0, 0, string("speculative"));
  CHECK(scan.diagnostics.size() == diags + 1);
  scan.scanner.rewind(state);
  CHECK_EQ(scan.scanner.tokenIndex(), state.tokenIndex);
  CHECK_EQ(scan.scanner.tokens().size(), state.tokenCount);
  CHECK_EQ(scan.scanner.trivia().size(), state.triviaCount);
  CHECK_EQ(scan.diagnostics.size(), diags);
  scan.scanner.scanOne();
  CHECK_EQ(scan.scanner.current().kind, TokenKind::Identifier);
  CHECK_EQ(scan.scanner.text(scan.scanner.current()), "x");
}

TEST(scanner, unicode_identifiers_and_escapes)
{
  std::string_view source = "héllo \\u0041 caf\\u00e9";
  Scanned scan(source);
  CHECK_EQ(scan.count(), 3);
  CHECK_EQ(textOf(source, scan.token(0)), "héllo");
  CHECK_EQ(textOf(source, scan.token(2)), "caf\\u00e9");
}

TEST(scanner, unicode_tables_classify_id_start_and_continue)
{
  CHECK(syntax::idStartContains('a'));
  CHECK(syntax::idStartContains(0x00e9)); // é
  CHECK(syntax::idStartContains(0x4e00)); // 一
  CHECK(!syntax::idStartContains('1'));
  CHECK(!syntax::idStartContains(0x0301)); // combining acute: continue only
  CHECK(syntax::idContinueContains('1'));
  CHECK(syntax::idContinueContains(0x0301));
}

TEST(scanner, invalid_characters_report_without_losing_sync)
{
  Scanned scan("a \u0001 b");
  CHECK_EQ(scan.diagnostics.size(), 1);
  CHECK_EQ(scan.count(), 3); // a, the broken token, b
  CHECK_EQ(scan.token(1).kind, TokenKind::Identifier);
  CHECK_EQ(scan.token(2).kind, TokenKind::Identifier);
}

TEST(scanner, merge_conflict_markers_are_diagnostics_and_trivia)
{
  Scanned scan("<<<<<<< head\nx");
  CHECK_EQ(scan.diagnostics.size(), 1);
  CHECK_EQ(scan.diagnostics.items()[0].code, 1402);
}

TEST(scanner, snapshot_of_a_small_program)
{
  std::string_view source = R"(// banner
const value = 42;
export function greet(name: string): string {
  return `hi ${name}`;
}
)";
  Scanned scan(source);
  string out;
  for (size_t i = 0; i < scan.scanner.tokens().size(); i++) {
    const syntax::Token &token = scan.token(i);
    out += (syntax::tokenName(token.kind));
    if (token.precedingLineBreak)
      out += (" <lb>");
    out += ("\n");
  }
  SNAPSHOT(out);
}
