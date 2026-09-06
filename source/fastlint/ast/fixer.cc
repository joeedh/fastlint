#include "fastlint/ast/fixer.h"

#include "fastlint/ast/kind_info.h"

namespace fastlint::ast {

void markDirty(Node *node)
{
  for (Node *n = node; n; n = n->parent) {
    n->dirty = true;
  }
}

bool isAttached(const AstFile &file, const Node *node)
{
  const Node *n = node;
  while (n->parent) {
    n = n->parent;
  }
  return n == file.root();
}

namespace {

/** Shifts the tail right and stores `value` at `index`. */
void insertAt(Vector<Node *, 3> &children, int index, Node *value)
{
  children.append(nullptr);
  for (int i = int(children.size()) - 1; i > index; i--) {
    children[i] = children[i - 1];
  }
  children[index] = value;
}

/** Whether `source` has a line break between `from` and `to`. */
bool newlineBetween(string_view source, uint32_t from, uint32_t to)
{
  if (to > source.size() || from >= to) {
    return false;
  }
  for (uint32_t i = from; i < to; i++) {
    char c = source[i];
    if (c == '\n' || c == '\r') {
      return true;
    }
  }
  return false;
}

} // namespace

// ------------------------------------------------------------- mutations

void Fixer::dirty(Node *node)
{
  for (Node *n = node; n; n = n->parent) {
    if (n->dirty) {
      continue;
    }
    m_file.captureLayout(n);
    n->dirty = true;
  }
}

void Fixer::detach(Node *node)
{
  Node *parent = node->parent;
  if (!parent) {
    return;
  }
  int index = indexOf(parent, node);
  if (index < 0) {
    node->parent = nullptr;
    return;
  }
  dirty(parent);
  if (isListIndex(parent, index)) {
    parent->children.remove_at(index);
  } else {
    parent->children[index] = nullptr;
  }
  node->parent = nullptr;
}

int Fixer::indexOf(const Node *parent, const Node *child)
{
  for (size_t i = 0; i < parent->children.size(); i++) {
    if (parent->children[int(i)] == child) {
      return int(i);
    }
  }
  return -1;
}

bool Fixer::isListIndex(const Node *parent, int index)
{
  const KindInfo &info = kindInfo(parent->kind);
  return info.hasList && index >= info.fixedChildren;
}

bool Fixer::replace(Node *old, Node *fresh)
{
  Node *parent = old ? old->parent : nullptr;
  if (!parent || !fresh || fresh == old) {
    return false;
  }
  int index = indexOf(parent, old);
  if (index < 0) {
    return false;
  }
  detach(fresh);
  dirty(parent);
  parent->children[index] = fresh;
  fresh->parent = parent;
  old->parent = nullptr;
  // Leading and trailing comments sit in the parent's own text and still
  // print; dangling ones were inside `old` and must print from the table.
  if (const CommentList *list = m_file.comments(old)) {
    for (const Comment &c : *list) {
      if (c.place == CommentPlace::Dangling) {
        m_file.markDead(c.offset, c.length);
      }
    }
  }
  m_file.moveComments(old, fresh);
  if (CommentList *list = fresh ? &m_file.commentsFor(fresh) : nullptr) {
    for (Comment &c : *list) {
      if (c.place == CommentPlace::Dangling) {
        c.moved = true;
      }
    }
  }
  return true;
}

bool Fixer::insertBefore(Node *sibling, Node *fresh)
{
  Node *parent = sibling ? sibling->parent : nullptr;
  int index = parent ? indexOf(parent, sibling) : -1;
  if (index < 0 || !fresh || !isListIndex(parent, index)) {
    return false;
  }
  detach(fresh);
  dirty(parent);
  index = indexOf(parent, sibling);
  insertAt(parent->children, index, fresh);
  fresh->parent = parent;
  return true;
}

bool Fixer::insertAfter(Node *sibling, Node *fresh)
{
  Node *parent = sibling ? sibling->parent : nullptr;
  int index = parent ? indexOf(parent, sibling) : -1;
  if (index < 0 || !fresh || !isListIndex(parent, index)) {
    return false;
  }
  detach(fresh);
  dirty(parent);
  index = indexOf(parent, sibling);
  insertAt(parent->children, index + 1, fresh);
  fresh->parent = parent;
  return true;
}

bool Fixer::append(Node *parent, Node *fresh)
{
  if (!parent || !fresh || !kindInfo(parent->kind).hasList) {
    return false;
  }
  detach(fresh);
  dirty(parent);
  parent->appendChild(fresh);
  return true;
}

void Fixer::moveRemovedComments(Node *node, Node *parent, int index, CommentPolicy policy)
{
  const CommentList *list = m_file.comments(node);
  if (!list || list->size() == 0) {
    return;
  }
  if (policy == CommentPolicy::DropAll) {
    for (const Comment &c : *list) {
      m_file.markDead(c.offset, c.length);
    }
    m_file.dropComments(node);
    return;
  }
  string_view source = m_file.grammar() ? m_file.grammar()->source() : string_view();
  // The neighbour that inherits the comments; `before` means they lead it.
  Node *next = nullptr;
  Node *prev = nullptr;
  if (isListIndex(parent, index)) {
    for (int i = index + 1; i < int(parent->children.size()) && !next; i++) {
      next = parent->children[i];
    }
    for (int i = index - 1; i >= kindInfo(parent->kind).fixedChildren && !prev; i--) {
      prev = parent->children[i];
    }
  }
  CommentList moved;
  for (const Comment &c : *list) {
    // The text around the removed node goes with it, so every comment
    // either dies here or is printed again from the table.
    m_file.markDead(c.offset, c.length);
    Comment copy = c;
    copy.moved = true;
    if (c.place == CommentPlace::Trailing && policy == CommentPolicy::MoveLeading &&
        !newlineBetween(source, node->end, c.offset))
    {
      continue;
    }
    moved.append(copy);
  }
  m_file.dropComments(node);
  if (moved.size() == 0) {
    return;
  }
  Node *target = next ? next : prev ? prev : parent;
  CommentList &dest = m_file.commentsFor(target);
  for (Comment &c : moved) {
    if (next) {
      c.place = CommentPlace::Leading;
    } else if (prev) {
      c.place = CommentPlace::Trailing;
    } else {
      c.place = CommentPlace::Dangling;
    }
    dest.append(c);
  }
}

bool Fixer::remove(Node *node, CommentPolicy policy)
{
  Node *parent = node ? node->parent : nullptr;
  int index = parent ? indexOf(parent, node) : -1;
  if (index < 0) {
    return false;
  }
  const KindInfo &info = kindInfo(parent->kind);
  bool list = isListIndex(parent, index);
  bool required = !list && (info.requiredMask & (1u << index)) != 0;
  if (required) {
    return false;
  }
  dirty(parent);
  moveRemovedComments(node, parent, index, policy);
  if (list) {
    parent->children.remove_at(index);
  } else {
    parent->children[index] = nullptr;
  }
  node->parent = nullptr;
  return true;
}

bool Fixer::set(Node *parent, int index, Node *fresh)
{
  if (!parent || index < 0 || index >= kindInfo(parent->kind).fixedChildren) {
    return false;
  }
  Node *old = parent->children[index];
  if (!fresh) {
    return old ? remove(old) : true;
  }
  if (old) {
    return replace(old, fresh);
  }
  detach(fresh);
  dirty(parent);
  parent->children[index] = fresh;
  fresh->parent = parent;
  return true;
}

// -------------------------------------------------------------- builders

Node *Fixer::build(NodeKind kind, std::initializer_list<Node *> children)
{
  const KindInfo &info = kindInfo(kind);
  bool tooFew = children.size() < info.fixedChildren;
  bool tooMany = !info.hasList && children.size() > info.fixedChildren;
  if (tooFew || tooMany) {
    return nullptr;
  }
  Node *n = m_file.make(kind);
  n->dirty = true;
  for (Node *c : children) {
    n->appendChild(c);
  }
  return n;
}

Node *Fixer::identifier(string_view name)
{
  Node *n = build(NodeKind::Identifier, {nullptr});
  n->text = m_file.intern(name);
  return n;
}

Node *Fixer::literal(LiteralKind kind, string_view text)
{
  Node *n = build(NodeKind::Literal, {});
  n->text = m_file.intern(text);
  n->setDataByte(0, uint8_t(kind));
  return n;
}

Node *Fixer::stringLiteral(string_view value)
{
  litestl::util::string quoted;
  quoted += '"';
  for (char c : value) {
    quoted += c;
  }
  quoted += '"';
  return literal(LiteralKind::String, string_view(quoted.c_str(), quoted.size()));
}

Node *Fixer::numberLiteral(string_view text)
{
  return literal(LiteralKind::Number, text);
}

Node *Fixer::booleanLiteral(bool value)
{
  return literal(LiteralKind::Boolean, value ? "true" : "false");
}

Node *Fixer::nullLiteral()
{
  return literal(LiteralKind::Null, "null");
}

Node *Fixer::member(Node *object, Node *property, bool computed)
{
  Node *n = build(NodeKind::MemberExpression, {object, property});
  n->setFlag(Flag::Computed, computed);
  return n;
}

Node *Fixer::call(Node *callee, std::initializer_list<Node *> arguments)
{
  Node *n = build(NodeKind::CallExpression, {callee, nullptr});
  for (Node *a : arguments) {
    n->appendChild(a);
  }
  return n;
}

Node *Fixer::call(Node *callee, span<Node *> arguments)
{
  Node *n = build(NodeKind::CallExpression, {callee, nullptr});
  for (Node *a : arguments) {
    n->appendChild(a);
  }
  return n;
}

Node *Fixer::unary(UnaryOperator op, Node *argument)
{
  Node *n = build(NodeKind::UnaryExpression, {argument});
  n->setDataByte(0, uint8_t(op));
  return n;
}

Node *Fixer::binary(BinaryOperator op, Node *left, Node *right)
{
  Node *n = build(NodeKind::BinaryExpression, {left, right});
  n->setDataByte(0, uint8_t(op));
  return n;
}

Node *Fixer::logical(LogicalOperator op, Node *left, Node *right)
{
  Node *n = build(NodeKind::LogicalExpression, {left, right});
  n->setDataByte(0, uint8_t(op));
  return n;
}

Node *Fixer::awaitExpression(Node *argument)
{
  return build(NodeKind::AwaitExpression, {argument});
}

Node *Fixer::expressionStatement(Node *expression)
{
  return build(NodeKind::ExpressionStatement, {expression});
}

Node *Fixer::returnStatement(Node *argument)
{
  return build(NodeKind::ReturnStatement, {argument});
}

Node *Fixer::block(std::initializer_list<Node *> body)
{
  return build(NodeKind::BlockStatement, body);
}

Node *Fixer::variableDeclaration(VariableKind kind, Node *id, Node *init)
{
  Node *declarator = build(NodeKind::VariableDeclarator, {id, init});
  Node *n = build(NodeKind::VariableDeclaration, {declarator});
  n->setDataByte(0, uint8_t(kind));
  return n;
}

Node *Fixer::keywordType(Keyword keyword)
{
  Node *n = build(NodeKind::TSKeywordType, {});
  n->setDataByte(0, uint8_t(keyword));
  return n;
}

Node *Fixer::typeReference(Node *typeName)
{
  return build(NodeKind::TSTypeReference, {typeName, nullptr});
}

// ------------------------------------------------------------ applyFixes

FixReport applyFixes(AstFile &file, span<Fix> fixes)
{
  Vector<int> order;
  for (size_t i = 0; i < fixes.size(); i++) {
    order.append(int(i));
  }
  order.sort([&](int a, int b) {
    uint32_t sa = fixes[a].target->start;
    uint32_t sb = fixes[b].target->start;
    if (sa != sb) {
      return sa < sb ? -1 : 1;
    }
    return a < b ? -1 : a > b ? 1 : 0;
  });
  FixReport report;
  Fixer fixer(file);
  for (int i : order) {
    Fix &fix = fixes[i];
    if (!fix.target || fix.target->dirty || !isAttached(file, fix.target)) {
      report.deferred++;
      continue;
    }
    fix.apply(fixer);
    report.applied++;
  }
  return report;
}

} // namespace fastlint::ast
