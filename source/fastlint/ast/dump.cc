#include "fastlint/ast/dump.h"

#include "fastlint/ast/kind_info.h"

namespace fastlint::ast {

namespace {

constexpr int kMaxDepth = 200;

struct Dumper {
  const AstFile &file;
  string &out;

  void put(string_view text)
  {
    for (char c : text) {
      out += c;
    }
  }

  void putNumber(uint32_t value)
  {
    char buffer[16];
    int n = 0;
    do {
      buffer[n++] = char('0' + value % 10);
      value /= 10;
    } while (value > 0);
    while (n > 0) {
      out += buffer[--n];
    }
  }

  void putQuoted(string_view text)
  {
    out += '"';
    for (char c : text) {
      switch (c) {
      case '"':
        put("\\\"");
        break;
      case '\\':
        put("\\\\");
        break;
      case '\n':
        put("\\n");
        break;
      case '\r':
        put("\\r");
        break;
      case '\t':
        put("\\t");
        break;
      default:
        out += c;
        break;
      }
    }
    out += '"';
  }

  void indent(int depth)
  {
    for (int i = 0; i < depth * 2; i++) {
      out += ' ';
    }
  }

  void slot(const Node *child, int depth)
  {
    if (!child) {
      out += '-';
    } else {
      node(child, depth);
    }
  }

  /** The head line of a node: everything up to its comments and children. */
  void head(const Node *n)
  {
    const KindInfo &info = kindInfo(n->kind);
    out += '(';
    put(info.name);
    if (info.usesText) {
      out += ' ';
      putQuoted(n->text);
    }
    for (int bit = 0; bit < flagCount; bit++) {
      if (n->flags & (1u << bit)) {
        put(" :");
        put(flagName(bit));
      }
    }
    for (int i = 0; i < info.enumCount; i++) {
      const EnumField &field = info.enums[i];
      uint8_t value = n->dataByte(i);
      out += ' ';
      put(field.name);
      out += '=';
      put(value < field.count ? field.values[value] : "?");
    }
    if (n->dirty) {
      put(" :dirty");
    }
    put(" @");
    putNumber(n->start);
    out += '-';
    putNumber(n->end);
  }

  /** One line per attached comment; returns whether any were printed. */
  bool comments(const Node *n, int depth)
  {
    const CommentList *list = file.comments(n);
    if (!list || list->size() == 0) {
      return false;
    }
    string_view source = file.grammar() ? file.grammar()->source() : string_view();
    for (const Comment &c : *list) {
      out += '\n';
      indent(depth + 1);
      switch (c.place) {
      case CommentPlace::Leading:
        put(";leading ");
        break;
      case CommentPlace::Trailing:
        put(";trailing ");
        break;
      case CommentPlace::Dangling:
        put(";dangling ");
        break;
      }
      putQuoted(source.substr(c.offset, c.length));
    }
    return true;
  }

  void node(const Node *n, int depth)
  {
    const KindInfo &info = kindInfo(n->kind);
    head(n);
    bool multiLine = comments(n, depth);
    if (n->children.size() == 0) {
      if (multiLine) {
        out += '\n';
        indent(depth);
      }
      out += ')';
      return;
    }
    if (depth >= kMaxDepth) {
      put(" ...)");
      return;
    }
    size_t fixed = info.fixedChildren;
    for (size_t i = 0; i < n->children.size(); i++) {
      if (i < fixed) {
        out += '\n';
        indent(depth + 1);
        put(i < info.childCount ? info.childNames[i] : "?");
        out += ' ';
        slot(n->children[int(i)], depth + 1);
      } else {
        if (i == fixed) {
          out += '\n';
          indent(depth + 1);
          put(info.hasList && fixed < info.childCount ? info.childNames[fixed] : "?");
        }
        out += '\n';
        indent(depth + 2);
        slot(n->children[int(i)], depth + 2);
      }
    }
    out += '\n';
    indent(depth);
    out += ')';
  }
};

} // namespace

void dumpAst(const AstFile &file, string &out)
{
  Dumper dumper{file, out};
  if (!file.root()) {
    dumper.put("-\n");
    return;
  }
  dumper.node(file.root(), 0);
  out += '\n';
}

} // namespace fastlint::ast
