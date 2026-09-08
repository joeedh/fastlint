#include "fastlint/lint/regex.h"

#include "util/function.h"

namespace fastlint::lint {

namespace {

using litestl::util::function_ref;

char lower(char c)
{
  return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}

bool isWordChar(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '_';
}

bool isDigit(char c)
{
  return c >= '0' && c <= '9';
}

int hexValue(char c)
{
  if (isDigit(c)) {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

/** Encodes `cp` as UTF-8 into `out` (up to four bytes) and returns the count. */
int encodeUtf8(int cp, uint8_t out[4])
{
  if (cp < 0x80) {
    out[0] = uint8_t(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = uint8_t(0xC0 | (cp >> 6));
    out[1] = uint8_t(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = uint8_t(0xE0 | (cp >> 12));
    out[1] = uint8_t(0x80 | ((cp >> 6) & 0x3F));
    out[2] = uint8_t(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = uint8_t(0xF0 | (cp >> 18));
  out[1] = uint8_t(0x80 | ((cp >> 12) & 0x3F));
  out[2] = uint8_t(0x80 | ((cp >> 6) & 0x3F));
  out[3] = uint8_t(0x80 | (cp & 0x3F));
  return 4;
}

/** Adds the ranges of a `\d`, `\w` or `\s` class; the caller negates the upper-case
 * forms. */
void addNamedClass(Regex::Class &cls, char name)
{
  switch (name) {
  case 'd':
    cls.ranges.append({'0', '9'});
    break;
  case 'w':
    cls.ranges.append({'a', 'z'});
    cls.ranges.append({'A', 'Z'});
    cls.ranges.append({'0', '9'});
    cls.ranges.append({'_', '_'});
    break;
  case 's':
    cls.ranges.append({' ', ' '});
    cls.ranges.append({'\t', '\r'});
    break;
  default:
    break;
  }
}

struct Compiler {
  string_view pattern;
  size_t pos = 0;
  Vector<Regex::Group, 4> &groups;
  Vector<Regex::Class, 4> &classes;
  bool ok = true;

  bool more() const
  {
    return pos < pattern.size();
  }

  char peek() const
  {
    return pattern[pos];
  }

  /** The character an escape denotes, or -1 when it names a class or an assertion. */
  int escapeChar(char c)
  {
    switch (c) {
    case 'n':
      return '\n';
    case 'r':
      return '\r';
    case 't':
      return '\t';
    case 'f':
      return '\f';
    case 'v':
      return '\v';
    case '0':
      return 0;
    case 'x': {
      if (pos + 2 <= pattern.size() && hexValue(pattern[pos]) >= 0 &&
          hexValue(pattern[pos + 1]) >= 0)
      {
        int value = hexValue(pattern[pos]) * 16 + hexValue(pattern[pos + 1]);
        pos += 2;
        return value;
      }
      return 'x';
    }
    case 'u': {
      // `\u{...}` names a code point of any width; `\uXXXX` names four hex digits.
      // The caller expands a value above `0x7F` into its UTF-8 bytes.
      if (more() && peek() == '{') {
        size_t save = pos;
        pos++;
        int value = 0;
        bool any = false;
        while (more() && hexValue(peek()) >= 0) {
          value = value * 16 + hexValue(peek());
          pos++;
          any = true;
          if (value > 0x10FFFF) {
            break;
          }
        }
        if (any && value <= 0x10FFFF && more() && peek() == '}') {
          pos++;
          return value;
        }
        pos = save;
        return 'u';
      }
      if (pos + 4 <= pattern.size()) {
        int value = 0;
        for (int i = 0; i < 4; i++) {
          int h = hexValue(pattern[pos + size_t(i)]);
          if (h < 0) {
            return 'u';
          }
          value = value * 16 + h;
        }
        pos += 4;
        return value;
      }
      return 'u';
    }
    case 'd':
    case 'D':
    case 'w':
    case 'W':
    case 's':
    case 'S':
    case 'b':
    case 'B':
      return -1;
    default:
      return static_cast<unsigned char>(c);
    }
  }

  int parseNumber()
  {
    int value = 0;
    bool any = false;
    while (more() && isDigit(peek())) {
      value = value * 10 + (peek() - '0');
      pos++;
      any = true;
    }
    return any ? value : -1;
  }

  void parseQuantifier(Regex::Term &term)
  {
    if (!more()) {
      return;
    }
    char c = peek();
    if (c == '*') {
      term.min = 0;
      term.max = -1;
      pos++;
    } else if (c == '+') {
      term.min = 1;
      term.max = -1;
      pos++;
    } else if (c == '?') {
      term.min = 0;
      term.max = 1;
      pos++;
    } else if (c == '{') {
      size_t save = pos;
      pos++;
      int min = parseNumber();
      if (min < 0) {
        pos = save;
        return;
      }
      int max = min;
      if (more() && peek() == ',') {
        pos++;
        max = more() && peek() == '}' ? -1 : parseNumber();
        if (max == -2) {
          pos = save;
          return;
        }
      }
      if (!more() || peek() != '}') {
        pos = save;
        return;
      }
      pos++;
      term.min = min;
      term.max = max;
    } else {
      return;
    }
    if (more() && peek() == '?') {
      term.greedy = false;
      pos++;
    }
  }

  void parseClass(Regex::Term &term)
  {
    Regex::Class cls;
    if (more() && peek() == '^') {
      cls.negate = true;
      pos++;
    }
    bool first = true;
    while (more() && (peek() != ']' || first)) {
      first = false;
      int lo;
      char c = peek();
      pos++;
      if (c == '\\') {
        if (!more()) {
          ok = false;
          return;
        }
        char e = peek();
        pos++;
        int value = escapeChar(e);
        if (value > 0x7F) {
          // A multi-byte code point cannot be a byte-range endpoint.
          ok = false;
          return;
        }
        if (value < 0) {
          if (e == 'D' || e == 'W' || e == 'S') {
            // A negated class inside a set needs set subtraction; unsupported.
            ok = false;
            return;
          }
          if (e == 'b') {
            lo = '\b';
          } else {
            addNamedClass(cls, e);
            continue;
          }
        } else {
          lo = value;
        }
      } else {
        lo = static_cast<unsigned char>(c);
      }
      int hi = lo;
      if (pos + 1 < pattern.size() && peek() == '-' && pattern[pos + 1] != ']') {
        pos++;
        char d = peek();
        pos++;
        if (d == '\\') {
          if (!more()) {
            ok = false;
            return;
          }
          // Advance past the escape letter before `escapeChar` reads its hex
          // digits from `pos`, as the low-endpoint path above does.
          char he = peek();
          pos++;
          int value = escapeChar(he);
          if (value < 0 || value > 0x7F) {
            ok = false;
            return;
          }
          hi = value;
        } else {
          hi = static_cast<unsigned char>(d);
        }
        if (hi < lo) {
          ok = false;
          return;
        }
      }
      cls.ranges.append({uint8_t(lo), uint8_t(hi)});
    }
    if (!more()) {
      ok = false;
      return;
    }
    pos++; // ]
    term.kind = Regex::Term::Class;
    term.index = int(classes.size());
    classes.append(std::move(cls));
  }

  void parseSequence(Regex::Sequence &out)
  {
    while (ok && more() && peek() != '|' && peek() != ')') {
      Regex::Term term{Regex::Term::Char};
      char c = peek();
      pos++;
      switch (c) {
      case '.':
        term.kind = Regex::Term::Any;
        break;
      case '^':
        term.kind = Regex::Term::Begin;
        break;
      case '$':
        term.kind = Regex::Term::End;
        break;
      case '(': {
        if (more() && peek() == '?') {
          if (pos + 1 < pattern.size() && pattern[pos + 1] == ':') {
            pos += 2;
          } else {
            // Lookaround and named groups are unsupported.
            ok = false;
            return;
          }
        }
        term.kind = Regex::Term::Group;
        term.index = int(groups.size());
        groups.append(Regex::Group{});
        parseGroup(term.index);
        if (!ok) {
          return;
        }
        if (!more() || peek() != ')') {
          ok = false;
          return;
        }
        pos++;
        break;
      }
      case '[':
        parseClass(term);
        if (!ok) {
          return;
        }
        break;
      case '\\': {
        if (!more()) {
          ok = false;
          return;
        }
        char e = peek();
        pos++;
        int value = escapeChar(e);
        if (!ok) {
          return;
        }
        if (value > 0x7F) {
          // A code point above ASCII is matched as its UTF-8 bytes, held in a
          // group so a following quantifier applies to the whole character.
          uint8_t bytes[4];
          int n = encodeUtf8(value, bytes);
          Regex::Group group;
          Regex::Sequence seq;
          for (int i = 0; i < n; i++) {
            Regex::Term byte{Regex::Term::Char};
            byte.ch = bytes[i];
            seq.append(byte);
          }
          group.alternatives.append(std::move(seq));
          term.kind = Regex::Term::Group;
          term.index = int(groups.size());
          groups.append(std::move(group));
        } else if (value >= 0) {
          term.ch = uint8_t(value);
        } else if (e == 'b') {
          term.kind = Regex::Term::WordBoundary;
        } else if (e == 'B') {
          term.kind = Regex::Term::NotWordBoundary;
        } else {
          Regex::Class cls;
          addNamedClass(cls, lower(e));
          cls.negate = e >= 'A' && e <= 'Z';
          term.kind = Regex::Term::Class;
          term.index = int(classes.size());
          classes.append(std::move(cls));
        }
        break;
      }
      case '*':
      case '+':
      case '?':
        // A quantifier with nothing to repeat.
        ok = false;
        return;
      default:
        term.ch = static_cast<unsigned char>(c);
        break;
      }
      if (term.kind != Regex::Term::Begin && term.kind != Regex::Term::End &&
          term.kind != Regex::Term::WordBoundary &&
          term.kind != Regex::Term::NotWordBoundary)
      {
        parseQuantifier(term);
      }
      out.append(term);
    }
  }

  void parseGroup(int index)
  {
    for (;;) {
      Regex::Sequence seq;
      parseSequence(seq);
      if (!ok) {
        return;
      }
      groups[index].alternatives.append(std::move(seq));
      if (more() && peek() == '|') {
        pos++;
        continue;
      }
      return;
    }
  }
};

struct Matcher {
  const Vector<Regex::Group, 4> &groups;
  const Vector<Regex::Class, 4> &classes;
  bool ignoreCase;
  string_view text;

  bool sameChar(char a, uint8_t b) const
  {
    return ignoreCase ? lower(a) == lower(char(b)) : static_cast<unsigned char>(a) == b;
  }

  bool inClass(const Regex::Class &cls, char c) const
  {
    unsigned char u = static_cast<unsigned char>(c);
    bool hit = false;
    for (const Regex::Range &r : cls.ranges) {
      if (u >= r.lo && u <= r.hi) {
        hit = true;
        break;
      }
      if (ignoreCase) {
        unsigned char l = static_cast<unsigned char>(lower(c));
        unsigned char up =
            (c >= 'a' && c <= 'z') ? static_cast<unsigned char>(c - 'a' + 'A') : u;
        if ((l >= r.lo && l <= r.hi) || (up >= r.lo && up <= r.hi)) {
          hit = true;
          break;
        }
      }
    }
    return hit != cls.negate;
  }

  bool atWordBoundary(size_t pos) const
  {
    bool before = pos > 0 && isWordChar(text[pos - 1]);
    bool after = pos < text.size() && isWordChar(text[pos]);
    return before != after;
  }

  bool matchOne(const Regex::Term &t, size_t pos, function_ref<bool(size_t)> k) const
  {
    switch (t.kind) {
    case Regex::Term::Char:
      return pos < text.size() && sameChar(text[pos], t.ch) && k(pos + 1);
    case Regex::Term::Any:
      return pos < text.size() && text[pos] != '\n' && text[pos] != '\r' && k(pos + 1);
    case Regex::Term::Class:
      return pos < text.size() && inClass(classes[t.index], text[pos]) && k(pos + 1);
    case Regex::Term::Begin:
      return pos == 0 && k(pos);
    case Regex::Term::End:
      return pos == text.size() && k(pos);
    case Regex::Term::WordBoundary:
      return atWordBoundary(pos) && k(pos);
    case Regex::Term::NotWordBoundary:
      return !atWordBoundary(pos) && k(pos);
    case Regex::Term::Group:
      for (const Regex::Sequence &alt : groups[t.index].alternatives) {
        if (matchSequence(alt, 0, pos, k)) {
          return true;
        }
      }
      return false;
    }
    return false;
  }

  bool matchRepeat(const Regex::Term &t,
                   int count,
                   size_t pos,
                   function_ref<bool(size_t)> k) const
  {
    bool canMore = t.max < 0 || count < t.max;
    bool canStop = count >= t.min;
    // An iteration that consumed nothing must not repeat, or it never ends.
    auto more = [&]() {
      return canMore && matchOne(t, pos, [&](size_t next) {
               return (next != pos || count < t.min) &&
                      matchRepeat(t, count + 1, next, k);
             });
    };
    if (t.greedy) {
      return more() || (canStop && k(pos));
    }
    return (canStop && k(pos)) || more();
  }

  bool matchSequence(const Regex::Sequence &seq,
                     size_t i,
                     size_t pos,
                     function_ref<bool(size_t)> k) const
  {
    if (i == seq.size()) {
      return k(pos);
    }
    return matchRepeat(seq[int(i)], 0, pos, [&](size_t next) {
      return matchSequence(seq, i + 1, next, k);
    });
  }
};

} // namespace

bool Regex::compile(string_view pattern, string_view flags)
{
  m_groups.clear();
  m_classes.clear();
  m_ignoreCase = false;
  m_valid = false;
  for (char f : flags) {
    if (f == 'i') {
      m_ignoreCase = true;
    } else if (f != 'u' && f != 'g' && f != 'v') {
      return false;
    }
  }
  m_groups.append(Group{});
  Compiler compiler{pattern, 0, m_groups, m_classes};
  compiler.parseGroup(0);
  if (!compiler.ok || compiler.more()) {
    m_groups.clear();
    m_classes.clear();
    return false;
  }
  m_valid = true;
  return true;
}

bool Regex::search(string_view text) const
{
  if (!m_valid) {
    return false;
  }
  Matcher matcher{m_groups, m_classes, m_ignoreCase, text};
  Term root{Term::Group};
  root.index = 0;
  for (size_t start = 0; start <= text.size(); start++) {
    if (matcher.matchOne(root, start, [](size_t) { return true; })) {
      return true;
    }
  }
  return false;
}

} // namespace fastlint::lint
