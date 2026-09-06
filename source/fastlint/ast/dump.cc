#include "fastlint/ast/dump.h"

#include "fastlint/ast/kind_info.h"

namespace fastlint::ast {

namespace {

constexpr int kMaxDepth = 200;

void put(string &out, string_view text)
{
  for (char c : text) {
    out += c;
  }
}

void putNumber(string &out, uint32_t value)
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

void putQuoted(string &out, string_view text)
{
  out += '"';
  for (char c : text) {
    switch (c) {
    case '"':
      put(out, "\\\"");
      break;
    case '\\':
      put(out, "\\\\");
      break;
    case '\n':
      put(out, "\\n");
      break;
    case '\r':
      put(out, "\\r");
      break;
    case '\t':
      put(out, "\\t");
      break;
    default:
      out += c;
      break;
    }
  }
  out += '"';
}

void indent(string &out, int depth)
{
  for (int i = 0; i < depth * 2; i++) {
    out += ' ';
  }
}

void dumpNode(const Node *n, string &out, int depth);

void dumpSlot(const Node *child, string &out, int depth)
{
  if (!child) {
    out += '-';
  } else {
    dumpNode(child, out, depth);
  }
}

/** The head line of a node: everything up to its children. */
void dumpHead(const Node *n, string &out)
{
  const KindInfo &info = kindInfo(n->kind);
  out += '(';
  put(out, info.name);
  if (info.usesText) {
    out += ' ';
    putQuoted(out, n->text);
  }
  for (int bit = 0; bit < flagCount; bit++) {
    if (n->flags & (1u << bit)) {
      put(out, " :");
      put(out, flagName(bit));
    }
  }
  for (int i = 0; i < info.enumCount; i++) {
    const EnumField &field = info.enums[i];
    uint8_t value = n->dataByte(i);
    out += ' ';
    put(out, field.name);
    out += '=';
    put(out, value < field.count ? field.values[value] : "?");
  }
  if (n->dirty) {
    put(out, " :dirty");
  }
  put(out, " @");
  putNumber(out, n->start);
  out += '-';
  putNumber(out, n->end);
}

void dumpNode(const Node *n, string &out, int depth)
{
  const KindInfo &info = kindInfo(n->kind);
  dumpHead(n, out);
  if (n->children.size() == 0) {
    out += ')';
    return;
  }
  if (depth >= kMaxDepth) {
    put(out, " ...)");
    return;
  }
  size_t fixed = info.fixedChildren;
  for (size_t i = 0; i < n->children.size(); i++) {
    if (i < fixed) {
      out += '\n';
      indent(out, depth + 1);
      put(out, i < info.childCount ? info.childNames[i] : "?");
      out += ' ';
      dumpSlot(n->children[int(i)], out, depth + 1);
    } else {
      if (i == fixed) {
        out += '\n';
        indent(out, depth + 1);
        put(out, info.hasList && fixed < info.childCount ? info.childNames[fixed] : "?");
      }
      out += '\n';
      indent(out, depth + 2);
      dumpSlot(n->children[int(i)], out, depth + 2);
    }
  }
  out += '\n';
  indent(out, depth);
  out += ')';
}

} // namespace

void dumpAst(const Node *root, string &out)
{
  if (!root) {
    put(out, "-\n");
    return;
  }
  dumpNode(root, out, 0);
  out += '\n';
}

} // namespace fastlint::ast
