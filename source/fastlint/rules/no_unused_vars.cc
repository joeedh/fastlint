// no-unused-vars: report names that are declared but never read
// (docs/rules/no-unused-vars.md). A port of typescript-eslint's rule over
// the binder: a variable is one declaration chain of a scope, its uses are
// the reads that are not the variable feeding itself.

#include "fastlint/lint/regex.h"
#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"
#include "util/map.h"
#include "util/pool.h"
#include "util/set.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Declaration;
using ast::DeclKind;
using ast::Node;
using ast::NodeKind;
using ast::Reference;
using ast::Scope;
using litestl::util::Map;
using litestl::util::Pool;
using litestl::util::Set;

constexpr Message kMessages[] = {
    {"unusedVar", "'{{varName}}' is {{action}} but never used{{additional}}."},
    {"usedIgnoredVar", "'{{varName}}' is marked as ignored but is used{{additional}}."},
    {"usedOnlyAsType",
     "'{{varName}}' is {{action}} but only used as a type{{additional}}."},
    {"removeUnusedVar", "Remove unused variable \"{{varName}}\"."},
    {"removeUnusedImportDeclaration", "Remove unused import declaration."},
};

enum class Args : uint8_t { All, AfterUsed, None };

/** One ignore pattern with the `/pattern/u` text the messages quote. */
struct Pattern {
  Regex regex;
  string text;
  bool set = false;

  void load(const JsonValue *opt, string_view key)
  {
    string_view source = opt->getString(key);
    if (source.empty()) {
      return;
    }
    set = regex.compile(source, "u");
    text = string("/");
    text += std::string(source);
    text += "/u";
  }
  bool matches(string_view name) const
  {
    return set && regex.search(name);
  }
};

struct Options {
  bool varsLocal = false;
  Args args = Args::AfterUsed;
  bool caughtErrors = true;
  bool ignoreRestSiblings = false;
  bool ignoreUsingDeclarations = false;
  bool ignoreClassWithStaticInitBlock = false;
  bool reportUsedIgnorePattern = false;
  bool autofixImports = false;
  Pattern vars;
  Pattern args_;
  Pattern caught;
  Pattern destructuredArray;
  bool script = false;
  bool definitionFile = false;
};

void readOptions(RuleContext &ctx, Options &o)
{
  const JsonValue *opt = ctx.option();
  if (opt && opt->isString()) {
    o.varsLocal = opt->asString() == "local";
  } else if (opt) {
    o.varsLocal = opt->getString("vars") == "local";
    string_view args = opt->getString("args");
    o.args = args == "all" ? Args::All : args == "none" ? Args::None : Args::AfterUsed;
    o.caughtErrors = opt->getString("caughtErrors") != "none";
    o.ignoreRestSiblings = opt->getBool("ignoreRestSiblings", false);
    o.ignoreUsingDeclarations = opt->getBool("ignoreUsingDeclarations", false);
    o.ignoreClassWithStaticInitBlock =
        opt->getBool("ignoreClassWithStaticInitBlock", false);
    o.reportUsedIgnorePattern = opt->getBool("reportUsedIgnorePattern", false);
    if (const JsonValue *autofix = opt->get("enableAutofixRemoval")) {
      o.autofixImports = autofix->getBool("imports", false);
    }
    o.vars.load(opt, "varsIgnorePattern");
    o.args_.load(opt, "argsIgnorePattern");
    o.caught.load(opt, "caughtErrorsIgnorePattern");
    o.destructuredArray.load(opt, "destructuredArrayIgnorePattern");
  }
  o.script = isScript(ctx);
  o.definitionFile = isDefinitionFile(ctx);
}

/**
 * One name of a scope with every declaration of it, as scope-manager merges
 * an interface with its class or a function with its overloads.
 */
struct Variable {
  Vector<Declaration *, 2> defs;
  /** Every reference of every definition, in source order. */
  Vector<Reference *, 4> refs;
  bool markedUsed = false;
  string_view name() const
  {
    return defs[0]->name;
  }
  Scope *scope() const
  {
    return defs[0]->scope;
  }
};

struct State {
  Options options;
  Pool<string, 8> texts;
  /** Declarations the ambient-declaration pass marked as used. */
  Set<const Declaration *> ambientUsed;

  string_view intern(const string &value)
  {
    string *text = texts.alloc();
    *text = value;
    return string_view(text->c_str(), text->size());
  }
};

// ------------------------------------------------------------- tree helpers

bool isInside(const Node *node, const Node *outer)
{
  return node->start >= outer->start && node->end <= outer->end;
}

/** Whether `ref` was made from `nodes`' scope or one nested in it. */
bool isSelfReference(const Reference *ref, span<Node *> nodes)
{
  for (Scope *s = ref->scope; s; s = s->parent) {
    for (Node *n : nodes) {
      if (s->node == n) {
        return true;
      }
    }
  }
  return false;
}

bool isInsideOneOf(const Reference *ref, span<Node *> nodes)
{
  for (Node *n : nodes) {
    if (isInside(ref->id, n)) {
      return true;
    }
  }
  return false;
}

/** Whether `id` sits in a `typeof x` type query. */
bool inTypeQuery(const Node *id)
{
  for (const Node *n = id; n; n = n->parent) {
    if (n->kind == NodeKind::TSTypeQuery) {
      return true;
    }
    if (n->kind != NodeKind::Identifier && n->kind != NodeKind::TSQualifiedName) {
      return false;
    }
  }
  return false;
}

/** Whether `id` names the parameter of an `x is T` predicate. */
bool inTypePredicate(const Node *id)
{
  return id->parent && id->parent->kind == NodeKind::TSTypePredicate;
}

/** A value whose result no one reads: a statement, or a non-final comma operand. */
bool isUnusedExpression(const Node *node)
{
  const Node *parent = node->parent;
  if (!parent) {
    return false;
  }
  if (parent->kind == NodeKind::ExpressionStatement) {
    return true;
  }
  if (parent->kind == NodeKind::SequenceExpression) {
    if (parent->children[int(parent->children.size()) - 1] != node) {
      return true;
    }
    return isUnusedExpression(parent);
  }
  return false;
}

bool isInLoop(const Node *node)
{
  for (const Node *n = node; n; n = n->parent) {
    if (ast::FunctionLike::matches(n->kind)) {
      return false;
    }
    if (ast::Loop::matches(n->kind)) {
      return true;
    }
  }
  return false;
}

/**
 * The right side of `x = ...` when the assignment's value is dropped and
 * `x` cannot be read later; reads of `x` inside it only feed `x` itself.
 */
Node *rhsNode(const Reference *ref, Node *prevRhs)
{
  Node *id = ref->id;
  Node *parent = id->parent;
  Scope *refScope = ref->scope->variableScope();
  Scope *varScope = ref->resolved->scope->variableScope();
  bool canBeUsedLater = refScope != varScope || isInLoop(id);
  if (prevRhs && isInside(id, prevRhs)) {
    return prevRhs;
  }
  if (parent && parent->kind == NodeKind::AssignmentExpression &&
      isUnusedExpression(parent) && ast::AssignmentExpression(parent).left() == id &&
      !canBeUsedLater)
  {
    return ast::AssignmentExpression(parent).right();
  }
  return nullptr;
}

/** Whether a function under `rhs` is kept somewhere, so its reads may run later. */
bool isStorableFunction(Node *fn, const Node *rhs)
{
  Node *node = fn;
  Node *parent = fn->parent;
  while (parent && isInside(parent, rhs)) {
    switch (parent->kind) {
    case NodeKind::SequenceExpression:
      if (parent->children.last() != node) {
        return false;
      }
      break;
    case NodeKind::CallExpression:
    case NodeKind::NewExpression:
      return parent->children[0] != node;
    case NodeKind::AssignmentExpression:
    case NodeKind::TaggedTemplateExpression:
    case NodeKind::YieldExpression:
      return true;
    default:
      if (parent->isStatement() || ast::NamedDeclaration::matches(parent->kind) ||
          parent->kind == NodeKind::VariableDeclaration)
      {
        return true;
      }
      break;
    }
    node = parent;
    parent = parent->parent;
  }
  return false;
}

bool isInsideOfStorableFunction(Node *id, const Node *rhs)
{
  Node *fn = id->enclosingFunction();
  return fn && isInside(fn, rhs) && isStorableFunction(fn, rhs);
}

bool isLogicalAssignment(const Node *assignment)
{
  ast::AssignmentOperator op =
      ast::AssignmentExpression(const_cast<Node *>(assignment)).op();
  return op == ast::AssignmentOperator::AndAssign ||
         op == ast::AssignmentOperator::OrAssign ||
         op == ast::AssignmentOperator::NullishAssign;
}

/** A read that only feeds the same variable: `a = a + 1`, `a++`, or inside its rhs. */
bool isReadForItself(const Reference *ref, const Node *rhs)
{
  Node *id = ref->id;
  Node *parent = id->parent;
  if (!ref->isRead()) {
    return false;
  }
  if (parent && parent->kind == NodeKind::AssignmentExpression &&
      !isLogicalAssignment(parent) && isUnusedExpression(parent) &&
      ast::AssignmentExpression(parent).left() == id)
  {
    return true;
  }
  if (parent && parent->kind == NodeKind::UpdateExpression && isUnusedExpression(parent))
  {
    return true;
  }
  return rhs && isInside(id, rhs) && !isInsideOfStorableFunction(id, rhs);
}

bool isTypeImport(const Declaration *decl)
{
  return decl->kind == DeclKind::Import && !decl->inSpace(ast::Space::Value);
}

/** Whether some reference reads the variable for a purpose other than feeding itself. */
bool isUsedVariable(const Variable &v)
{
  Vector<Node *, 2> functionNodes;
  Vector<Node *, 2> typeNodes;
  Vector<Node *, 2> moduleNodes;
  Vector<Node *, 2> enumNodes;
  bool importedAsType = true;
  for (Declaration *def : v.defs) {
    importedAsType = importedAsType && isTypeImport(def);
    switch (def->kind) {
    case DeclKind::Function:
    // References from inside a class resolve to the class's own copy of its
    // name in scope-manager, so they do not count as uses of the outer one.
    case DeclKind::Class:
      functionNodes.append(def->node);
      break;
    case DeclKind::Var:
    case DeclKind::Let:
    case DeclKind::Const:
    case DeclKind::Using: {
      Node *init = ast::VariableDeclarator(def->node).init();
      if (init && (init->kind == NodeKind::FunctionExpression ||
                   init->kind == NodeKind::ArrowFunctionExpression))
      {
        functionNodes.append(init);
      }
      break;
    }
    case DeclKind::Interface:
    case DeclKind::TypeAlias:
      typeNodes.append(def->node);
      break;
    case DeclKind::Namespace:
      moduleNodes.append(def->node);
      break;
    case DeclKind::Enum:
      enumNodes.append(def->node);
      break;
    default:
      break;
    }
  }
  Node *rhs = nullptr;
  for (Reference *ref : v.refs) {
    bool forItself = isReadForItself(ref, rhs);
    rhs = rhsNode(ref, rhs);
    if (!ref->isRead() || forItself) {
      continue;
    }
    if (!importedAsType && (inTypeQuery(ref->id) || inTypePredicate(ref->id))) {
      continue;
    }
    if (isSelfReference(ref, functionNodes) || isInsideOneOf(ref, typeNodes) ||
        isSelfReference(ref, moduleNodes) || isSelfReference(ref, enumNodes))
    {
      continue;
    }
    return true;
  }
  return false;
}

bool isExportParent(const Node *node)
{
  return node && (node->kind == NodeKind::ExportNamedDeclaration ||
                  node->kind == NodeKind::ExportDefaultDeclaration);
}

/** The construct exported for `def`: its declaration, or the declaration holding it. */
bool isExported(const Declaration *def)
{
  if (def->kind == DeclKind::Parameter || def->kind == DeclKind::CatchParam ||
      def->kind == DeclKind::TypeParameter)
  {
    return false;
  }
  Node *node = def->node;
  if (def->kind == DeclKind::Import) {
    return node->kind == NodeKind::TSImportEqualsDeclaration &&
           isExportParent(node->parent);
  }
  if (node->kind == NodeKind::VariableDeclarator) {
    node = node->parent;
  }
  return node && isExportParent(node->parent);
}

// ------------------------------------------------------- implicit uses

bool isSignatureLike(NodeKind kind)
{
  return ast::SignatureLike::matches(kind) || kind == NodeKind::TSDeclareFunction ||
         kind == NodeKind::TSEmptyBodyFunctionExpression;
}

/** Parameters of a setter, a signature, an overload or a parameter property. */
bool isImplicitlyUsedParameter(const Declaration *def)
{
  if (def->kind != DeclKind::Parameter) {
    return false;
  }
  if (def->node->kind == NodeKind::TSParameterProperty) {
    ast::TSParameterProperty p(def->node);
    return p.accessibility() != ast::Accessibility::None || p.isReadonly() ||
           p.isOverride();
  }
  Node *owner = def->scope->node;
  if (isSignatureLike(owner->kind)) {
    return true;
  }
  Node *holder = owner->parent;
  if (holder && holder->kind == NodeKind::MethodDefinition) {
    return ast::MethodDefinition(holder).kind() == ast::MethodKind::Set;
  }
  if (holder && holder->kind == NodeKind::Property) {
    return ast::Property(holder).kind() == ast::PropertyKind::Set;
  }
  return false;
}

/** A `for-in`/`for-of` whose whole body is a `return` uses its loop variable. */
bool isReturningLoopVariable(const Variable &v)
{
  auto bodyReturns = [](Node *loop) {
    Node *body = ast::Loop(loop).body();
    if (body && body->kind == NodeKind::BlockStatement) {
      span<Node *> list = ast::BlockStatement(body).body();
      if (list.size() != 1) {
        return false;
      }
      body = list[0];
    }
    return body && body->kind == NodeKind::ReturnStatement;
  };
  for (Declaration *def : v.defs) {
    if (def->isVariable()) {
      Node *declaration = def->node->parent;
      Node *loop = declaration ? declaration->parent : nullptr;
      if (loop &&
          (loop->kind == NodeKind::ForInStatement ||
           loop->kind == NodeKind::ForOfStatement) &&
          loop->children[0] == declaration && bodyReturns(loop))
      {
        return true;
      }
    }
  }
  for (Reference *ref : v.refs) {
    Node *loop = ref->id->parent;
    if (loop &&
        (loop->kind == NodeKind::ForInStatement ||
         loop->kind == NodeKind::ForOfStatement) &&
        loop->children[0] == ref->id && bodyReturns(loop))
    {
      return true;
    }
  }
  return false;
}

/** A `TSModuleBlock` or Program body with an export that overrides ambient visibility. */
bool hasOverridingExportStatement(span<Node *> body)
{
  for (Node *statement : body) {
    switch (statement->kind) {
    case NodeKind::ExportNamedDeclaration:
      if (!ast::ExportNamedDeclaration(statement).declaration()) {
        return true;
      }
      break;
    case NodeKind::ExportAllDeclaration:
    case NodeKind::TSExportAssignment:
      return true;
    case NodeKind::ExportDefaultDeclaration: {
      Node *decl = ast::ExportDefaultDeclaration(statement).declaration();
      if (decl && decl->kind == NodeKind::Identifier) {
        return true;
      }
      break;
    }
    default:
      break;
    }
  }
  return false;
}

void markDeclared(RuleContext &ctx, State &state, Node *id)
{
  if (!id) {
    return;
  }
  if (id->kind == NodeKind::Identifier) {
    if (Declaration *decl = ctx.bindings().declarationOf(id)) {
      state.ambientUsed.add(decl);
    }
    return;
  }
  id->descendants<ast::Identifier>([&](ast::Identifier leaf) {
    if (Declaration *decl = ctx.bindings().declarationOf(leaf.node())) {
      state.ambientUsed.add(decl);
    }
  });
}

/** Marks the ambient declarations directly in `body` as used. */
void markAmbientBody(RuleContext &ctx, State &state, span<Node *> body)
{
  if (hasOverridingExportStatement(body)) {
    return;
  }
  for (Node *statement : body) {
    switch (statement->kind) {
    case NodeKind::TSInterfaceDeclaration:
    case NodeKind::TSTypeAliasDeclaration:
    case NodeKind::ClassDeclaration:
    case NodeKind::TSEnumDeclaration:
      markDeclared(ctx, state, ast::NamedDeclaration(statement).id());
      break;
    case NodeKind::TSDeclareFunction:
      markDeclared(ctx, state, ast::FunctionLike(statement).id());
      break;
    case NodeKind::TSModuleDeclaration: {
      Node *id = ast::TSModuleDeclaration(statement).id();
      while (id && id->kind == NodeKind::TSQualifiedName) {
        id = id->children[0];
      }
      markDeclared(ctx, state, id);
      break;
    }
    case NodeKind::VariableDeclaration:
      for (Node *declarator : ast::VariableDeclaration(statement).declarations()) {
        markDeclared(ctx, state, ast::VariableDeclarator(declarator).id());
      }
      break;
    default:
      break;
    }
  }
}

bool underDeclareModule(const Node *node)
{
  for (const Node *n = node->parent; n; n = n->parent) {
    if (n->kind == NodeKind::TSModuleDeclaration &&
        ast::TSModuleDeclaration(const_cast<Node *>(n)).isDeclare())
    {
      return true;
    }
  }
  return false;
}

/**
 * Declarations in a `.d.ts` file's top level, in `declare module` blocks
 * and in namespaces nested in them describe the outside world, so nothing
 * in the file needs to reference them.
 */
void markAmbient(RuleContext &ctx, State &state)
{
  Node *root = ctx.file().root();
  if (state.options.definitionFile) {
    markAmbientBody(ctx, state, ast::Program(root).body());
  }
  root->descendants<ast::TSModuleDeclaration>([&](ast::TSModuleDeclaration module) {
    Node *body = module.body();
    if (!body || body->kind != NodeKind::TSModuleBlock) {
      return;
    }
    if (module.isDeclare() || state.options.definitionFile ||
        underDeclareModule(module.node()))
    {
      markAmbientBody(ctx, state, ast::TSModuleBlock(body).body());
    }
  });
}

// ----------------------------------------------------------------- filters

bool hasRestSibling(const Node *node)
{
  if (!node || node->kind != NodeKind::Property || !node->parent ||
      node->parent->kind != NodeKind::ObjectPattern)
  {
    return false;
  }
  span<Node *> props = ast::ObjectPattern(node->parent).properties();
  return props.size() > 0 && props[props.size() - 1]->kind == NodeKind::RestElement;
}

bool hasRestSpreadSibling(const Variable &v)
{
  for (Declaration *def : v.defs) {
    if (hasRestSibling(def->id->parent)) {
      return true;
    }
  }
  for (Reference *ref : v.refs) {
    if (hasRestSibling(ref->id->parent)) {
      return true;
    }
  }
  return false;
}

bool inArrayPattern(const Variable &v)
{
  if (v.defs[0]->id->parent && v.defs[0]->id->parent->kind == NodeKind::ArrayPattern) {
    return true;
  }
  for (Reference *ref : v.refs) {
    if (ref->id->parent && ref->id->parent->kind == NodeKind::ArrayPattern) {
      return true;
    }
  }
  return false;
}

bool classHasStaticBlock(const Node *cls)
{
  Node *body = ast::ClassLike(const_cast<Node *>(cls)).body();
  if (!body) {
    return false;
  }
  for (Node *member : ast::ClassBody(body).body()) {
    if (member->kind == NodeKind::StaticBlock) {
      return true;
    }
  }
  return false;
}

/** Whether no parameter after `def` in its function is referenced or implicitly used. */
/** A parameter that is a bare name, decorated or not, rather than a pattern. */
bool isPlainParameter(const Declaration *def)
{
  Node *holder = def->id->parent;
  if (holder && holder->kind == NodeKind::TSParameterProperty) {
    holder = holder->parent;
  }
  return holder && ast::FunctionLike::matches(holder->kind);
}

bool isAfterLastUsedArg(const Variable &v,
                        Map<const Declaration *, int> &index,
                        const Vector<Variable> &variables)
{
  Declaration *def = v.defs[0];
  bool after = false;
  for (Declaration *param : def->scope->declarations) {
    if (param == def) {
      after = true;
      continue;
    }
    if (!after || param->kind != DeclKind::Parameter || param->name == "this") {
      continue;
    }
    const int *slot = index.lookup_ptr(param);
    if (!slot) {
      continue;
    }
    const Variable &other = variables[*slot];
    if (!other.refs.isEmpty() || other.markedUsed) {
      return false;
    }
  }
  return true;
}

// ----------------------------------------------------------------- reports

enum class Kind : uint8_t { ArrayDestructure, CatchClause, Parameter, Variable };

struct Description {
  const Pattern *pattern;
  const char *text;
};

Description describe(const Options &o, Kind kind)
{
  switch (kind) {
  case Kind::ArrayDestructure:
    return {&o.destructuredArray, "elements of array destructuring"};
  case Kind::CatchClause:
    return {&o.caught, "caught errors"};
  case Kind::Parameter:
    return {&o.args_, "args"};
  default:
    return {&o.vars, "vars"};
  }
}

Kind kindOf(const Options &o, const Variable &v)
{
  Declaration *def = v.defs[0];
  if (o.destructuredArray.set && def->id->parent &&
      def->id->parent->kind == NodeKind::ArrayPattern)
  {
    return Kind::ArrayDestructure;
  }
  if (def->kind == DeclKind::CatchParam) {
    return Kind::CatchClause;
  }
  if (def->kind == DeclKind::Parameter) {
    return Kind::Parameter;
  }
  return Kind::Variable;
}

string_view additionalText(State &state, Kind kind, bool used)
{
  Description d = describe(state.options, kind);
  if (!d.pattern->set) {
    return "";
  }
  string text(used ? ". Used " : ". Allowed unused ");
  text += string(d.text);
  text += string(used ? " must not match " : " must match ");
  text += d.pattern->text;
  return state.intern(text);
}

bool hasWrite(const Variable &v)
{
  for (Reference *ref : v.refs) {
    if (ref->isWrite()) {
      return true;
    }
  }
  return false;
}

/** The last write in the variable's own function, else the declared name. */
Node *reportTarget(const Variable &v)
{
  Node *target = v.defs[0]->id;
  Scope *variableScope = v.scope()->variableScope();
  for (Reference *ref : v.refs) {
    if (ref->isWrite() && ref->scope->variableScope() == variableScope) {
      target = ref->id;
    }
  }
  return target;
}

/** The import specifier or `import x =` declaration a lone import definition removes. */
void addImportFixer(RuleContext &ctx,
                    const Options &o,
                    const Variable &v,
                    Report &r,
                    const Set<const Declaration *> &unused)
{
  Declaration *def = v.defs[0];
  Node *node = def->node;
  Node *target = node;
  const char *messageId = "removeUnusedImportDeclaration";
  if (node->kind == NodeKind::ImportSpecifier ||
      node->kind == NodeKind::ImportDefaultSpecifier ||
      node->kind == NodeKind::ImportNamespaceSpecifier)
  {
    Node *declaration = node->parent;
    bool allUnused = true;
    for (Node *spec : ast::ImportDeclaration(declaration).specifiers()) {
      Node *local = spec->children[spec->kind == NodeKind::ImportSpecifier ? 1 : 0];
      Declaration *other = local ? ctx.bindings().declarationOf(local) : nullptr;
      allUnused = allUnused && other && unused.contains(other);
    }
    if (allUnused || node->kind == NodeKind::ImportNamespaceSpecifier) {
      target = declaration;
    } else {
      messageId = "removeUnusedVar";
    }
  } else if (node->kind != NodeKind::TSImportEqualsDeclaration) {
    return;
  }
  FixFn fix = [target](ast::Fixer &fixer) { fixer.remove(target); };
  if (o.autofixImports) {
    r.fix = std::move(fix);
    return;
  }
  Suggestion s;
  s.messageId = messageId;
  s.data.append({"varName", v.name()});
  s.fix = std::move(fix);
  r.suggestions.append(std::move(s));
}

void report(RuleContext &ctx,
            State &state,
            const Variable &v,
            const char *messageId,
            Kind kind,
            bool used,
            const Set<const Declaration *> &unused)
{
  Node *target = reportTarget(v);
  Report r;
  r.node = target;
  r.at(target->start, target->start + uint32_t(v.name().size()));
  r.messageId = messageId;
  r.data.append({"varName", v.name()});
  if (!used) {
    r.data.append({"action", hasWrite(v) ? "assigned a value" : "defined"});
  }
  r.data.append({"additional", additionalText(state, kind, used)});
  if (!used && v.defs.size() == 1 && v.defs[0]->kind == DeclKind::Import) {
    addImportFixer(ctx, state.options, v, r, unused);
  }
  ctx.report(std::move(r));
}

// ------------------------------------------------------------------- driver

/** Names scope-manager never records or always counts as used. */
bool skipDeclaration(const Declaration *decl)
{
  switch (decl->kind) {
  case DeclKind::EnumMember:
    return true;
  case DeclKind::Parameter:
    return decl->name == "this";
  case DeclKind::Function:
    return decl->node->kind == NodeKind::FunctionExpression;
  case DeclKind::Class:
    return decl->node->kind == NodeKind::ClassExpression;
  case DeclKind::Namespace: {
    Node *id = ast::TSModuleDeclaration(decl->node).id();
    return !id || id->kind != NodeKind::Identifier;
  }
  case DeclKind::TypeParameter:
    return decl->id->parent && decl->id->parent->kind == NodeKind::TSMappedType;
  default:
    return false;
  }
}

void collect(Scope *scope, Vector<Variable> &out, Map<const Declaration *, int> &index)
{
  for (Declaration *decl : scope->declarations) {
    if (scope->lookupLocal(decl->name) != decl) {
      continue;
    }
    Variable v;
    for (Declaration *d = decl; d; d = d->nextSameName) {
      if (skipDeclaration(d)) {
        continue;
      }
      v.defs.append(d);
      for (Reference *ref : d->references) {
        v.refs.append(ref);
      }
    }
    if (v.defs.isEmpty()) {
      continue;
    }
    v.refs.sort([](Reference *const &a, Reference *const &b) {
      return a->id->start < b->id->start ? -1 : a->id->start > b->id->start ? 1 : 0;
    });
    for (Declaration *d : v.defs) {
      index.add(static_cast<const Declaration *>(d), int(out.size()));
    }
    out.append(std::move(v));
  }
  for (Scope *child : scope->children) {
    collect(child, out, index);
  }
}

void checkProgram(RuleContext &ctx, State &state)
{
  const Options &o = state.options;
  markAmbient(ctx, state);

  Vector<Variable> variables;
  Map<const Declaration *, int> index;
  collect(ctx.bindings().moduleScope(), variables, index);

  Vector<int> used;
  for (Variable &v : variables) {
    bool exported = false;
    for (Declaration *def : v.defs) {
      exported = exported || isExported(def);
      v.markedUsed = v.markedUsed || state.ambientUsed.contains(def) ||
                     isImplicitlyUsedParameter(def);
    }
    v.markedUsed = v.markedUsed || isReturningLoopVariable(v);
    used.append(v.markedUsed || exported || isUsedVariable(v));
  }

  // Which unused variables survive the options, so import fixers know
  // whether a whole declaration goes.
  Set<const Declaration *> unused;
  Vector<int> reportUnused;
  for (int i = 0; i < int(variables.size()); i++) {
    const Variable &v = variables[i];
    Declaration *def = v.defs[0];
    bool isUsed = used[i] != 0;
    bool keep = false;
    Kind usedKind = Kind::Variable;
    bool reportUsed = false;
    if (o.varsLocal && o.script && v.scope()->kind == ast::ScopeKind::Module) {
      // A script's top-level names are globals.
    } else if (o.destructuredArray.set && inArrayPattern(v) &&
               o.destructuredArray.matches(v.name()))
    {
      usedKind = Kind::ArrayDestructure;
      reportUsed = true;
    } else if (def->kind == DeclKind::Class && o.ignoreClassWithStaticInitBlock &&
               classHasStaticBlock(def->node))
    {
    } else if (def->kind == DeclKind::CatchParam) {
      if (!o.caughtErrors) {
      } else if (o.caught.matches(v.name())) {
        usedKind = Kind::CatchClause;
        reportUsed = true;
      } else {
        keep = true;
      }
    } else if (def->kind == DeclKind::Parameter) {
      if (o.args == Args::None) {
      } else if (o.args_.matches(v.name())) {
        usedKind = Kind::Parameter;
        reportUsed = true;
      } else if (o.args == Args::AfterUsed && isPlainParameter(def) &&
                 !isAfterLastUsedArg(v, index, variables))
      {
      } else {
        keep = true;
      }
    } else if (o.vars.matches(v.name())) {
      usedKind = Kind::Variable;
      reportUsed = true;
    } else {
      keep = true;
    }
    if (reportUsed) {
      if (o.reportUsedIgnorePattern && isUsed) {
        report(ctx, state, v, "usedIgnoredVar", usedKind, true, unused);
      }
      continue;
    }
    if (!keep) {
      reportUnused.append(0);
      continue;
    }
    if (def->isVariable() && o.ignoreUsingDeclarations && def->kind == DeclKind::Using) {
      keep = false;
    }
    if (keep && o.ignoreRestSiblings && hasRestSpreadSibling(v)) {
      keep = false;
    }
    if (keep && !isUsed) {
      for (Declaration *d : v.defs) {
        unused.add(static_cast<const Declaration *>(d));
      }
      reportUnused.append(i + 1);
    }
  }

  for (int slot : reportUnused) {
    if (slot == 0) {
      continue;
    }
    const Variable &v = variables[slot - 1];
    bool onlyAsType = false;
    for (Reference *ref : v.refs) {
      onlyAsType = onlyAsType || inTypeQuery(ref->id) || inTypePredicate(ref->id);
    }
    if (onlyAsType && v.defs[0]->kind == DeclKind::Import) {
      continue;
    }
    report(ctx,
           state,
           v,
           onlyAsType ? "usedOnlyAsType" : "unusedVar",
           kindOf(o, v),
           false,
           unused);
  }
}

void create(RuleContext &ctx)
{
  State *state = ctx.state<State>();
  ctx.onExit(NodeKind::Program, [&ctx, state](Node *) {
    readOptions(ctx, state->options);
    checkProgram(ctx, *state);
  });
}

} // namespace

const RuleDef kNoUnusedVars{
    {
        "no-unused-vars",
        "Disallow unused variables",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/no-unused-vars.md",
        /*recommended=*/true,
        /*fixable=*/true,
        /*hasSuggestions=*/true,
        /*typeAware=*/false,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
