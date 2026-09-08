#include "fastlint/lint/glob.h"

#include <string>

namespace fastlint::lint {

namespace {

using std::string_view;

char lower(char c)
{
  return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

bool charsEqual(char a, char b, bool caseInsensitive)
{
  return caseInsensitive ? lower(a) == lower(b) : a == b;
}

bool matchExpanded(string_view p, string_view s, bool caseInsensitive)
{
  while (!p.empty()) {
    if (p.size() >= 2 && p[0] == '*' && p[1] == '*') {
      // `**` eats whole segments: try the rest at every segment boundary.
      string_view rest = p.substr(2);
      if (!rest.empty() && rest[0] == '/') {
        rest = rest.substr(1);
      }
      if (rest.empty()) {
        return true;
      }
      for (size_t k = 0;; k++) {
        if (matchExpanded(rest, s.substr(k), caseInsensitive)) {
          return true;
        }
        size_t slash = s.find('/', k);
        if (slash == string_view::npos) {
          return false;
        }
        k = slash;
      }
    }
    char c = p[0];
    if (c == '*') {
      // Zero or more characters within the current segment.
      for (size_t k = 0;; k++) {
        if (matchExpanded(p.substr(1), s.substr(k), caseInsensitive)) {
          return true;
        }
        if (k >= s.size() || s[k] == '/') {
          return false;
        }
      }
    }
    if (s.empty()) {
      // `dir/**` also names `dir` itself.
      return c == '/' && p.size() >= 3 && p[1] == '*' && p[2] == '*' &&
             matchExpanded(p.substr(3), s, caseInsensitive);
    }
    if (c == '?') {
      if (s[0] == '/') {
        return false;
      }
    } else if (!charsEqual(c, s[0], caseInsensitive)) {
      return false;
    }
    p = p.substr(1);
    s = s.substr(1);
  }
  return s.empty();
}

/** Expands the first `{a,b}` group of `pattern` and matches every alternative. */
bool matchBraces(string_view pattern, string_view path, bool caseInsensitive)
{
  size_t open = pattern.find('{');
  if (open == string_view::npos) {
    return matchExpanded(pattern, path, caseInsensitive);
  }
  size_t close = pattern.find('}', open);
  if (close == string_view::npos) {
    return matchExpanded(pattern, path, caseInsensitive);
  }
  string_view head = pattern.substr(0, open);
  string_view tail = pattern.substr(close + 1);
  string_view body = pattern.substr(open + 1, close - open - 1);
  size_t at = 0;
  for (;;) {
    size_t comma = body.find(',', at);
    string_view alternative =
        body.substr(at, comma == string_view::npos ? string_view::npos : comma - at);
    std::string expanded;
    expanded.append(head);
    expanded.append(alternative);
    expanded.append(tail);
    if (matchBraces(expanded, path, caseInsensitive)) {
      return true;
    }
    if (comma == string_view::npos) {
      return false;
    }
    at = comma + 1;
  }
}

} // namespace

bool globMatch(string_view pattern, string_view path, bool caseInsensitive)
{
  if (pattern.size() >= 2 && pattern[0] == '.' && pattern[1] == '/') {
    pattern = pattern.substr(2);
  }
  return matchBraces(pattern, path, caseInsensitive);
}

} // namespace fastlint::lint
