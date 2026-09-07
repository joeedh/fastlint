// consistent-type-imports: `import type` for imports only used as types
// (docs/rules/consistent-type-imports.md).

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"

#include "util/pool.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Declaration;
using ast::ImportKind;
using ast::Node;
using ast::NodeKind;
using ast::Reference;
using ast::Space;
using litestl::util::Pool;

using Nodes = Vector<Node *, 4>;

constexpr Message kMessages[] = {
    {"avoidImportType", "Use an `import` instead of an `import type`."},
    {"noImportTypeAnnotations", "`import()` type annotations are forbidden."},
    {"someImportsAreOnlyTypes", "Imports {{typeImports}} are only used as type."},
    {"typeOverValue",
     "All imports in the declaration are only used as types. Use `import type`."},
};

enum class Prefer { TypeImports, NoTypeImports };
enum class FixStyle { Separate, Inline };

struct Options {
  Prefer prefer = Prefer::TypeImports;
  FixStyle fixStyle = FixStyle::Separate;
  bool disallowTypeAnnotations = true;
};

/** A value import whose specifiers include some only used as types. */
struct ValueImport {
  Node *node = nullptr;
  Nodes typeSpecifiers;
  Vector<Node *, 2> inlineTypeSpecifiers;
  Vector<Node *, 2> valueSpecifiers;
  Vector<Node *, 2> unusedSpecifiers;
};

/** The imports of one module a fix may merge into; copied into each fix. */
struct Group {
  /** `import type { A }` with named specifiers only. */
  Node *typeOnlyNamedImport = nullptr;
  /** A value import with a default specifier or named specifiers only. */
  Node *valueImport = nullptr;
  /** A value import with named specifiers only. */
  Node *valueOnlyNamedImport = nullptr;
};

/** Every import of one module specifier, as upstream groups them. */
struct SourceImports {
  string_view source;
  Group group;
  Vector<ValueImport, 2> reports;
};

struct State {
  Options options;
  Vector<SourceImports, 8> sources;
  Pool<string, 4> texts;
  bool hasJsx = false;

  SourceImports &forSource(string_view source)
  {
    for (SourceImports &s : sources) {
      if (s.source == source) {
        return s;
      }
    }
    SourceImports fresh;
    fresh.source = source;
    sources.append(std::move(fresh));
    return sources[int(sources.size()) - 1];
  }

  string_view intern(const string &value)
  {
    string *text = texts.alloc();
    *text = value;
    return string_view(text->c_str(), text->size());
  }
};

void readOptions(RuleContext &ctx, Options &o)
{
  const JsonValue *opt = ctx.option();
  if (!opt) {
    return;
  }
  o.prefer = opt->getString("prefer") == "no-type-imports" ? Prefer::NoTypeImports
                                                           : Prefer::TypeImports;
  o.fixStyle = opt->getString("fixStyle") == "inline-type-imports" ? FixStyle::Inline
                                                                   : FixStyle::Separate;
  o.disallowTypeAnnotations = opt->getBool("disallowTypeAnnotations", true);
}

// ------------------------------------------------------------- classifying

bool contains(const Nodes &nodes, const Node *node)
{
  for (Node *n : nodes) {
    if (n == node) {
      return true;
    }
  }
  return false;
}

Node *localOf(Node *specifier)
{
  return specifier->kind == NodeKind::ImportSpecifier
             ? ast::ImportSpecifier(specifier).local()
             : specifier->children[0];
}

bool isExportPosition(const Node *parent)
{
  return parent && (parent->kind == NodeKind::ExportSpecifier ||
                    parent->kind == NodeKind::ExportDefaultDeclaration ||
                    parent->kind == NodeKind::TSExportAssignment);
}

/**
 * Whether a value reference is still safe for a type-only import: the name
 * under `typeof`, or the computed key of a property signature (`{ [foo.bar]:
 * string }`), reached through member accesses.
 */
bool isTypeSafeValueReference(const Reference *ref)
{
  if ((ref->flags & Reference::TypeQuery) != 0) {
    return true;
  }
  const Node *child = ref->id;
  const Node *parent = child->parent;
  while (parent) {
    switch (parent->kind) {
    case NodeKind::TSTypeQuery:
      return true;
    case NodeKind::TSQualifiedName:
      if (parent->children[0] != child) {
        return false;
      }
      break;
    case NodeKind::TSPropertySignature:
      return ast::TSPropertySignature(const_cast<Node *>(parent)).key() == child;
    case NodeKind::MemberExpression:
      if (ast::MemberExpression(const_cast<Node *>(parent)).object() != child) {
        return false;
      }
      break;
    default:
      return false;
    }
    child = parent;
    parent = parent->parent;
  }
  return false;
}

/** Whether `ref` only needs the imported name as a type. */
bool isTypeReference(const Reference *ref, ImportKind importKind)
{
  const Node *parent = ref->id->parent;
  if (ref->space == Space::Either) {
    // `export { T }`, `export default T` and `export = T` keep the import's kind.
    if (isExportPosition(parent)) {
      return importKind == ImportKind::Type;
    }
    // The head of a qualified name: a type when the name is a type, a value
    // when it is the module reference of `import x = A.B`.
    while (parent && parent->kind == NodeKind::TSQualifiedName) {
      parent = parent->parent;
    }
    return parent && parent->kind != NodeKind::TSImportEqualsDeclaration;
  }
  if (ref->space == Space::Value) {
    return isTypeSafeValueReference(ref);
  }
  return true;
}

// -------------------------------------------------------------- the fixes

struct Specifiers {
  Node *defaultSpecifier = nullptr;
  Node *namespaceSpecifier = nullptr;
  Nodes named;
};

Specifiers classify(Node *declaration)
{
  Specifiers out;
  span<Node *> specs = ast::ImportDeclaration(declaration).specifiers();
  for (size_t i = 0; i < specs.size(); i++) {
    Node *s = specs[i];
    if (s->kind == NodeKind::ImportDefaultSpecifier && i == 0) {
      out.defaultSpecifier = s;
    } else if (s->kind == NodeKind::ImportNamespaceSpecifier) {
      out.namespaceSpecifier = s;
    } else if (s->kind == NodeKind::ImportSpecifier) {
      out.named.append(s);
    }
  }
  return out;
}

bool hasAttributes(Node *declaration)
{
  Node *attributes = ast::ImportDeclaration(declaration).attributes();
  return attributes && ast::ImportAttributes(attributes).attributes().size() > 0;
}

/** A fresh import of the same module with no specifiers yet. */
Node *newImport(ast::Fixer &fixer, Node *like, ImportKind kind)
{
  Node *source = ast::ImportDeclaration(like).source();
  Node *literal = fixer.literal(ast::LiteralKind::String, source->text);
  Node *fresh = fixer.build(NodeKind::ImportDeclaration, {literal, nullptr});
  fresh->setDataByte(0, uint8_t(kind));
  return fresh;
}

/** Moves `specifiers` out of their import into a new one inserted before `node`. */
void splitOut(ast::Fixer &fixer,
              Node *node,
              const Nodes &specifiers,
              ImportKind kind,
              bool inlineType)
{
  Node *fresh = newImport(fixer, node, kind);
  for (Node *spec : specifiers) {
    fixer.detach(spec);
    if (inlineType) {
      fixer.setData(spec, 0, uint8_t(ImportKind::Type));
    }
    fixer.append(fresh, spec);
  }
  fixer.insertBefore(node, fresh);
}

/** `import type ...`; inline `type` modifiers would then be redundant. */
void toTypeDeclaration(ast::Fixer &fixer, Node *node)
{
  fixer.setData(node, 0, uint8_t(ImportKind::Type));
  for (Node *spec : ast::ImportDeclaration(node).specifiers()) {
    if (spec->kind == NodeKind::ImportSpecifier &&
        ast::ImportSpecifier(spec).importKind() == ImportKind::Type)
    {
      fixer.setData(spec, 0, uint8_t(ImportKind::Value));
    }
  }
}

/** Adds inline `type` to the named specifiers only used as types. */
void inlineTypes(ast::Fixer &fixer, const ValueImport &report, const Group &source)
{
  if (!source.valueImport) {
    return;
  }
  Specifiers valueSpecifiers = classify(source.valueImport);
  if (!source.valueOnlyNamedImport && valueSpecifiers.named.isEmpty()) {
    return;
  }
  for (Node *spec : classify(report.node).named) {
    if (contains(report.typeSpecifiers, spec)) {
      fixer.setData(spec, 0, uint8_t(ImportKind::Type));
    }
  }
}

void toTypeImports(ast::Fixer &fixer,
                   const ValueImport &report,
                   const Group &source,
                   FixStyle style)
{
  Node *node = report.node;
  Specifiers s = classify(node);
  auto isType = [&](Node *spec) { return contains(report.typeSpecifiers, spec); };
  bool inlineStyle = style == FixStyle::Inline;

  if (s.namespaceSpecifier && !s.defaultSpecifier) {
    if (!hasAttributes(node)) {
      toTypeDeclaration(fixer, node);
    }
    return;
  }
  if (s.defaultSpecifier) {
    if (isType(s.defaultSpecifier) && s.named.isEmpty() && !s.namespaceSpecifier) {
      toTypeDeclaration(fixer, node);
      return;
    }
    if (inlineStyle && !isType(s.defaultSpecifier) && !s.named.isEmpty() &&
        !s.namespaceSpecifier)
    {
      inlineTypes(fixer, report, source);
      return;
    }
  } else if (!s.namespaceSpecifier) {
    bool any = false;
    bool all = true;
    for (Node *spec : s.named) {
      any = any || isType(spec);
      all = all && isType(spec);
    }
    if (inlineStyle && any) {
      inlineTypes(fixer, report, source);
      return;
    }
    if (all) {
      toTypeDeclaration(fixer, node);
      return;
    }
  }

  size_t total = ast::ImportDeclaration(node).specifiers().size();
  Nodes typeNamed;
  for (Node *spec : s.named) {
    if (isType(spec)) {
      typeNamed.append(spec);
    }
  }
  if (!typeNamed.isEmpty()) {
    if (source.typeOnlyNamedImport && source.typeOnlyNamedImport != node) {
      for (Node *spec : typeNamed) {
        fixer.detach(spec);
        fixer.append(source.typeOnlyNamedImport, spec);
      }
    } else {
      splitOut(fixer,
               node,
               typeNamed,
               inlineStyle ? ImportKind::Value : ImportKind::Type,
               inlineStyle);
    }
  }
  if (s.namespaceSpecifier && isType(s.namespaceSpecifier)) {
    splitOut(fixer, node, Nodes{s.namespaceSpecifier}, ImportKind::Type, false);
  }
  if (s.defaultSpecifier && isType(s.defaultSpecifier)) {
    if (report.typeSpecifiers.size() == total) {
      fixer.setData(node, 0, uint8_t(ImportKind::Type));
    } else {
      splitOut(fixer, node, Nodes{s.defaultSpecifier}, ImportKind::Type, false);
    }
  }
}

// ------------------------------------------------------------------ driver

string_view wordList(State &state, const Nodes &specifiers)
{
  string out;
  for (size_t i = 0; i < specifiers.size(); i++) {
    if (i > 0) {
      out += i + 1 == specifiers.size() ? " and " : ", ";
    }
    out += '"';
    out += std::string(localOf(specifiers[int(i)])->text);
    out += '"';
  }
  return state.intern(out);
}

void collect(RuleContext &ctx, State &state, Node *node)
{
  ast::ImportDeclaration decl(node);
  Node *sourceNode = decl.source();
  if (!sourceNode) {
    return;
  }
  SourceImports &source = state.forSource(sourceNode->text);
  span<Node *> specs = decl.specifiers();
  bool allNamed = true;
  bool hasDefault = false;
  for (Node *spec : specs) {
    allNamed = allNamed && spec->kind == NodeKind::ImportSpecifier;
    hasDefault = hasDefault || spec->kind == NodeKind::ImportDefaultSpecifier;
  }
  Group &group = source.group;
  if (decl.importKind() == ImportKind::Type) {
    if (!group.typeOnlyNamedImport && allNamed) {
      group.typeOnlyNamedImport = node;
    }
  } else if (!group.valueOnlyNamedImport && specs.size() > 0 && allNamed) {
    group.valueOnlyNamedImport = node;
    group.valueImport = node;
  } else if (!group.valueImport && hasDefault) {
    group.valueImport = node;
  }

  ValueImport report;
  report.node = node;
  for (Node *spec : specs) {
    if (spec->kind == NodeKind::ImportSpecifier &&
        ast::ImportSpecifier(spec).importKind() == ImportKind::Type)
    {
      report.inlineTypeSpecifiers.append(spec);
      continue;
    }
    Node *local = localOf(spec);
    Declaration *variable = local ? ctx.bindings().declarationOf(local) : nullptr;
    if (!variable) {
      continue;
    }
    if (variable->references.isEmpty()) {
      report.unusedSpecifiers.append(spec);
      continue;
    }
    // JSX compiles to calls on the `React` import.
    bool onlyTypes = !(state.hasJsx && local->text == "React");
    for (Reference *ref : variable->references) {
      onlyTypes = onlyTypes && isTypeReference(ref, decl.importKind());
    }
    if (onlyTypes) {
      report.typeSpecifiers.append(spec);
    } else {
      report.valueSpecifiers.append(spec);
    }
  }
  if (decl.importKind() == ImportKind::Value && !report.typeSpecifiers.isEmpty()) {
    source.reports.append(std::move(report));
  }
}

void checkProgram(RuleContext &ctx, State &state)
{
  FixStyle style = state.options.fixStyle;
  for (SourceImports &source : state.sources) {
    for (ValueImport &report : source.reports) {
      bool whole = report.valueSpecifiers.isEmpty() && report.unusedSpecifiers.isEmpty();
      if (whole && hasAttributes(report.node)) {
        continue;
      }
      Report r;
      r.node = report.node;
      r.messageId = whole ? "typeOverValue" : "someImportsAreOnlyTypes";
      if (!whole) {
        r.data.append({"typeImports", wordList(state, report.typeSpecifiers)});
      }
      // The state is gone by the time fixes run, so the fix owns copies.
      ValueImport copy = report;
      Group group = source.group;
      r.fix = [copy, group, style](ast::Fixer &fixer) {
        toTypeImports(fixer, copy, group, style);
      };
      ctx.report(std::move(r));
    }
  }
}

void create(RuleContext &ctx)
{
  State *state = ctx.state<State>();
  readOptions(ctx, state->options);

  if (state->options.disallowTypeAnnotations) {
    ctx.on(NodeKind::TSImportType, [&ctx](Node *node) {
      Report r;
      r.node = node;
      r.messageId = "noImportTypeAnnotations";
      ctx.report(std::move(r));
    });
  }

  if (state->options.prefer == Prefer::NoTypeImports) {
    auto avoid = [&ctx](Node *node) {
      Report r;
      r.node = node;
      r.messageId = "avoidImportType";
      r.fix = [node](ast::Fixer &fixer) {
        fixer.setData(node, 0, uint8_t(ImportKind::Value));
      };
      ctx.report(std::move(r));
    };
    ctx.on(NodeKind::ImportDeclaration, [avoid](Node *node) {
      if (ast::ImportDeclaration(node).importKind() == ImportKind::Type) {
        avoid(node);
      }
    });
    ctx.on(NodeKind::ImportSpecifier, [avoid](Node *node) {
      if (ast::ImportSpecifier(node).importKind() == ImportKind::Type) {
        avoid(node);
      }
    });
    return;
  }

  ctx.on(NodeKind::JSXElement, [state](Node *) { state->hasJsx = true; });
  ctx.on(NodeKind::JSXFragment, [state](Node *) { state->hasJsx = true; });
  // Collected on exit so the JSX flag is known by the time imports are read.
  ctx.onExit(NodeKind::Program, [&ctx, state](Node *program) {
    for (Node *statement : ast::Program(program).body()) {
      if (statement->kind == NodeKind::ImportDeclaration) {
        collect(ctx, *state, statement);
      }
    }
    checkProgram(ctx, *state);
  });
}

} // namespace

const RuleDef kConsistentTypeImports{
    {
        "consistent-type-imports",
        "Enforce consistent usage of type imports",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/"
        "consistent-type-imports.md",
        /*recommended=*/false,
        /*fixable=*/true,
        /*hasSuggestions=*/false,
        /*typeAware=*/false,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
