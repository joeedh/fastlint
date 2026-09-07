#pragma once

// A small backtracking regular-expression matcher for the patterns rule
// options carry (`commentPattern`, ignore patterns), so ESLint configs keep
// working. ECMAScript syntax over bytes: literals, `.`, classes, `\d\w\s\b`,
// groups, alternation, greedy and lazy quantifiers, `^` and `$`, and the
// `i` flag. Lookaround, backreferences and Unicode classes are not
// supported and fail to compile.

#include "util/string.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

namespace fastlint::lint {

using litestl::util::Vector;
using std::string_view;

class Regex {
public:
  Regex() = default;

  /** Compiles `pattern`; `flags` may hold `i`. False when the syntax is unsupported. */
  bool compile(string_view pattern, string_view flags = {});

  bool valid() const
  {
    return m_valid;
  }

  /** Whether the pattern matches anywhere in `text`. */
  bool search(string_view text) const;

  struct Range {
    uint8_t lo;
    uint8_t hi;
  };

  struct Class {
    Vector<Range, 8> ranges;
    bool negate = false;
  };

  struct Term {
    enum Kind : uint8_t {
      Char,
      Any,
      Class,
      Begin,
      End,
      WordBoundary,
      NotWordBoundary,
      Group
    };
    Kind kind;
    uint8_t ch = 0;
    int index = -1;
    int min = 1;
    /** -1 means unbounded. */
    int max = 1;
    bool greedy = true;
  };

  using Sequence = Vector<Term, 8>;

  struct Group {
    Vector<Sequence, 2> alternatives;
  };

private:
  Vector<Group, 4> m_groups;
  Vector<Class, 4> m_classes;
  bool m_ignoreCase = false;
  bool m_valid = false;
};

} // namespace fastlint::lint
