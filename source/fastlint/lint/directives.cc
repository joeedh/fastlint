#include "fastlint/lint/directives.h"

#include <initializer_list>

namespace fastlint::lint {

namespace {

bool isSpace(char c)
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

string_view trim(string_view text)
{
  while (!text.empty() && isSpace(text.front())) {
    text = text.substr(1);
  }
  while (!text.empty() && isSpace(text.back())) {
    text = text.substr(0, text.size() - 1);
  }
  return text;
}

bool startsWith(string_view text, string_view prefix)
{
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

/** The text of a comment without its delimiters. */
string_view commentBody(string_view source, const syntax::Trivia &comment)
{
  string_view text = source.substr(comment.offset, comment.length);
  if (comment.kind == syntax::Trivia::Kind::SingleLineComment) {
    return text.size() >= 2 ? text.substr(2) : string_view();
  }
  if (text.size() >= 4) {
    return text.substr(2, text.size() - 4);
  }
  return string_view();
}

struct Keyword {
  string_view text;
  DirectiveKind kind;
};

// Longest first, so `-disable-line` is not read as `-disable`.
constexpr Keyword kKeywords[] = {
    {"-disable-next-line", DirectiveKind::DisableNextLine},
    {"-disable-line", DirectiveKind::DisableLine},
    {"-disable", DirectiveKind::Disable},
    {"-enable", DirectiveKind::Enable},
};

const char *keywordName(DirectiveKind kind)
{
  switch (kind) {
  case DirectiveKind::Disable:
    return "disable";
  case DirectiveKind::Enable:
    return "enable";
  case DirectiveKind::DisableLine:
    return "disable-line";
  case DirectiveKind::DisableNextLine:
    return "disable-next-line";
  }
  return "";
}

/** Splits `text` at the first ` -- ` justification separator. */
string_view withoutJustification(string_view text)
{
  for (size_t i = 1; i + 1 < text.size(); i++) {
    if (text[i] == '-' && text[i + 1] == '-' && isSpace(text[i - 1])) {
      size_t j = i;
      while (j < text.size() && text[j] == '-') {
        j++;
      }
      if (j == text.size() || isSpace(text[j])) {
        return text.substr(0, i);
      }
    }
  }
  return text;
}

string concat(std::initializer_list<string_view> parts)
{
  string out;
  for (string_view part : parts) {
    for (char c : part) {
      out += c;
    }
  }
  return out;
}

} // namespace

void DirectiveSet::collect(const syntax::GrammarTree &tree,
                           const Registry &registry,
                           bool eslintCompat)
{
  m_directives.clear();
  m_problems.clear();
  m_next = 0;
  m_disableAll = -1;
  m_disabled.clear();
  m_enabled.clear();

  string_view source = tree.source();
  for (const syntax::Trivia &trivia : tree.trivia()) {
    if (trivia.kind != syntax::Trivia::Kind::SingleLineComment &&
        trivia.kind != syntax::Trivia::Kind::MultiLineComment)
    {
      continue;
    }
    string_view body = trim(commentBody(source, trivia));
    if (startsWith(body, "fastlint-")) {
      addDirective(tree, registry, trivia, body.substr(8), false);
    } else if (eslintCompat && startsWith(body, "eslint-")) {
      addDirective(tree, registry, trivia, body.substr(6), true);
    }
  }
}

void DirectiveSet::addDirective(const syntax::GrammarTree &tree,
                                const Registry &registry,
                                const syntax::Trivia &comment,
                                string_view body,
                                bool eslint)
{
  const Keyword *keyword = nullptr;
  for (const Keyword &k : kKeywords) {
    if (startsWith(body, k.text) &&
        (body.size() == k.text.size() || isSpace(body[k.text.size()])))
    {
      keyword = &k;
      break;
    }
  }
  if (!keyword) {
    return;
  }
  string_view prefix = eslint ? "eslint" : "fastlint";
  bool lineDirective = keyword->kind == DirectiveKind::DisableLine ||
                       keyword->kind == DirectiveKind::DisableNextLine;
  if (lineDirective && comment.lineBreak) {
    m_problems.append({comment.offset,
                       comment.length,
                       concat({prefix,
                               "-",
                               keywordName(keyword->kind),
                               " comment should not span multiple lines."})});
    return;
  }

  uint32_t line = tree.lineOf(comment.offset);
  if (keyword->kind == DirectiveKind::DisableNextLine) {
    line = tree.lineOf(comment.offset + comment.length - 1) + 1;
  }
  string_view list = trim(withoutJustification(body.substr(keyword->text.size())));
  if (list.empty()) {
    m_directives.append(
        {keyword->kind, comment.offset, comment.length, line, {}, eslint});
    return;
  }
  size_t at = 0;
  while (at <= list.size()) {
    size_t comma = list.find(',', at);
    string_view name = trim(
        list.substr(at, comma == string_view::npos ? string_view::npos : comma - at));
    if (!name.empty()) {
      const RuleDef *rule = registry.find(name);
      if (rule) {
        m_directives.append({keyword->kind,
                             comment.offset,
                             comment.length,
                             line,
                             rule->meta.name,
                             eslint});
      } else if (!eslint) {
        m_problems.append({comment.offset,
                           comment.length,
                           concat({"Definition for rule '", name, "' was not found."})});
      }
    }
    if (comma == string_view::npos) {
      break;
    }
    at = comma + 1;
  }
}

void DirectiveSet::advance(uint32_t offset)
{
  for (; m_next < int(m_directives.size()); m_next++) {
    Directive &d = m_directives[m_next];
    if (d.offset > offset) {
      break;
    }
    if (d.kind == DirectiveKind::Disable) {
      if (d.rule.empty()) {
        m_disableAll = m_next;
        m_disabled.clear();
        m_enabled.clear();
      } else {
        m_enabled.remove(d.rule);
        m_disabled.append(m_next);
      }
    } else if (d.kind == DirectiveKind::Enable) {
      if (d.rule.empty()) {
        m_disableAll = -1;
        m_disabled.clear();
        m_enabled.clear();
      } else {
        for (int i = int(m_disabled.size()) - 1; i >= 0; i--) {
          int index = m_disabled[i];
          if (m_directives[index].rule == d.rule) {
            m_disabled.remove(index);
          }
        }
        if (m_disableAll >= 0 && !m_enabled.contains(d.rule)) {
          m_enabled.append(d.rule);
        }
      }
    }
  }
}

int DirectiveSet::suppressor(string_view rule, uint32_t offset, uint32_t line)
{
  for (int i = 0; i < int(m_directives.size()); i++) {
    Directive &d = m_directives[i];
    bool lineDirective =
        d.kind == DirectiveKind::DisableLine || d.kind == DirectiveKind::DisableNextLine;
    if (lineDirective && d.line == line && (d.rule.empty() || d.rule == rule)) {
      d.used = true;
      return i;
    }
  }
  advance(offset);
  // The most recent per-rule directive wins over a blanket one, as in ESLint.
  for (int i = int(m_disabled.size()) - 1; i >= 0; i--) {
    if (m_directives[m_disabled[i]].rule == rule) {
      m_directives[m_disabled[i]].used = true;
      return m_disabled[i];
    }
  }
  if (m_disableAll >= 0 && !m_enabled.contains(rule)) {
    m_directives[m_disableAll].used = true;
    return m_disableAll;
  }
  return -1;
}

} // namespace fastlint::lint
