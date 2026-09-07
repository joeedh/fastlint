#include "fastlint/cache/imports.h"

#include "fastlint/ast/node.h"

namespace fastlint::cache {

namespace {

using ast::LiteralKind;
using ast::Node;
using ast::NodeKind;

bool isStringLiteral(const Node *n)
{
  return n && n->kind == NodeKind::Literal &&
         LiteralKind(n->dataByte(0)) == LiteralKind::String;
}

const Node *childOrNull(const Node *n, int index)
{
  return n && index < int(n->children.size()) ? n->children[index] : nullptr;
}

/** The string literal that names a module for `n`, or null when the node does not name
 * one (an export without a source, a dynamic import of a computed expression). */
const Node *specifierOf(const Node *n)
{
  switch (n->kind) {
  case NodeKind::ImportDeclaration:
  case NodeKind::ImportExpression:
  case NodeKind::TSExternalModuleReference:
    return childOrNull(n, 0);
  case NodeKind::ExportNamedDeclaration:
  case NodeKind::ExportAllDeclaration:
    return childOrNull(n, 1);
  case NodeKind::TSImportType: {
    const Node *argument = childOrNull(n, 0);
    return argument && argument->kind == NodeKind::TSLiteralType
               ? childOrNull(argument, 0)
               : argument;
  }
  case NodeKind::CallExpression: {
    const Node *callee = childOrNull(n, 0);
    if (!callee || callee->kind != NodeKind::Identifier || callee->text != "require") {
      return nullptr;
    }
    // Fixed slots are the callee and the type arguments; the first argument follows.
    return childOrNull(n, 2);
  }
  default:
    return nullptr;
  }
}

bool contains(const Vector<string> &items, std::string_view text)
{
  for (const string &item : items) {
    if (std::string_view(item.c_str(), item.size()) == text) {
      return true;
    }
  }
  return false;
}

} // namespace

void collectImports(const ast::AstFile &file, Vector<string> &specifiers)
{
  for (const ast::PreorderEntry &entry : file.preorder()) {
    const Node *literal = specifierOf(entry.node);
    if (!isStringLiteral(literal) || literal->text.size() < 2) {
      continue;
    }
    std::string_view text = literal->text.substr(1, literal->text.size() - 2);
    if (text.empty() || contains(specifiers, text)) {
      continue;
    }
    string copy;
    for (char c : text) {
      copy += c;
    }
    specifiers.append(std::move(copy));
  }
}

} // namespace fastlint::cache
