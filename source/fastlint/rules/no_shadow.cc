// no-shadow: disallow a declaration whose name an outer scope already
// declares (docs/rules/no-shadow.md). A port of typescript-eslint's
// extension of ESLint's rule, answered from the binder.

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"
#include "util/pool.h"

#include <charconv>

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Declaration;
using ast::DeclKind;
using ast::Node;
using ast::NodeKind;
using ast::Scope;
using litestl::util::Pool;

constexpr Message kMessages[] = {
    {"noShadow",
     "'{{name}}' is already declared in the upper scope on line {{shadowedLine}} column "
     "{{shadowedColumn}}."},
    {"noShadowGlobal", "'{{name}}' is already a global variable."},
    {"noEnumShadow",
     "Enum members are added to the enum scope, so references to '{{name}}' in enum "
     "member initializers resolve to this member instead of the declaration in the upper "
     "scope on line {{shadowedLine}} column {{shadowedColumn}}."},
};

/** The ECMAScript standard globals; `builtinGlobals` reports names that shadow one. */
constexpr string_view kBuiltinGlobals[] = {
    "AggregateError",
    "Array",
    "ArrayBuffer",
    "Atomics",
    "BigInt",
    "BigInt64Array",
    "BigUint64Array",
    "Boolean",
    "DataView",
    "Date",
    "Error",
    "EvalError",
    "FinalizationRegistry",
    "Float32Array",
    "Float64Array",
    "Function",
    "Infinity",
    "Int16Array",
    "Int32Array",
    "Int8Array",
    "Intl",
    "Iterator",
    "JSON",
    "Map",
    "Math",
    "NaN",
    "Number",
    "Object",
    "Promise",
    "Proxy",
    "RangeError",
    "ReferenceError",
    "Reflect",
    "RegExp",
    "Set",
    "SharedArrayBuffer",
    "String",
    "Symbol",
    "SyntaxError",
    "TypeError",
    "URIError",
    "Uint16Array",
    "Uint32Array",
    "Uint8Array",
    "Uint8ClampedArray",
    "WeakMap",
    "WeakRef",
    "WeakSet",
    "decodeURI",
    "decodeURIComponent",
    "encodeURI",
    "encodeURIComponent",
    "escape",
    "eval",
    "globalThis",
    "isFinite",
    "isNaN",
    "parseFloat",
    "parseInt",
    "undefined",
    "unescape",
};

bool isBuiltinGlobal(string_view name)
{
  for (string_view global : kBuiltinGlobals) {
    if (global == name) {
      return true;
    }
  }
  return false;
}

enum class Hoist : uint8_t { All, Functions, FunctionsAndTypes, Never, Types };

struct Options {
  Hoist hoist = Hoist::FunctionsAndTypes;
  bool builtinGlobals = false;
  bool ignoreOnInitialization = false;
  bool ignoreTypeValueShadow = true;
  bool ignoreFunctionTypeParameterNameValueShadow = true;
  const JsonValue *allow = nullptr;
  bool module = true;
  bool definitionFile = false;
};

Hoist hoistOf(string_view text)
{
  if (text == "all") {
    return Hoist::All;
  }
  if (text == "functions") {
    return Hoist::Functions;
  }
  if (text == "never") {
    return Hoist::Never;
  }
  if (text == "types") {
    return Hoist::Types;
  }
  return Hoist::FunctionsAndTypes;
}

bool endsWith(string_view text, string_view suffix)
{
  return text.size() >= suffix.size() &&
         text.substr(text.size() - suffix.size()) == suffix;
}

Options readOptions(RuleContext &ctx)
{
  Options o;
  if (const JsonValue *opt = ctx.option()) {
    o.hoist = hoistOf(opt->getString("hoist"));
    o.builtinGlobals = opt->getBool("builtinGlobals", false);
    o.ignoreOnInitialization = opt->getBool("ignoreOnInitialization", false);
    o.ignoreTypeValueShadow = opt->getBool("ignoreTypeValueShadow", true);
    o.ignoreFunctionTypeParameterNameValueShadow =
        opt->getBool("ignoreFunctionTypeParameterNameValueShadow", true);
    o.allow = opt->get("allow");
  }
  string_view name = ctx.filename();
  bool typescript = endsWith(name, ".ts") || endsWith(name, ".tsx") ||
                    endsWith(name, ".mts") || endsWith(name, ".cts");
  o.module = typescript ||
             ast::Program(ctx.file().root()).sourceType() == ast::SourceType::Module;
  o.definitionFile =
      endsWith(name, ".d.ts") || endsWith(name, ".d.mts") || endsWith(name, ".d.cts");
  return o;
}

/** Storage for the line and column texts a report interpolates. */
struct State {
  Options options;
  Pool<string, 16> texts;

  string_view number(uint32_t value)
  {
    char buffer[16];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer) - 1, value);
    *result.ptr = '\0';
    string *text = texts.alloc();
    *text = string(buffer);
    return string_view(text->c_str(), text->size());
  }
};

bool isValue(const Declaration *decl)
{
  return decl->inSpace(ast::Space::Value);
}

/** The span of the name alone; a type parameter's node also holds its constraint. */
uint32_t nameEnd(const Declaration *decl)
{
  if (decl->kind == DeclKind::TypeParameter) {
    return decl->id->start + uint32_t(decl->id->text.size());
  }
  return decl->id->end;
}

bool inRange(const Node *node, uint32_t offset)
{
  return node && node->start <= offset && offset <= node->end;
}

bool inGlobalAugmentation(const Scope *scope)
{
  for (const Node *n = scope->node; n; n = n->parent) {
    if (n->kind == NodeKind::TSModuleDeclaration &&
        ast::TSModuleDeclaration(const_cast<Node *>(n)).kind() == ast::ModuleKind::Global)
    {
      return true;
    }
  }
  return false;
}

bool isAllowed(const Options &o, string_view name)
{
  if (!o.allow || !o.allow->isArray()) {
    return false;
  }
  for (int i = 0; i < o.allow->size(); i++) {
    const JsonValue *item = o.allow->at(i);
    if (item && item->isString() && item->asString() == name) {
      return true;
    }
  }
  return false;
}

/** A `declare`d variable, class, enum or namespace in a `.d.ts` file. */
bool isDeclareInDefinitionFile(const Options &o, const Declaration *decl)
{
  if (!o.definitionFile) {
    return false;
  }
  Node *node = decl->node;
  switch (decl->kind) {
  case DeclKind::Var:
  case DeclKind::Let:
  case DeclKind::Const:
  case DeclKind::Using:
    return node->parent && node->parent->kind == NodeKind::VariableDeclaration &&
           ast::VariableDeclaration(node->parent).isDeclare();
  case DeclKind::Class:
    return node->kind == NodeKind::ClassDeclaration &&
           ast::ClassDeclaration(node).isDeclare();
  case DeclKind::Enum:
    return ast::TSEnumDeclaration(node).isDeclare();
  case DeclKind::Namespace:
    return ast::TSModuleDeclaration(node).isDeclare();
  default:
    return false;
  }
}

/** A type shadowing a value or a value shadowing a type; a global counts as a value. */
bool isTypeValueShadow(const Options &o,
                       const Declaration *decl,
                       const Declaration *shadowed)
{
  if (!o.ignoreTypeValueShadow) {
    return false;
  }
  bool shadowedIsValue = !shadowed || isValue(shadowed);
  return isValue(decl) != shadowedIsValue;
}

/** A parameter of a function type or signature named like a value. */
bool isFunctionTypeParameterShadow(const Options &o,
                                   const Declaration *decl,
                                   const Declaration *shadowed)
{
  if (!o.ignoreFunctionTypeParameterNameValueShadow ||
      decl->kind != DeclKind::Parameter || (shadowed && !isValue(shadowed)))
  {
    return false;
  }
  switch (decl->scope->node->kind) {
  case NodeKind::TSCallSignatureDeclaration:
  case NodeKind::TSFunctionType:
  case NodeKind::TSMethodSignature:
  case NodeKind::TSEmptyBodyFunctionExpression:
  case NodeKind::TSDeclareFunction:
  case NodeKind::TSConstructSignatureDeclaration:
  case NodeKind::TSConstructorType:
    return true;
  default:
    return false;
  }
}

/** The node that owns a type parameter's list, or null. */
Node *typeParameterOwner(const Declaration *decl)
{
  if (decl->kind != DeclKind::TypeParameter) {
    return nullptr;
  }
  Node *list = decl->id->parent;
  if (!list || list->kind != NodeKind::TSTypeParameterDeclaration) {
    return nullptr;
  }
  return list->parent;
}

/** A static method's type parameter shadowing its class's type parameter. */
bool isGenericOfStaticMethodShadow(const Declaration *decl, const Declaration *shadowed)
{
  Node *fn = typeParameterOwner(decl);
  if (!fn || (fn->kind != NodeKind::FunctionExpression &&
              fn->kind != NodeKind::TSEmptyBodyFunctionExpression))
  {
    return false;
  }
  Node *method = fn->parent;
  if (!method || method->kind != NodeKind::MethodDefinition ||
      !ast::MethodDefinition(method).isStatic())
  {
    return false;
  }
  Node *cls = shadowed ? typeParameterOwner(shadowed) : nullptr;
  return cls && ast::ClassLike::matches(cls->kind);
}

string_view unquote(string_view text)
{
  if (text.size() >= 2 && (text.front() == '\'' || text.front() == '"')) {
    return text.substr(1, text.size() - 2);
  }
  return text;
}

/** An interface or alias inside `declare module 'm'` merging with a type import from `m`.
 */
bool isExternalDeclarationMerging(const Scope *scope,
                                  const Declaration *decl,
                                  const Declaration *shadowed)
{
  if (!shadowed || shadowed->kind != DeclKind::Import || isValue(shadowed)) {
    return false;
  }
  Node *import = shadowed->node->parent;
  if (!import || import->kind != NodeKind::ImportDeclaration) {
    return false;
  }
  Node *source = ast::ImportDeclaration(import).source();
  if (!source || scope->node->kind != NodeKind::TSModuleDeclaration) {
    return false;
  }
  Node *id = ast::TSModuleDeclaration(scope->node).id();
  if (!id || id->kind != NodeKind::Literal || unquote(id->text) != unquote(source->text))
  {
    return false;
  }
  return decl->node->kind == NodeKind::TSInterfaceDeclaration ||
         decl->node->kind == NodeKind::TSTypeAliasDeclaration;
}

/** Climbs out of `||`, `&&`, `??` and the branches of `?:`. */
Node *unwrapExpression(Node *node)
{
  Node *parent = node->parent;
  if (parent && (parent->kind == NodeKind::LogicalExpression ||
                 (parent->kind == NodeKind::ConditionalExpression &&
                  ast::ConditionalExpression(parent).test() != node)))
  {
    return unwrapExpression(parent);
  }
  return node;
}

/** A named function or class expression that initializes the variable it shadows. */
bool isOnInitializer(const Declaration *decl, const Declaration *shadowed)
{
  Node *inner = decl->node;
  bool namedExpression =
      (decl->kind == DeclKind::Function && inner->kind == NodeKind::FunctionExpression) ||
      (decl->kind == DeclKind::Class && inner->kind == NodeKind::ClassExpression);
  if (!namedExpression || !shadowed) {
    return false;
  }
  Node *outer = shadowed->id->parent;
  Node *init = nullptr;
  if (outer && outer->kind == NodeKind::VariableDeclarator) {
    init = ast::VariableDeclarator(outer).init();
  } else if (outer && outer->kind == NodeKind::AssignmentPattern) {
    init = ast::AssignmentPattern(outer).right();
  }
  if (!init || init->start > inner->start || inner->end > init->end) {
    return false;
  }
  return init == unwrapExpression(inner);
}

/**
 * A name declared by a function expression that a call in the shadowed
 * variable's initializer receives, while that variable is still
 * uninitialized (`const a = [].find(a => a)`).
 */
bool isInitPatternNode(const Declaration *decl, const Declaration *shadowed)
{
  if (!shadowed) {
    return false;
  }
  // An expression's own name belongs outside the function it names.
  if (decl->node == decl->scope->node) {
    return false;
  }
  Scope *variableScope = decl->scope->variableScope();
  Node *fn = variableScope->node;
  if ((fn->kind != NodeKind::ArrowFunctionExpression &&
       fn->kind != NodeKind::FunctionExpression) ||
      variableScope->parent != shadowed->scope)
  {
    return false;
  }
  Node *call = fn->parent;
  while (call && call->kind != NodeKind::CallExpression) {
    call = call->parent;
  }
  if (!call) {
    return false;
  }
  uint32_t location = call->end;
  for (Node *node = shadowed->id; node; node = node->parent) {
    switch (node->kind) {
    case NodeKind::VariableDeclarator: {
      if (inRange(ast::VariableDeclarator(node).init(), location)) {
        return true;
      }
      Node *loop = node->parent ? node->parent->parent : nullptr;
      if (loop &&
          (loop->kind == NodeKind::ForInStatement ||
           loop->kind == NodeKind::ForOfStatement) &&
          inRange(loop->children[1], location))
      {
        return true;
      }
      return false;
    }
    case NodeKind::AssignmentPattern:
      if (inRange(ast::AssignmentPattern(node).right(), location)) {
        return true;
      }
      break;
    case NodeKind::ArrowFunctionExpression:
    case NodeKind::CatchClause:
    case NodeKind::ClassDeclaration:
    case NodeKind::ClassExpression:
    case NodeKind::ExportNamedDeclaration:
    case NodeKind::FunctionDeclaration:
    case NodeKind::FunctionExpression:
    case NodeKind::ImportDeclaration:
      return false;
    default:
      break;
    }
  }
  return false;
}

/** Whether the inner name precedes the outer declaration that `hoist` leaves unhoisted.
 */
bool isInTdz(const Options &o, const Declaration *decl, const Declaration *shadowed)
{
  if (!shadowed || nameEnd(decl) >= shadowed->id->start) {
    return false;
  }
  NodeKind outer = shadowed->node->kind;
  bool function = outer == NodeKind::FunctionDeclaration;
  bool type = outer == NodeKind::TSInterfaceDeclaration ||
              outer == NodeKind::TSTypeAliasDeclaration;
  switch (o.hoist) {
  case Hoist::Functions:
    return !function;
  case Hoist::Types:
    return !type;
  case Hoist::FunctionsAndTypes:
    return !function && !type;
  default:
    return true;
  }
}

/** The name a function or class expression declares for itself in `scope`, or null. */
Declaration *selfName(const Scope *scope, string_view name)
{
  Node *owner = scope->node;
  if (owner->kind != NodeKind::FunctionExpression &&
      owner->kind != NodeKind::ClassExpression)
  {
    return nullptr;
  }
  Declaration *self = scope->lookupLocal(name);
  if (self && self->node == owner &&
      (self->kind == DeclKind::Function || self->kind == DeclKind::Class))
  {
    return self;
  }
  return nullptr;
}

/**
 * The declaration `decl` shadows: the function or class expression's own
 * name when `decl` sits in that expression's scope, else the nearest
 * declaration of the name outside the scope. An expression's own name
 * counts as one scope further out than the declarations beside it, so a
 * declaration of the same name in the expression's scope shadows it and is
 * itself what nested scopes shadow. Null when there is none.
 */
Declaration *shadowedBy(Scope *scope, Declaration *decl)
{
  Declaration *self = selfName(scope, decl->name);
  if (self && self != decl) {
    return self;
  }
  for (Scope *s = scope->parent; s; s = s->parent) {
    Declaration *found = s->lookupLocal(decl->name);
    if (!found) {
      continue;
    }
    if (found == selfName(s, decl->name) && found->nextSameName) {
      return found->nextSameName;
    }
    return found;
  }
  return nullptr;
}

void checkScope(RuleContext &ctx, State &state, Scope *scope)
{
  const Options &o = state.options;
  if (inGlobalAugmentation(scope)) {
    return;
  }
  for (Declaration *decl : scope->declarations) {
    if (decl->kind == DeclKind::Parameter && decl->name == "this") {
      continue;
    }
    if (isAllowed(o, decl->name) || isDeclareInDefinitionFile(o, decl)) {
      continue;
    }
    Declaration *shadowed = shadowedBy(scope, decl);
    // A top-level module name shadows a builtin global; a script's does not.
    bool global = !shadowed && o.builtinGlobals && isBuiltinGlobal(decl->name) &&
                  (scope->parent || o.module);
    if (!shadowed && !global) {
      continue;
    }
    if (isTypeValueShadow(o, decl, shadowed) ||
        isFunctionTypeParameterShadow(o, decl, shadowed) ||
        isGenericOfStaticMethodShadow(decl, shadowed) ||
        isExternalDeclarationMerging(scope, decl, shadowed) ||
        isOnInitializer(decl, shadowed) ||
        (o.ignoreOnInitialization && isInitPatternNode(decl, shadowed)) ||
        (o.hoist != Hoist::All && isInTdz(o, decl, shadowed)))
    {
      continue;
    }
    Report r;
    r.node = decl->id;
    r.at(decl->id->start, nameEnd(decl));
    r.data.append({"name", decl->name});
    if (!shadowed) {
      r.messageId = "noShadowGlobal";
    } else {
      r.messageId = shadowed->kind == DeclKind::Enum ? "noEnumShadow" : "noShadow";
      r.data.append({"shadowedLine", state.number(lineOf(ctx, shadowed->id->start))});
      r.data.append({"shadowedColumn", state.number(columnOf(ctx, shadowed->id->start))});
    }
    ctx.report(std::move(r));
  }
  for (Scope *child : scope->children) {
    checkScope(ctx, state, child);
  }
}

void create(RuleContext &ctx)
{
  State *state = ctx.state<State>();
  ctx.onExit(NodeKind::Program, [&ctx, state](Node *) {
    state->options = readOptions(ctx);
    checkScope(ctx, *state, ctx.bindings().moduleScope());
  });
}

} // namespace

const RuleDef kNoShadow{
    {
        "no-shadow",
        "Disallow variable declarations from shadowing variables declared in the outer "
        "scope",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/no-shadow.md",
        /*recommended=*/false,
        /*fixable=*/false,
        /*hasSuggestions=*/false,
        /*typeAware=*/false,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
