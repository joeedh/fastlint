#include "fastlint/ast/binder.h"

#include "fastlint/ast/generated/views.h"

namespace fastlint::ast {

// ------------------------------------------------------------------- Scope

Scope *Scope::variableScope()
{
  Scope *s = this;
  while (!s->isVariableScope() && s->parent) {
    s = s->parent;
  }
  return s;
}

Declaration *Scope::lookupLocal(string_view name, Space space) const
{
  Declaration *const *slot =
      const_cast<Map<Name, Declaration *> &>(byName).lookup_ptr(Name{name});
  for (Declaration *d = slot ? *slot : nullptr; d; d = d->nextSameName) {
    if (d->inSpace(space)) {
      return d;
    }
  }
  return nullptr;
}

Declaration *Scope::lookup(string_view name, Space space)
{
  for (Scope *s = this; s; s = s->parent) {
    if (Declaration *d = s->lookupLocal(name, space)) {
      return d;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------- Bindings

Scope *Bindings::scopeOf(const Node *node) const
{
  Scope *const *slot =
      const_cast<Map<const Node *, Scope *> &>(m_scopeByNode).lookup_ptr(node);
  return slot ? *slot : nullptr;
}

Declaration *Bindings::declarationOf(const Node *id) const
{
  Declaration *const *slot =
      const_cast<Map<const Node *, Declaration *> &>(m_declByNode).lookup_ptr(id);
  return slot ? *slot : nullptr;
}

Reference *Bindings::referenceOf(const Node *id) const
{
  Reference *const *slot =
      const_cast<Map<const Node *, Reference *> &>(m_refByNode).lookup_ptr(id);
  return slot ? *slot : nullptr;
}

void Bindings::clear()
{
  m_scopes.clear();
  m_declarations.clear();
  m_references.clear();
  m_module = nullptr;
  m_scopeByNode.clear();
  m_declByNode.clear();
  m_refByNode.clear();
  m_unresolved.clear();
}

// ------------------------------------------------------------------ Binder

class Binder {
public:
  Binder(AstFile &file, Bindings &bindings) : f(file), b(bindings)
  {
  }

  void run()
  {
    Node *root = f.root();
    if (!root) {
      return;
    }
    cur = push(ScopeKind::Module, root);
    b.m_module = cur;
    for (Node *s : root->children) {
      visit(s);
    }
    pop();
    resolve();
  }

private:
  AstFile &f;
  Bindings &b;
  Scope *cur = nullptr;
  /** Every reference in creation order, which is source order. */
  Vector<Reference *> refs;

  // --------------------------------------------------------------- records

  Scope *push(ScopeKind kind, Node *node)
  {
    Scope *s = b.m_scopes.alloc();
    s->kind = kind;
    s->node = node;
    s->parent = cur;
    if (cur) {
      cur->children.append(s);
    }
    b.m_scopeByNode.add_overwrite(node, s);
    cur = s;
    return s;
  }

  void pop()
  {
    cur = cur->parent;
  }

  Declaration *declare(string_view name,
                       Node *id,
                       Node *node,
                       DeclKind kind,
                       Space spaces,
                       Scope *scope = nullptr)
  {
    if (!scope) {
      scope = cur;
    }
    Declaration *d = b.m_declarations.alloc();
    d->name = name;
    d->id = id;
    d->node = node;
    d->kind = kind;
    d->spaces = uint8_t(spaces);
    d->scope = scope;
    scope->declarations.append(d);
    b.m_declByNode.add_overwrite(id, d);
    Declaration **slot = scope->byName.lookup_ptr(Name{name});
    if (!slot) {
      scope->byName.add(Name{name}, d);
    } else {
      Declaration *last = *slot;
      while (last->nextSameName) {
        last = last->nextSameName;
      }
      last->nextSameName = d;
    }
    return d;
  }

  Reference *reference(Node *id, Space space, uint8_t flags)
  {
    Reference *r = b.m_references.alloc();
    r->id = id;
    r->scope = cur;
    r->space = space;
    r->flags = flags;
    cur->references.append(r);
    b.m_refByNode.add_overwrite(id, r);
    refs.append(r);
    return r;
  }

  void resolve()
  {
    for (Reference *r : refs) {
      Declaration *d = r->scope->lookup(r->name(), r->space);
      if (d) {
        r->resolved = d;
        d->references.append(r);
      } else {
        b.m_unresolved.append(r);
      }
    }
  }

  // --------------------------------------------------------------- helpers

  void visitAll(span<Node *> nodes)
  {
    for (Node *n : nodes) {
      visit(n);
    }
  }

  /** Visits every child; the default for kinds whose identifiers are all references. */
  void visitChildren(Node *n)
  {
    for (Node *c : n->children) {
      visit(c);
    }
  }

  /** A name in reference position: `x`, or the leftmost part of `A.B.C`. */
  void entity(Node *n, Space space)
  {
    if (!n) {
      return;
    }
    if (n->kind == NodeKind::Identifier) {
      reference(n, space, Reference::Read);
    } else if (n->kind == NodeKind::TSQualifiedName) {
      entity(n->children[0], Space::Either);
    } else {
      visit(n);
    }
  }

  static DeclKind declKindOf(VariableKind kind)
  {
    switch (kind) {
    case VariableKind::Var:
      return DeclKind::Var;
    case VariableKind::Let:
      return DeclKind::Let;
    case VariableKind::Const:
      return DeclKind::Const;
    default:
      return DeclKind::Using;
    }
  }

  /** Declares every identifier in a binding pattern into `scope`. */
  void bindPattern(Node *target, DeclKind kind, Scope *scope, bool init, Node *declNode)
  {
    if (!target) {
      return;
    }
    switch (target->kind) {
    case NodeKind::Identifier: {
      declare(target->text, target, declNode, kind, Space::Value, scope);
      if (init) {
        reference(target, Space::Value, Reference::Write | Reference::Init);
      }
      visit(target->children[0]);
      break;
    }
    case NodeKind::ObjectPattern: {
      ObjectPattern p(target);
      visit(p.typeAnnotation());
      for (Node *prop : p.properties()) {
        if (prop->kind == NodeKind::Property) {
          Property pr(prop);
          if (pr.isComputed()) {
            visit(pr.key());
          }
          bindPattern(pr.value(), kind, scope, init, declNode);
        } else {
          bindPattern(prop, kind, scope, init, declNode);
        }
      }
      break;
    }
    case NodeKind::ArrayPattern: {
      ArrayPattern p(target);
      visit(p.typeAnnotation());
      for (Node *e : p.elements()) {
        bindPattern(e, kind, scope, init, declNode);
      }
      break;
    }
    case NodeKind::RestElement: {
      RestElement r(target);
      bindPattern(r.argument(), kind, scope, init, declNode);
      visit(r.typeAnnotation());
      break;
    }
    case NodeKind::AssignmentPattern: {
      AssignmentPattern a(target);
      bindPattern(a.left(), kind, scope, true, declNode);
      visit(a.right());
      break;
    }
    case NodeKind::TSParameterProperty: {
      TSParameterProperty p(target);
      visit(p.decorators());
      bindPattern(p.parameter(), kind, scope, init, declNode);
      break;
    }
    default:
      visit(target);
      break;
    }
  }

  /** The left side of an assignment: every identifier is written. */
  void assignTarget(Node *target, uint8_t flags)
  {
    if (!target) {
      return;
    }
    switch (target->kind) {
    case NodeKind::Identifier:
      reference(target, Space::Value, flags);
      break;
    case NodeKind::ObjectPattern:
      for (Node *prop : ObjectPattern(target).properties()) {
        if (prop->kind == NodeKind::Property) {
          Property pr(prop);
          if (pr.isComputed()) {
            visit(pr.key());
          }
          assignTarget(pr.value(), flags);
        } else {
          assignTarget(prop, flags);
        }
      }
      break;
    case NodeKind::ArrayPattern:
      for (Node *e : ArrayPattern(target).elements()) {
        assignTarget(e, flags);
      }
      break;
    case NodeKind::RestElement:
      assignTarget(RestElement(target).argument(), flags);
      break;
    case NodeKind::AssignmentPattern: {
      AssignmentPattern a(target);
      assignTarget(a.left(), flags);
      visit(a.right());
      break;
    }
    default:
      visit(target);
      break;
    }
  }

  void params(span<Node *> list)
  {
    for (Node *p : list) {
      bindPattern(p, DeclKind::Parameter, cur, false, p);
    }
  }

  void function(Node *n)
  {
    FunctionLike fn(n);
    Node *id = fn.id();
    bool declaration = n->kind == NodeKind::FunctionDeclaration ||
                       n->kind == NodeKind::TSDeclareFunction;
    if (id && declaration) {
      declare(id->text, id, n, DeclKind::Function, Space::Value);
    }
    push(ScopeKind::Function, n);
    if (id && !declaration) {
      declare(id->text, id, n, DeclKind::Function, Space::Value);
    }
    visit(fn.typeParameters());
    params(fn.params());
    visit(fn.returnType());
    Node *body = fn.body();
    if (body && body->kind == NodeKind::BlockStatement) {
      visitChildren(body);
    } else {
      visit(body);
    }
    pop();
  }

  void classLike(Node *n)
  {
    ClassLike cls(n);
    visit(cls.decorators());
    Node *id = cls.id();
    bool declaration = n->kind == NodeKind::ClassDeclaration;
    if (id && declaration) {
      declare(id->text, id, n, DeclKind::Class, Space::Either);
    }
    push(ScopeKind::Class, n);
    if (id && !declaration) {
      declare(id->text, id, n, DeclKind::Class, Space::Either);
    }
    visit(cls.typeParameters());
    visit(cls.superClass());
    visit(cls.superTypeArguments());
    for (Node *impl : cls.implements()) {
      heritage(impl);
    }
    if (Node *body = cls.body()) {
      for (Node *member : ClassBody(body).body()) {
        classMember(member);
      }
    }
    pop();
  }

  void heritage(Node *n)
  {
    if (n->kind == NodeKind::TSClassImplements ||
        n->kind == NodeKind::TSInterfaceHeritage)
    {
      entity(n->children[0], Space::Type);
      visit(n->children[1]);
    } else {
      visit(n);
    }
  }

  void classMember(Node *n)
  {
    if (ClassMember m = n->as<ClassMember>()) {
      visit(m.decorators());
      if (m.isComputed()) {
        visit(m.key());
      }
      if (n->kind == NodeKind::PropertyDefinition ||
          n->kind == NodeKind::TSAbstractPropertyDefinition ||
          n->kind == NodeKind::AccessorProperty ||
          n->kind == NodeKind::TSAbstractAccessorProperty)
      {
        visit(n->children[2]);
      }
      visit(m.value());
      return;
    }
    if (n->kind == NodeKind::StaticBlock) {
      push(ScopeKind::StaticBlock, n);
      visitChildren(n);
      pop();
      return;
    }
    visit(n);
  }

  void signature(Node *n)
  {
    SignatureLike sig(n);
    if (n->kind == NodeKind::TSMethodSignature) {
      TSMethodSignature m(n);
      if (m.isComputed()) {
        visit(m.key());
      }
    }
    push(ScopeKind::Type, n);
    visit(sig.typeParameters());
    params(sig.params());
    visit(sig.returnType());
    pop();
  }

  /** The body of a loop, which may open a For scope for a let/const head. */
  bool loopHeadDeclares(Node *head) const
  {
    return head && head->kind == NodeKind::VariableDeclaration &&
           VariableDeclaration(head).kind() != VariableKind::Var;
  }

  void variableDeclaration(Node *n, bool loopHead)
  {
    VariableDeclaration decl(n);
    DeclKind kind = declKindOf(decl.kind());
    Scope *scope = kind == DeclKind::Var ? cur->variableScope() : cur;
    for (Node *d : decl.declarations()) {
      VariableDeclarator v(d);
      bindPattern(v.id(), kind, scope, loopHead || v.init() != nullptr, d);
      visit(v.init());
    }
  }

  void moduleDeclaration(Node *n)
  {
    TSModuleDeclaration m(n);
    Node *id = m.id();
    if (m.kind() == ModuleKind::Global) {
      visit(m.body());
      return;
    }
    if (id && id->kind != NodeKind::Literal) {
      Node *name = id;
      while (name->kind == NodeKind::TSQualifiedName) {
        name = name->children[0];
      }
      declare(name->text, name, n, DeclKind::Namespace, Space::Either);
    }
    push(ScopeKind::Namespace, n);
    visit(m.body());
    pop();
  }

  void importDeclaration(Node *n)
  {
    ImportDeclaration decl(n);
    bool typeOnly = decl.importKind() == ImportKind::Type;
    for (Node *spec : decl.specifiers()) {
      Node *local = nullptr;
      Space spaces = typeOnly ? Space::Type : Space::Either;
      switch (spec->kind) {
      case NodeKind::ImportSpecifier: {
        ImportSpecifier s(spec);
        local = s.local();
        if (s.importKind() == ImportKind::Type) {
          spaces = Space::Type;
        }
        break;
      }
      case NodeKind::ImportDefaultSpecifier:
        local = ImportDefaultSpecifier(spec).local();
        break;
      case NodeKind::ImportNamespaceSpecifier:
        local = ImportNamespaceSpecifier(spec).local();
        break;
      default:
        break;
      }
      if (local && local->kind == NodeKind::Identifier) {
        declare(local->text, local, spec, DeclKind::Import, spaces);
      }
    }
  }

  void exportNamed(Node *n)
  {
    ExportNamedDeclaration decl(n);
    visit(decl.declaration());
    if (decl.source()) {
      return;
    }
    bool typeOnly = decl.exportKind() == ImportKind::Type;
    for (Node *spec : decl.specifiers()) {
      ExportSpecifier s(spec);
      Node *local = s.local();
      if (local && local->kind == NodeKind::Identifier) {
        bool type = typeOnly || s.exportKind() == ImportKind::Type;
        reference(local, type ? Space::Type : Space::Either, Reference::Read);
      }
    }
  }

  // ------------------------------------------------------------------ visit

  void visit(Node *n)
  {
    if (!n) {
      return;
    }
    switch (n->kind) {
    // Names in reference position.
    case NodeKind::Identifier:
      reference(n, Space::Value, Reference::Read);
      break;

    // Kinds whose identifiers are not references.
    case NodeKind::PrivateIdentifier:
    case NodeKind::Literal:
    case NodeKind::TemplateElement:
    case NodeKind::ThisExpression:
    case NodeKind::Super:
    case NodeKind::MetaProperty:
    case NodeKind::BreakStatement:
    case NodeKind::ContinueStatement:
    case NodeKind::ExportAllDeclaration:
    case NodeKind::TSNamespaceExportDeclaration:
    case NodeKind::TSQualifiedName:
    case NodeKind::TSImportType:
    case NodeKind::Error:
      break;
    case NodeKind::ImportExpression:
      visitChildren(n);
      break;

    case NodeKind::MemberExpression: {
      MemberExpression m(n);
      visit(m.object());
      if (m.isComputed()) {
        visit(m.property());
      }
      break;
    }
    case NodeKind::Property: {
      Property p(n);
      if (p.isComputed()) {
        visit(p.key());
      }
      visit(p.value());
      break;
    }
    case NodeKind::AssignmentExpression: {
      AssignmentExpression a(n);
      uint8_t flags = a.op() == AssignmentOperator::Assign
                          ? Reference::Write
                          : Reference::Read | Reference::Write;
      assignTarget(a.left(), flags);
      visit(a.right());
      break;
    }
    case NodeKind::UpdateExpression:
      assignTarget(UpdateExpression(n).argument(), Reference::Read | Reference::Write);
      break;
    case NodeKind::ObjectPattern:
    case NodeKind::ArrayPattern:
    case NodeKind::RestElement:
    case NodeKind::AssignmentPattern:
      assignTarget(n, Reference::Write);
      break;

    case NodeKind::FunctionDeclaration:
    case NodeKind::FunctionExpression:
    case NodeKind::ArrowFunctionExpression:
    case NodeKind::TSDeclareFunction:
    case NodeKind::TSEmptyBodyFunctionExpression:
      function(n);
      break;
    case NodeKind::ClassDeclaration:
    case NodeKind::ClassExpression:
      classLike(n);
      break;
    case NodeKind::StaticBlock:
      push(ScopeKind::StaticBlock, n);
      visitChildren(n);
      pop();
      break;

    case NodeKind::VariableDeclaration:
      variableDeclaration(n, false);
      break;
    case NodeKind::BlockStatement:
      push(ScopeKind::Block, n);
      visitChildren(n);
      pop();
      break;
    case NodeKind::ForStatement: {
      ForStatement s(n);
      bool scoped = loopHeadDeclares(s.init());
      if (scoped) {
        push(ScopeKind::For, n);
      }
      visit(s.init());
      visit(s.test());
      visit(s.update());
      visit(s.body());
      if (scoped) {
        pop();
      }
      break;
    }
    case NodeKind::ForInStatement:
    case NodeKind::ForOfStatement: {
      Loop loop(n);
      Node *left = n->children[0];
      Node *right = n->children[1];
      bool scoped = loopHeadDeclares(left);
      if (scoped) {
        push(ScopeKind::For, n);
      }
      if (left && left->kind == NodeKind::VariableDeclaration) {
        variableDeclaration(left, true);
      } else {
        assignTarget(left, Reference::Write);
      }
      visit(right);
      visit(loop.body());
      if (scoped) {
        pop();
      }
      break;
    }
    case NodeKind::SwitchStatement: {
      SwitchStatement s(n);
      visit(s.discriminant());
      push(ScopeKind::Switch, n);
      for (Node *c : s.cases()) {
        visitChildren(c);
      }
      pop();
      break;
    }
    case NodeKind::CatchClause: {
      CatchClause c(n);
      push(ScopeKind::Catch, n);
      bindPattern(c.param(), DeclKind::CatchParam, cur, false, n);
      visit(c.body());
      pop();
      break;
    }
    case NodeKind::LabeledStatement:
      visit(LabeledStatement(n).body());
      break;

    case NodeKind::ImportDeclaration:
      importDeclaration(n);
      break;
    case NodeKind::ExportNamedDeclaration:
      exportNamed(n);
      break;
    case NodeKind::TSImportEqualsDeclaration: {
      TSImportEqualsDeclaration d(n);
      Node *id = d.id();
      if (id) {
        declare(id->text, id, n, DeclKind::Import, Space::Either);
      }
      Node *ref = d.moduleReference();
      if (ref && ref->kind != NodeKind::TSExternalModuleReference) {
        entity(ref, Space::Either);
      }
      break;
    }

    case NodeKind::TSModuleDeclaration:
      moduleDeclaration(n);
      break;
    case NodeKind::TSEnumDeclaration: {
      TSEnumDeclaration e(n);
      Node *id = e.id();
      if (id) {
        declare(id->text, id, n, DeclKind::Enum, Space::Either);
      }
      push(ScopeKind::Enum, n);
      for (Node *member : e.members()) {
        TSEnumMember m(member);
        Node *mid = m.id();
        if (mid && mid->kind == NodeKind::Identifier && !m.isComputed()) {
          declare(mid->text, mid, member, DeclKind::EnumMember, Space::Value);
        } else if (m.isComputed()) {
          visit(mid);
        }
        visit(m.initializer());
      }
      pop();
      break;
    }
    case NodeKind::TSInterfaceDeclaration: {
      TSInterfaceDeclaration i(n);
      Node *id = i.id();
      if (id) {
        declare(id->text, id, n, DeclKind::Interface, Space::Type);
      }
      push(ScopeKind::Type, n);
      visit(i.typeParameters());
      for (Node *h : i.extends()) {
        heritage(h);
      }
      visit(i.body());
      pop();
      break;
    }
    case NodeKind::TSTypeAliasDeclaration: {
      TSTypeAliasDeclaration a(n);
      Node *id = a.id();
      if (id) {
        declare(id->text, id, n, DeclKind::TypeAlias, Space::Type);
      }
      push(ScopeKind::Type, n);
      visit(a.typeParameters());
      visit(a.typeAnnotation());
      pop();
      break;
    }
    case NodeKind::TSTypeParameter:
      declare(n->text, n, n, DeclKind::TypeParameter, Space::Type);
      visitChildren(n);
      break;

    case NodeKind::TSTypeReference: {
      TSTypeReference r(n);
      entity(r.typeName(), Space::Type);
      visit(r.typeArguments());
      break;
    }
    case NodeKind::TSTypeQuery: {
      TSTypeQuery q(n);
      entity(q.exprName(), Space::Value);
      visit(q.typeArguments());
      break;
    }
    case NodeKind::TSTypePredicate: {
      TSTypePredicate p(n);
      Node *name = p.parameterName();
      if (name && name->kind == NodeKind::Identifier) {
        reference(name, Space::Value, Reference::Read);
      }
      visit(p.typeAnnotation());
      break;
    }
    case NodeKind::TSInterfaceHeritage:
    case NodeKind::TSClassImplements:
      heritage(n);
      break;
    case NodeKind::TSPropertySignature: {
      TSPropertySignature p(n);
      if (p.isComputed()) {
        visit(p.key());
      }
      visit(p.typeAnnotation());
      break;
    }
    case NodeKind::TSMethodSignature:
    case NodeKind::TSCallSignatureDeclaration:
    case NodeKind::TSConstructSignatureDeclaration:
    case NodeKind::TSFunctionType:
    case NodeKind::TSConstructorType:
      signature(n);
      break;
    case NodeKind::TSIndexSignature: {
      TSIndexSignature s(n);
      for (Node *p : s.parameters()) {
        if (p && p->kind == NodeKind::Identifier) {
          visit(p->children[0]);
        } else {
          visit(p);
        }
      }
      visit(s.typeAnnotation());
      break;
    }
    case NodeKind::TSMappedType:
      push(ScopeKind::Type, n);
      visitChildren(n);
      pop();
      break;
    case NodeKind::TSConditionalType: {
      TSConditionalType c(n);
      visit(c.checkType());
      push(ScopeKind::Type, n);
      visit(c.extendsType());
      visit(c.trueType());
      pop();
      visit(c.falseType());
      break;
    }
    case NodeKind::TSNamedTupleMember:
      visit(TSNamedTupleMember(n).elementType());
      break;
    case NodeKind::TSEnumMember:
      break;

    case NodeKind::JSXIdentifier:
    case NodeKind::JSXNamespacedName:
    case NodeKind::JSXText:
    case NodeKind::JSXOpeningFragment:
    case NodeKind::JSXClosingFragment:
    case NodeKind::JSXEmptyExpression:
      break;
    case NodeKind::JSXAttribute:
      visit(JSXAttribute(n).value());
      break;
    case NodeKind::JSXClosingElement:
      break;
    case NodeKind::JSXOpeningElement: {
      JSXOpeningElement o(n);
      Node *name = o.name();
      if (name && name->kind == NodeKind::JSXIdentifier && !name->text.empty() &&
          name->text.front() >= 'A' && name->text.front() <= 'Z')
      {
        reference(name, Space::Value, Reference::Read);
      } else if (name && name->kind == NodeKind::JSXMemberExpression) {
        Node *object = name;
        while (object->kind == NodeKind::JSXMemberExpression) {
          object = object->children[0];
        }
        if (object->kind == NodeKind::JSXIdentifier) {
          reference(object, Space::Value, Reference::Read);
        }
      }
      visit(o.typeArguments());
      visitAll(o.attributes());
      break;
    }

    default:
      visitChildren(n);
      break;
    }
  }
};

void bind(AstFile &file, Bindings &out)
{
  out.clear();
  Binder binder(file, out);
  binder.run();
}

// -------------------------------------------------------------------- dump

namespace {

const char *scopeKindName(ScopeKind kind)
{
  switch (kind) {
  case ScopeKind::Module:
    return "module";
  case ScopeKind::Function:
    return "function";
  case ScopeKind::Class:
    return "class";
  case ScopeKind::Block:
    return "block";
  case ScopeKind::Switch:
    return "switch";
  case ScopeKind::For:
    return "for";
  case ScopeKind::Catch:
    return "catch";
  case ScopeKind::StaticBlock:
    return "static";
  case ScopeKind::Namespace:
    return "namespace";
  case ScopeKind::Enum:
    return "enum";
  case ScopeKind::Type:
    return "type";
  }
  return "?";
}

const char *declKindName(DeclKind kind)
{
  switch (kind) {
  case DeclKind::Var:
    return "var";
  case DeclKind::Let:
    return "let";
  case DeclKind::Const:
    return "const";
  case DeclKind::Using:
    return "using";
  case DeclKind::Function:
    return "function";
  case DeclKind::Class:
    return "class";
  case DeclKind::Parameter:
    return "parameter";
  case DeclKind::CatchParam:
    return "catch-param";
  case DeclKind::Import:
    return "import";
  case DeclKind::Interface:
    return "interface";
  case DeclKind::TypeAlias:
    return "type-alias";
  case DeclKind::Enum:
    return "enum";
  case DeclKind::EnumMember:
    return "enum-member";
  case DeclKind::Namespace:
    return "namespace";
  case DeclKind::TypeParameter:
    return "type-parameter";
  }
  return "?";
}

const char *spaceName(uint8_t spaces)
{
  switch (spaces) {
  case uint8_t(Space::Value):
    return "value";
  case uint8_t(Space::Type):
    return "type";
  default:
    return "value+type";
  }
}

struct BindingsDumper {
  string &out;

  void put(string_view text)
  {
    for (char c : text) {
      out += c;
    }
  }
  void number(uint32_t value)
  {
    char buffer[12];
    int n = 0;
    do {
      buffer[n++] = char('0' + value % 10);
      value /= 10;
    } while (value);
    while (n) {
      out += buffer[--n];
    }
  }
  void indent(int depth)
  {
    for (int i = 0; i < depth * 2; i++) {
      out += ' ';
    }
  }
  void at(const Node *n)
  {
    out += '@';
    number(n->start);
  }

  void scope(const Scope *s, int depth)
  {
    indent(depth);
    put(scopeKindName(s->kind));
    out += ' ';
    out += '@';
    number(s->node->start);
    out += '-';
    number(s->node->end);
    out += '\n';
    for (const Declaration *d : s->declarations) {
      indent(depth + 1);
      put("decl ");
      put(d->name);
      at(d->id);
      out += ' ';
      put(declKindName(d->kind));
      out += ' ';
      put(spaceName(d->spaces));
      put(" refs=");
      number(uint32_t(d->references.size()));
      out += '\n';
    }
    for (const Reference *r : s->references) {
      indent(depth + 1);
      put("ref ");
      put(r->name());
      at(r->id);
      out += ' ';
      if (r->isRead()) {
        put("read");
      }
      if (r->isWrite()) {
        put(r->isRead() ? "+write" : "write");
      }
      if (r->isInit()) {
        put("+init");
      }
      if (r->space == Space::Type) {
        put(" type");
      } else if (r->space == Space::Either) {
        put(" value+type");
      }
      put(" -> ");
      if (r->resolved) {
        put(scopeKindName(r->resolved->scope->kind));
        out += ' ';
        put(r->resolved->name);
        at(r->resolved->id);
      } else {
        put("unresolved");
      }
      out += '\n';
    }
    for (const Scope *c : s->children) {
      scope(c, depth + 1);
    }
  }
};

} // namespace

void dumpBindings(const Bindings &bindings, string &out)
{
  if (const Scope *root = bindings.moduleScope()) {
    BindingsDumper dumper{out};
    dumper.scope(root, 0);
  }
}

} // namespace fastlint::ast
