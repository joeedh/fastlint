#include "fastlint/ast/template.h"

#include "fastlint/ast/fixer.h"
#include "fastlint/ast/generated/views.h"
#include "fastlint/ast/kind_info.h"
#include "fastlint/ast/lower.h"
#include "fastlint/ast/precedence.h"
#include "fastlint/syntax/parser.h"
#include "util/alloc.h"
#include "util/map.h"

#include <mutex>
#include <new>

namespace fastlint::ast {

using litestl::util::Map;

// ------------------------------------------------------------- bindings

TemplateArg *TemplateArgs::find(string_view name) const
{
  Vector<TemplateArg, 4> &items = const_cast<Vector<TemplateArg, 4> &>(m_items);
  for (size_t i = 0; i < items.size(); i++) {
    if (items[int(i)].name == name) {
      return &items[int(i)];
    }
  }
  return nullptr;
}

void TemplateArgs::set(string_view name, Node *node)
{
  TemplateArg *b = find(name);
  if (!b) {
    m_items.append(TemplateArg{name, {}});
    b = &m_items.last();
  }
  b->nodes.clear();
  b->nodes.append(node);
}

void TemplateArgs::set(string_view name, span<Node *const> nodes)
{
  TemplateArg *b = find(name);
  if (!b) {
    m_items.append(TemplateArg{name, {}});
    b = &m_items.last();
  }
  b->nodes.clear();
  for (Node *n : nodes) {
    b->nodes.append(n);
  }
}

Node *TemplateArgs::get(string_view name) const
{
  TemplateArg *b = find(name);
  return b && b->nodes.size() == 1 ? b->nodes[0] : nullptr;
}

span<Node *const> TemplateArgs::list(string_view name) const
{
  TemplateArg *b = find(name);
  if (!b) {
    return {};
  }
  return {b->nodes.data(), b->nodes.size()};
}

bool TemplateArgs::has(string_view name) const
{
  return find(name) != nullptr;
}

// ------------------------------------------------------------- helpers

namespace {

constexpr uint32_t kIgnoredFlags =
    uint32_t(Flag::Parenthesized) | uint32_t(Flag::Incomplete);

bool isPlaceholderText(string_view text)
{
  return text.size() > 1 && text[0] == '$' && text[1] != '$';
}

bool isEscapeText(string_view text)
{
  return text.size() > 1 && text[0] == '$' && text[1] == '$';
}

int indexOf(const Node *parent, const Node *child)
{
  for (size_t i = 0; i < parent->children.size(); i++) {
    if (parent->children[int(i)] == child) {
      return int(i);
    }
  }
  return -1;
}

bool isListIndex(const Node *parent, int index)
{
  const KindInfo &info = kindInfo(parent->kind);
  return info.hasList && index >= info.fixedChildren;
}

/** Shifts the tail right and stores `value` at `index`. */
void insertAt(Vector<Node *, 3> &children, int index, Node *value)
{
  children.append(nullptr);
  for (int i = int(children.size()) - 1; i > index; i--) {
    children[i] = children[i - 1];
  }
  children[index] = value;
}

/** Whether slot `index` of `parent` holds a statement. */
bool isStatementSlot(const Node *parent, int index)
{
  if (!parent) {
    return true;
  }
  switch (parent->kind) {
  case NodeKind::Program:
  case NodeKind::BlockStatement:
  case NodeKind::TSModuleBlock:
  case NodeKind::StaticBlock:
  case NodeKind::SwitchCase:
    return isListIndex(parent, index);
  case NodeKind::IfStatement:
    return index == 1 || index == 2;
  case NodeKind::ForStatement:
    return index == 3;
  case NodeKind::ForInStatement:
  case NodeKind::ForOfStatement:
    return index == 2;
  case NodeKind::WhileStatement:
  case NodeKind::LabeledStatement:
  case NodeKind::WithStatement:
    return index == 1;
  case NodeKind::DoWhileStatement:
    return index == 0;
  default:
    return false;
  }
}

/** The element kind of `parent`'s list slot. */
SlotKind listSlot(const Node *parent)
{
  switch (parent->kind) {
  case NodeKind::Program:
  case NodeKind::BlockStatement:
  case NodeKind::TSModuleBlock:
  case NodeKind::StaticBlock:
  case NodeKind::SwitchCase:
    return SlotKind::Statement;
  case NodeKind::CallExpression:
  case NodeKind::NewExpression:
  case NodeKind::ArrayExpression:
  case NodeKind::SequenceExpression:
  case NodeKind::TemplateLiteral:
    return SlotKind::Expression;
  case NodeKind::FunctionDeclaration:
  case NodeKind::FunctionExpression:
  case NodeKind::ArrowFunctionExpression:
  case NodeKind::TSDeclareFunction:
  case NodeKind::TSEmptyBodyFunctionExpression:
  case NodeKind::ArrayPattern:
    return SlotKind::Pattern;
  case NodeKind::TSUnionType:
  case NodeKind::TSIntersectionType:
  case NodeKind::TSTupleType:
  case NodeKind::TSTypeParameterInstantiation:
    return SlotKind::Type;
  default:
    return SlotKind::Any;
  }
}

/** Whether `n` may fill a `slot`; a null `n` never does. */
bool accepts(SlotKind slot, const Node *n)
{
  if (!n) {
    return false;
  }
  switch (slot) {
  case SlotKind::Expression:
    return n->isExpression() || n->kind == NodeKind::SpreadElement;
  case SlotKind::Statement:
    return n->isStatement() || n->isExpression();
  case SlotKind::Type:
    return n->isType() || n->kind == NodeKind::Identifier ||
           n->kind == NodeKind::TSQualifiedName;
  case SlotKind::Name:
    return n->kind == NodeKind::Identifier;
  case SlotKind::Property:
    return n->kind == NodeKind::Identifier || n->kind == NodeKind::PrivateIdentifier;
  case SlotKind::PropertyName:
    return n->kind == NodeKind::Identifier || n->kind == NodeKind::PrivateIdentifier ||
           n->kind == NodeKind::Literal;
  case SlotKind::Pattern:
    return n->isPattern();
  case SlotKind::Assignable:
    return n->isPattern() || n->kind == NodeKind::MemberExpression;
  case SlotKind::Any:
    return true;
  }
  return false;
}

/** A splice's elements must all be of the list's own kind. */
bool acceptsSpliced(SlotKind slot, const Node *n)
{
  if (slot == SlotKind::Statement) {
    return n && n->isStatement();
  }
  return accepts(slot, n);
}

bool isFunctionLike(NodeKind kind)
{
  return FunctionLike::matches(kind);
}

/**
 * The slot an Identifier placeholder occupies, from its parent and index.
 * Returns false for a splice outside a list.
 */
bool classify(const Node *id, bool splice, SlotKind &out)
{
  const Node *parent = id->parent;
  int index = parent ? indexOf(parent, id) : -1;
  if (!parent) {
    out = SlotKind::Expression;
    return !splice;
  }
  bool list = isListIndex(parent, index);
  if (parent->kind == NodeKind::ExpressionStatement && index == 0) {
    const Node *stmt = parent;
    int stmtIndex = stmt->parent ? indexOf(stmt->parent, stmt) : -1;
    if (isStatementSlot(stmt->parent, stmtIndex)) {
      out = SlotKind::Statement;
      return !splice || (stmt->parent && isListIndex(stmt->parent, stmtIndex));
    }
  }
  if (splice) {
    if (!list) {
      return false;
    }
    out = listSlot(parent);
    return true;
  }
  if (list) {
    out = listSlot(parent);
    return true;
  }
  if (parent->kind == NodeKind::Property && parent->hasFlag(Flag::Shorthand)) {
    out = SlotKind::Name;
    return true;
  }
  switch (parent->kind) {
  case NodeKind::TSTypeReference:
    out = parent->children[1] ? SlotKind::Name : SlotKind::Type;
    return true;
  case NodeKind::TSQualifiedName:
  case NodeKind::ImportSpecifier:
  case NodeKind::ImportDefaultSpecifier:
  case NodeKind::ImportNamespaceSpecifier:
  case NodeKind::ExportSpecifier:
  case NodeKind::MetaProperty:
  case NodeKind::TSNamespaceExportDeclaration:
    out = SlotKind::Name;
    return true;
  case NodeKind::TSTypeQuery:
  case NodeKind::TSInterfaceDeclaration:
  case NodeKind::TSTypeAliasDeclaration:
  case NodeKind::TSEnumDeclaration:
  case NodeKind::TSModuleDeclaration:
  case NodeKind::TSImportEqualsDeclaration:
  case NodeKind::LabeledStatement:
  case NodeKind::BreakStatement:
  case NodeKind::ContinueStatement:
  case NodeKind::TSEnumMember:
  case NodeKind::TSTypePredicate:
  case NodeKind::TSNamedTupleMember:
    out = index == 0 ? SlotKind::Name : SlotKind::Expression;
    return true;
  case NodeKind::TSImportType:
    out = index == 1 ? SlotKind::Name : SlotKind::Expression;
    return true;
  case NodeKind::ClassDeclaration:
  case NodeKind::ClassExpression:
    out = index == 1 ? SlotKind::Name : SlotKind::Expression;
    return true;
  case NodeKind::MemberExpression:
    out = index == 1 && !parent->hasFlag(Flag::Computed) ? SlotKind::Property
                                                         : SlotKind::Expression;
    return true;
  case NodeKind::Property:
    if (index == 0) {
      out =
          parent->hasFlag(Flag::Computed) ? SlotKind::Expression : SlotKind::PropertyName;
    } else {
      out = parent->parent && parent->parent->kind == NodeKind::ObjectPattern
                ? SlotKind::Pattern
                : SlotKind::Expression;
    }
    return true;
  case NodeKind::MethodDefinition:
  case NodeKind::TSAbstractMethodDefinition:
  case NodeKind::PropertyDefinition:
  case NodeKind::TSAbstractPropertyDefinition:
  case NodeKind::AccessorProperty:
  case NodeKind::TSAbstractAccessorProperty:
    out = index == 1 && !parent->hasFlag(Flag::Computed) ? SlotKind::PropertyName
                                                         : SlotKind::Expression;
    return true;
  case NodeKind::TSPropertySignature:
  case NodeKind::TSMethodSignature:
  case NodeKind::ImportAttribute:
    out = index == 0 && !parent->hasFlag(Flag::Computed) ? SlotKind::PropertyName
                                                         : SlotKind::Expression;
    return true;
  case NodeKind::VariableDeclarator:
  case NodeKind::CatchClause:
  case NodeKind::AssignmentPattern:
  case NodeKind::RestElement:
    out = index == 0 ? SlotKind::Pattern : SlotKind::Expression;
    return true;
  case NodeKind::TSParameterProperty:
    out = SlotKind::Pattern;
    return true;
  case NodeKind::ForInStatement:
  case NodeKind::ForOfStatement:
    out = index == 0 ? SlotKind::Any : SlotKind::Expression;
    return true;
  case NodeKind::AssignmentExpression:
    out = index == 0 ? SlotKind::Assignable : SlotKind::Expression;
    return true;
  default:
    if (isFunctionLike(parent->kind) && index == 0) {
      out = SlotKind::Name;
      return true;
    }
    out = SlotKind::Expression;
    return true;
  }
}

/** Captures the layout of each ancestor turning dirty, then marks the chain. */
void dirtyChain(AstFile &file, Node *node)
{
  for (Node *n = node; n; n = n->parent) {
    if (n->dirty) {
      continue;
    }
    file.captureLayout(n);
    n->dirty = true;
  }
}

/** The 64-bit FNV-1a hash of the mode and text, the cache key. */
uint64_t cacheKey(string_view text, Template::Mode mode)
{
  uint64_t h = 14695981039346656037ull;
  h = (h ^ uint8_t(mode)) * 1099511628211ull;
  for (char c : text) {
    h = (h ^ uint8_t(c)) * 1099511628211ull;
  }
  return h;
}

} // namespace

// -------------------------------------------------------------- generic

bool equivalent(const Node *a, const Node *b)
{
  if (!a || !b) {
    return a == b;
  }
  if (a->kind != b->kind || (a->flags & ~kIgnoredFlags) != (b->flags & ~kIgnoredFlags) ||
      a->data != b->data)
  {
    return false;
  }
  if (kindInfo(a->kind).usesText && a->text != b->text) {
    return false;
  }
  if (a->children.size() != b->children.size()) {
    return false;
  }
  for (size_t i = 0; i < a->children.size(); i++) {
    if (!equivalent(a->children[int(i)], b->children[int(i)])) {
      return false;
    }
  }
  return true;
}

Node *cloneNode(AstFile &file, const Node *n)
{
  Node *c = file.make(n->kind, n->grammar);
  c->dirty = n->dirty;
  c->flags = n->flags;
  c->data = n->data;
  c->text = n->text;
  c->start = n->start;
  c->end = n->end;
  for (Node *child : n->children) {
    c->appendChild(child ? cloneNode(file, child) : nullptr);
  }
  return c;
}

// -------------------------------------------------------------- compile

const Template *Template::compile(string_view text, Mode mode)
{
  struct Cache {
    std::mutex mutex;
    Map<int, Template *> buckets;
  };
  static Cache cache;

  uint64_t hash = cacheKey(text, mode);
  int key = int(uint32_t(hash ^ (hash >> 32)));
  std::lock_guard<std::mutex> lock(cache.mutex);
  Template **bucket = cache.buckets.lookup_ptr(key);
  for (Template *t = bucket ? *bucket : nullptr; t; t = t->m_nextInBucket) {
    if (t->m_requested == mode &&
        string_view(t->m_text.c_str(), t->m_text.size()) == text)
    {
      return t;
    }
  }
  litestl::alloc::PermanentGuard guard;
  void *mem = litestl::alloc::alloc("ast::Template", sizeof(Template));
  Template *fresh = new (mem) Template(text, mode);
  if (bucket) {
    fresh->m_nextInBucket = *bucket;
    *bucket = fresh;
  } else {
    cache.buckets.add(key, fresh);
  }
  return fresh;
}

Template::Template(string_view text, Mode mode)
    : m_requested(mode), m_mode(mode), m_file(&m_tree)
{
  for (char c : text) {
    m_text += c;
  }
  const char *prefix = "";
  const char *suffix = "";
  if (mode == Mode::Expression) {
    prefix = "(";
    suffix = ")";
  } else if (mode == Mode::Type) {
    prefix = "type $__ = ";
    suffix = ";";
  }
  m_source += prefix;
  for (char c : text) {
    m_source += c;
  }
  m_source += suffix;

  string_view source(m_source.c_str(), m_source.size());
  syntax::Parser::Options options;
  syntax::Parser parser(source, options, m_diagnostics);
  parser.parseFile(m_tree);
  if (!m_diagnostics.empty()) {
    return;
  }
  Node *program = lower(m_tree, m_file);
  if (!program) {
    return;
  }
  Vector<Node *, 3> &body = program->children;
  Node *only = body.size() == 1 ? body[0] : nullptr;
  switch (mode) {
  case Mode::Auto:
    if (only && only->kind == NodeKind::ExpressionStatement &&
        !only->hasFlag(Flag::Directive))
    {
      m_mode = Mode::Expression;
      m_proto = only->children[0];
    } else if (only) {
      m_mode = Mode::Statement;
      m_proto = only;
    } else {
      m_mode = Mode::Statements;
      m_proto = program;
    }
    break;
  case Mode::Expression: {
    Node *expr =
        only && only->kind == NodeKind::ExpressionStatement ? only->children[0] : nullptr;
    if (!expr) {
      return;
    }
    // The wrapping parentheses are the template's, not the snippet's.
    expr->setFlag(Flag::Parenthesized, false);
    expr->start += 1;
    expr->end -= 1;
    m_proto = expr;
    break;
  }
  case Mode::Statement:
    if (!only) {
      return;
    }
    m_proto = only;
    break;
  case Mode::Statements:
    m_proto = program;
    break;
  case Mode::Type: {
    Node *alias = only && only->kind == NodeKind::TSTypeAliasDeclaration ? only : nullptr;
    if (!alias || !alias->children[2]) {
      return;
    }
    m_proto = alias->children[2];
    break;
  }
  }
  m_ok = true;
  collect(m_proto);
}

void Template::collect(const Node *n)
{
  if (!m_ok) {
    return;
  }
  if (n->kind == NodeKind::Identifier && isPlaceholderText(n->text)) {
    Placeholder p;
    p.slot = SlotKind::Expression;
    bool splice = n->text.size() > 2 && n->text.back() == '$';
    p.name = n->text.substr(1, n->text.size() - (splice ? 2 : 1));
    p.splice = splice;
    p.id = n;
    if (!classify(n, splice, p.slot)) {
      m_ok = false;
      return;
    }
    m_placeholders.append(p);
  }
  for (const Node *c : n->children) {
    if (c) {
      collect(c);
    }
  }
}

int Template::placeholderIndex(const Node *id) const
{
  for (size_t i = 0; i < m_placeholders.size(); i++) {
    if (m_placeholders[int(i)].id == id) {
      return int(i);
    }
  }
  return -1;
}

const Placeholder *Template::placeholderFor(const Node *id) const
{
  int index = placeholderIndex(id);
  return index < 0 ? nullptr : &m_placeholders[index];
}

// ---------------------------------------------------------- instantiate

Node *Template::build(AstFile &file, const TemplateArgs &bindings) const
{
  if (!m_ok) {
    return nullptr;
  }
  for (const Placeholder &p : m_placeholders) {
    if (!bindings.has(p.name)) {
      return nullptr;
    }
    if (p.splice) {
      for (Node *n : bindings.list(p.name)) {
        if (!acceptsSpliced(p.slot, n)) {
          return nullptr;
        }
      }
    } else if (!accepts(p.slot, bindings.get(p.name))) {
      return nullptr;
    }
  }

  Vector<Node *, 8> idClones;
  Vector<Node *, 4> escapes;
  idClones.resize(m_placeholders.size());
  struct Cloner {
    const Template &t;
    AstFile &file;
    Vector<Node *, 8> &idClones;
    Vector<Node *, 4> &escapes;

    Node *clone(const Node *n)
    {
      Node *c = file.make(n->kind, n->grammar);
      c->flags = n->flags;
      c->data = n->data;
      c->text = n->text;
      c->start = n->start;
      c->end = n->end;
      if (n->kind == NodeKind::Identifier) {
        int index = t.placeholderIndex(n);
        if (index >= 0) {
          idClones[index] = c;
        } else if (isEscapeText(n->text)) {
          c->text = n->text.substr(1);
          escapes.append(c);
        }
      }
      for (const Node *child : n->children) {
        c->appendChild(child ? clone(child) : nullptr);
      }
      return c;
    }
  };
  Cloner cloner{*this, file, idClones, escapes};
  Node *root = cloner.clone(m_proto);

  for (Node *e : escapes) {
    e->grammar = {};
    e->dirty = true;
    dirtyChain(file, e->parent);
  }

  Fixer fixer(file);
  Vector<string_view, 8> used;
  auto firstUse = [&](string_view name) {
    for (string_view u : used) {
      if (u == name) {
        return false;
      }
    }
    used.append(name);
    return true;
  };
  auto take = [&](Node *arg, bool first) {
    if (!first) {
      return cloneNode(file, arg);
    }
    fixer.detach(arg);
    return arg;
  };

  for (size_t i = 0; i < m_placeholders.size(); i++) {
    const Placeholder &p = m_placeholders[int(i)];
    Node *id = idClones[int(i)];
    if (p.splice) {
      Node *target = p.slot == SlotKind::Statement ? id->parent : id;
      Node *parent = target->parent;
      int index = indexOf(parent, target);
      dirtyChain(file, parent);
      parent->children.remove_at(index);
      target->parent = nullptr;
      bool first = firstUse(p.name);
      int at = index;
      for (Node *n : bindings.list(p.name)) {
        Node *arg = take(n, first);
        insertAt(parent->children, at, arg);
        arg->parent = parent;
        if (needsParens(parent, at, arg)) {
          arg->setFlag(Flag::Parenthesized);
        }
        at++;
      }
      continue;
    }
    Node *arg = take(bindings.get(p.name), firstUse(p.name));
    Node *target = id;
    if (p.slot == SlotKind::Statement && arg->isStatement()) {
      target = id->parent;
    } else if (p.slot == SlotKind::Type && arg->isType()) {
      target = id->parent;
    }
    Node *parent = target->parent;
    if (!parent || target == root) {
      target->parent = nullptr;
      root = arg;
      continue;
    }
    int index = indexOf(parent, target);
    dirtyChain(file, parent);
    parent->children[index] = arg;
    arg->parent = parent;
    target->parent = nullptr;
    if (needsParens(parent, index, arg)) {
      arg->setFlag(Flag::Parenthesized);
    }
  }
  return root;
}

Node *Template::instantiate(AstFile &file, const TemplateArgs &bindings) const
{
  if (m_mode == Mode::Statements) {
    return nullptr;
  }
  return build(file, bindings);
}

bool Template::instantiateAll(AstFile &file,
                              const TemplateArgs &bindings,
                              Vector<Node *, 4> &out) const
{
  if (m_mode != Mode::Statements) {
    return false;
  }
  Node *program = build(file, bindings);
  if (!program) {
    return false;
  }
  for (Node *s : program->children) {
    s->parent = nullptr;
    out.append(s);
  }
  program->children.clear();
  return true;
}

// ---------------------------------------------------------------- match

namespace {

/** Binds `name` to `node`, or checks the existing binding against it. */
bool bindOne(TemplateArgs &bindings, string_view name, Node *node)
{
  if (bindings.has(name)) {
    return equivalent(bindings.get(name), node);
  }
  bindings.set(name, node);
  return true;
}

bool bindMany(TemplateArgs &bindings, string_view name, span<Node *const> nodes)
{
  if (bindings.has(name)) {
    span<Node *const> old = bindings.list(name);
    if (old.size() != nodes.size()) {
      return false;
    }
    for (size_t i = 0; i < nodes.size(); i++) {
      if (!equivalent(old[i], nodes[i])) {
        return false;
      }
    }
    return true;
  }
  bindings.set(name, nodes);
  return true;
}

} // namespace

bool Template::match(Node *node, TemplateArgs &bindings) const
{
  if (!m_ok || !node) {
    return false;
  }
  if (m_mode == Mode::Statements) {
    return false;
  }
  return matchNode(m_proto, node, bindings);
}

bool Template::matchNode(const Node *proto, Node *node, TemplateArgs &bindings) const
{
  if (!proto || !node) {
    return proto == nullptr && node == nullptr;
  }
  if (proto->kind == NodeKind::Identifier) {
    if (const Placeholder *p = placeholderFor(proto)) {
      return !p->splice && accepts(p->slot, node) && bindOne(bindings, p->name, node);
    }
  }
  // The wrappers a placeholder stands in for bind the whole node.
  if (proto->kind == NodeKind::ExpressionStatement ||
      proto->kind == NodeKind::TSTypeReference)
  {
    const Node *inner = proto->children[0];
    const Placeholder *p = inner ? placeholderFor(inner) : nullptr;
    if (p && !p->splice) {
      if (p->slot == SlotKind::Statement && node->isStatement()) {
        return bindOne(bindings, p->name, node);
      }
      if (p->slot == SlotKind::Type && node->isType()) {
        return bindOne(bindings, p->name, node);
      }
    }
  }
  if (proto->kind != node->kind ||
      (proto->flags & ~kIgnoredFlags) != (node->flags & ~kIgnoredFlags) ||
      proto->data != node->data)
  {
    return false;
  }
  if (kindInfo(proto->kind).usesText) {
    string_view want = isEscapeText(proto->text) ? proto->text.substr(1) : proto->text;
    if (want != node->text) {
      return false;
    }
  }
  const KindInfo &info = kindInfo(proto->kind);
  int fixed = info.fixedChildren;
  for (int i = 0; i < fixed; i++) {
    const Node *pc = size_t(i) < proto->children.size() ? proto->children[i] : nullptr;
    Node *nc = size_t(i) < node->children.size() ? node->children[i] : nullptr;
    if (!matchNode(pc, nc, bindings)) {
      return false;
    }
  }
  if (!info.hasList) {
    return true;
  }
  return matchList(proto, node, fixed, bindings);
}

bool Template::matchList(const Node *proto,
                         Node *node,
                         int from,
                         TemplateArgs &bindings) const
{
  int protoCount = int(proto->children.size()) - from;
  int nodeCount = int(node->children.size()) - from;
  // A splice element is the placeholder itself or the statement wrapping it.
  int spliceAt = -1;
  const Placeholder *splice = nullptr;
  for (int i = 0; i < protoCount; i++) {
    const Node *e = proto->children[from + i];
    if (e && e->kind == NodeKind::ExpressionStatement) {
      e = e->children[0];
    }
    const Placeholder *p = e ? placeholderFor(e) : nullptr;
    if (p && p->splice) {
      spliceAt = i;
      splice = p;
      break;
    }
  }
  if (spliceAt < 0) {
    if (protoCount != nodeCount) {
      return false;
    }
    for (int i = 0; i < protoCount; i++) {
      if (!matchNode(proto->children[from + i], node->children[from + i], bindings)) {
        return false;
      }
    }
    return true;
  }
  int suffix = protoCount - spliceAt - 1;
  if (nodeCount < spliceAt + suffix) {
    return false;
  }
  for (int i = 0; i < spliceAt; i++) {
    if (!matchNode(proto->children[from + i], node->children[from + i], bindings)) {
      return false;
    }
  }
  for (int i = 0; i < suffix; i++) {
    const Node *pc = proto->children[from + protoCount - 1 - i];
    Node *nc = node->children[from + nodeCount - 1 - i];
    if (!matchNode(pc, nc, bindings)) {
      return false;
    }
  }
  int middle = nodeCount - spliceAt - suffix;
  for (int i = 0; i < middle; i++) {
    if (!acceptsSpliced(splice->slot, node->children[from + spliceAt + i])) {
      return false;
    }
  }
  span<Node *const> bound(node->children.data() + from + spliceAt, size_t(middle));
  return bindMany(bindings, splice->name, bound);
}

} // namespace fastlint::ast
