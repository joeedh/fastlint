#include "fastlint/lint/regex.h"
#include "testing/test.h"

using namespace fastlint;

namespace {

/** 1 on a match, 0 on none, -1 when the pattern fails to compile. */
int matches(const char *pattern, const char *text, const char *flags = "")
{
  lint::Regex re;
  if (!re.compile(pattern, flags)) {
    return -1;
  }
  return re.search(text) ? 1 : 0;
}

} // namespace

#define MATCH(...) CHECK_EQ(matches(__VA_ARGS__), 1)
#define NO_MATCH(...) CHECK_EQ(matches(__VA_ARGS__), 0)

TEST(lint_regex, literals_and_dot)
{
  MATCH("abc", "xxabcxx");
  NO_MATCH("abd", "xxabcxx");
  MATCH("a.c", "abc");
  NO_MATCH("a.c", "a\nc");
  MATCH("a\\.c", "a.c");
  NO_MATCH("a\\.c", "abc");
}

TEST(lint_regex, quantifiers)
{
  MATCH("ab*c", "ac");
  MATCH("ab*c", "abbbc");
  MATCH("ab+c", "abc");
  NO_MATCH("ab+c", "ac");
  MATCH("ab?c", "ac");
  MATCH("ab?c", "abc");
  NO_MATCH("^ab?c$", "abbc");
  MATCH("a{2}", "baab");
  NO_MATCH("^a{2}$", "aaa");
  MATCH("^a{2,}$", "aaaa");
  MATCH("^a{1,3}$", "aa");
  NO_MATCH("^a{1,3}$", "aaaa");
  MATCH("^a*?b$", "aab");
  MATCH("^(a|ab)(c|bcd)$", "abcd");
  MATCH("^(a*)*$", "aaaa");
}

TEST(lint_regex, classes_and_escapes)
{
  MATCH("[abc]+", "xxbax");
  NO_MATCH("^[abc]+$", "abd");
  MATCH("^[a-z0-9_]+$", "abc_123");
  NO_MATCH("^[^a-z]+$", "abc");
  MATCH("^[^a-z]+$", "ABC");
  MATCH("\\d+", "id 42");
  NO_MATCH("\\D", "42");
  MATCH("\\w+", "  word ");
  MATCH("\\s", "a b");
  NO_MATCH("\\S", "   ");
  MATCH("[\\s\\w]+omitted", "break is omitted");
  MATCH("\\bfoo\\b", "a foo b");
  NO_MATCH("\\bfoo\\b", "afoob");
  MATCH("a\\tb", "a\tb");
  MATCH("[\\]]", "]");
  MATCH("^\\x41$", "A");
}

TEST(lint_regex, anchors_alternation_and_flags)
{
  MATCH("^abc", "abcd");
  NO_MATCH("^abc", "xabc");
  MATCH("abc$", "xabc");
  MATCH("cat|dog", "hotdog");
  MATCH("^(?:cat|dog)s?$", "dogs");
  MATCH("FALLS?\\s?THROUGH", "// falls through", "i");
  MATCH("falls?\\s?through", "fall through");
  MATCH("falls?\\s?through", "fallthrough");
  NO_MATCH("falls?\\s?through", "falls  through");
  MATCH("[a-c]", "B", "i");
  NO_MATCH("[a-c]", "B");
}

TEST(lint_regex, rejects_unsupported_syntax)
{
  lint::Regex re;
  CHECK(!re.compile("(?=x)"));
  CHECK(!re.compile("(?<name>x)"));
  CHECK(!re.compile("a("));
  CHECK(!re.compile("a)"));
  CHECK(!re.compile("[a"));
  CHECK(!re.compile("*a"));
  CHECK(!re.compile("a", "m"));
  CHECK(!re.valid());
  CHECK(!re.search("a"));
}
