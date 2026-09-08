// prefer-const: require `const` for `let` names never reassigned
// (docs/rules/prefer-const.md). A port of ESLint's rule over the binder.

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"
#include "util/map.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Declaration;
using ast::Node;
using ast::NodeKind;
using ast::Reference;
using litestl::util::Map;

constexpr Message kMessages[] = {
    {"useConst", "'{{name}}' is never reassigned. Use 'const' instead."},
};

bool isPatternPart(NodeKind kind)
{
  return kind == NodeKind::ObjectPattern || kind == NodeKind::ArrayPattern ||
         kind == NodeKind::RestElement || kind == NodeKind::AssignmentPattern ||
         kind == NodeKind::Property;
}

/** The node above the patterns that hold `id`. */
Node *patternHost(Node *id)
{
  Node *node = id->parent;
  while (node && isPatternPart(node->kind)) {
    node = node->parent;
  }
  return node;
}

/** The declarator or assignment a write reference sits in, or null. */
Node *destructuringHost(const Reference *ref)
{
  if (!ref->isWrite()) {
    return nullptr;
  }
  Node *host = patternHost(ref->id);
  if (!host || (host->kind != NodeKind::VariableDeclarator &&
                host->kind != NodeKind::AssignmentExpression))
  {
    return nullptr;
  }
  return host;
}

/** Whether the write at `id` could be turned into a `const` declaration. */
bool canBecomeDeclaration(Node *id)
{
  Node *host = patternHost(id);
  if (!host) {
    return false;
  }
  if (host->kind == NodeKind::VariableDeclarator) {
    return true;
  }
  return host->kind == NodeKind::AssignmentExpression && host->parent &&
         host->parent->kind == NodeKind::ExpressionStatement &&
         isStatementListParent(host->parent->parent);
}

/** Whether `name` in a destructuring in `scope` resolves outside it or to a parameter. */
bool isOuterVariable(string_view name, ast::Scope *scope)
{
  Declaration *decl = scope->lookup(name, ast::Space::Value);
  return decl && (decl->scope != scope || decl->kind == ast::DeclKind::Parameter);
}

bool hasMemberAssignment(const Node *node)
{
  if (!node) {
    return false;
  }
  switch (node->kind) {
  case NodeKind::ObjectPattern:
    for (Node *prop : ast::ObjectPattern(const_cast<Node *>(node)).properties()) {
      Node *target = prop->kind == NodeKind::Property ? ast::Property(prop).value()
                                                      : ast::RestElement(prop).argument();
      if (hasMemberAssignment(target)) {
        return true;
      }
    }
    return false;
  case NodeKind::ArrayPattern:
    for (Node *e : ast::ArrayPattern(const_cast<Node *>(node)).elements()) {
      if (hasMemberAssignment(e)) {
        return true;
      }
    }
    return false;
  case NodeKind::AssignmentPattern:
    return hasMemberAssignment(node->children[0]);
  case NodeKind::MemberExpression:
    return true;
  default:
    return false;
  }
}

/** Whether a destructuring assignment to `left` mixes in names from outside `scope`. */
bool hasOuterNames(Node *left, ast::Scope *scope)
{
  if (left->kind == NodeKind::ObjectPattern) {
    for (Node *prop : ast::ObjectPattern(left).properties()) {
      if (prop->kind != NodeKind::Property) {
        continue;
      }
      Node *value = ast::Property(prop).value();
      if (value && value->kind == NodeKind::Identifier &&
          isOuterVariable(value->text, scope))
      {
        return true;
      }
    }
  } else if (left->kind == NodeKind::ArrayPattern) {
    for (Node *e : ast::ArrayPattern(left).elements()) {
      if (e && e->kind == NodeKind::Identifier && isOuterVariable(e->text, scope)) {
        return true;
      }
    }
  }
  return false;
}

/**
 * The identifier to report when `decl` could be `const`: the single write
 * when it is a declaration-shaped assignment in the variable's own scope,
 * or the declared name when a read precedes it. Null otherwise.
 */
Node *identifierIfConst(Declaration *decl, bool ignoreReadBeforeAssign)
{
  Reference *writer = nullptr;
  bool readBeforeInit = false;
  for (Reference *ref : decl->references) {
    if (ref->isWrite()) {
      if (writer && writer->id != ref->id) {
        return nullptr;
      }
      Node *host = destructuringHost(ref);
      if (host && host->kind == NodeKind::AssignmentExpression) {
        Node *left = ast::AssignmentExpression(host).left();
        if (hasOuterNames(left, decl->scope) || hasMemberAssignment(left)) {
          return nullptr;
        }
      }
      writer = ref;
    } else if (ref->isRead() && !writer) {
      if (ignoreReadBeforeAssign) {
        return nullptr;
      }
      readBeforeInit = true;
    }
  }
  if (!writer || writer->scope != decl->scope || !canBecomeDeclaration(writer->id)) {
    return nullptr;
  }
  return readBeforeInit ? decl->id : writer->id;
}

/** The `let` declaration whose declarator pattern holds `id`, or null. */
Node *declarationOf(Node *id)
{
  Node *host = patternHost(id);
  if (!host || host->kind != NodeKind::VariableDeclarator || !host->parent ||
      host->parent->kind != NodeKind::VariableDeclaration)
  {
    return nullptr;
  }
  return host->parent;
}

/** The names one destructuring host writes, with each name's report target. */
struct Group {
  Node *host;
  Vector<Node *, 4> ids;
};

struct State {
  /** The names every `let` declaration binds, in source order. */
  Vector<Declaration *> variables;
};

/** The bound identifiers of a declarator's pattern, in source order. */
void collectNames(ast::Bindings &bindings, Node *pattern, Vector<Node *, 4> &out)
{
  if (!pattern) {
    return;
  }
  if (pattern->kind == NodeKind::Identifier) {
    out.append(pattern);
    return;
  }
  pattern->descendants<ast::Identifier>([&](ast::Identifier id) {
    if (bindings.declarationOf(id.node())) {
      out.append(id.node());
    }
  });
}

int countNames(ast::Bindings &bindings, Node *declaration)
{
  int count = 0;
  for (Node *declarator : ast::VariableDeclaration(declaration).declarations()) {
    Vector<Node *, 4> names;
    collectNames(bindings, ast::VariableDeclarator(declarator).id(), names);
    count += int(names.size());
  }
  return count;
}

void checkProgram(RuleContext &ctx, State &state)
{
  const JsonValue *opt = ctx.option();
  bool matchAny = !opt || opt->getString("destructuring") != "all";
  bool ignoreReadBeforeAssign = opt && opt->getBool("ignoreReadBeforeAssign", false);

  Vector<Group> groups;
  Map<const Node *, int> groupIndex;
  for (Declaration *decl : state.variables) {
    Node *target = identifierIfConst(decl, ignoreReadBeforeAssign);
    Node *prev = nullptr;
    for (Reference *ref : decl->references) {
      if (ref->id == prev) {
        continue;
      }
      prev = ref->id;
      Node *host = destructuringHost(ref);
      if (!host) {
        continue;
      }
      int *index = groupIndex.lookup_ptr(host);
      if (!index) {
        groupIndex.add(static_cast<const Node *>(host), int(groups.size()));
        groups.append(Group{host, {}});
        index = groupIndex.lookup_ptr(host);
      }
      groups[*index].ids.append(target);
    }
  }

  // Which groups report, and how many of each declaration's names they cover.
  Vector<int> reports;
  Map<const Node *, int> reportable;
  for (Group &g : groups) {
    int nonNull = 0;
    for (Node *id : g.ids) {
      nonNull += id != nullptr;
    }
    bool report = nonNull > 0 && (matchAny || nonNull == int(g.ids.size()));
    reports.append(report);
    if (!report) {
      continue;
    }
    for (Node *id : g.ids) {
      Node *declaration = id ? declarationOf(id) : nullptr;
      if (!declaration) {
        continue;
      }
      if (int *count = reportable.lookup_ptr(declaration)) {
        (*count)++;
      } else {
        reportable.add(static_cast<const Node *>(declaration), 1);
      }
    }
  }

  for (int i = 0; i < int(groups.size()); i++) {
    if (!reports[i]) {
      continue;
    }
    Group &g = groups[i];
    Node *declaration = g.ids[0] ? declarationOf(g.ids[0]) : nullptr;
    bool fix = declaration != nullptr;
    if (fix) {
      ast::VariableDeclaration view(declaration);
      Node *parent = declaration->parent;
      bool loopHead = parent && (parent->kind == NodeKind::ForInStatement ||
                                 parent->kind == NodeKind::ForOfStatement);
      if (!loopHead) {
        for (Node *declarator : view.declarations()) {
          if (!ast::VariableDeclarator(declarator).init()) {
            fix = false;
          }
        }
      }
      for (Node *id : g.ids) {
        if (!id) {
          fix = false;
        }
      }
      // A declaration with several declarators only changes when every name
      // it binds is reported.
      if (fix && view.declarations().size() != 1) {
        int *count = reportable.lookup_ptr(declaration);
        fix = count && *count == countNames(ctx.bindings(), declaration);
      }
    }
    for (Node *id : g.ids) {
      if (!id) {
        continue;
      }
      Report r;
      r.node = id;
      r.messageId = "useConst";
      r.data.append(Placeholder{"name", id->text});
      if (fix) {
        r.fix = [declaration](ast::Fixer &fixer) {
          fixer.setData(declaration, 0, uint8_t(ast::VariableKind::Const));
        };
      }
      ctx.report(std::move(r));
    }
  }
}

void create(RuleContext &ctx)
{
  State *state = ctx.state<State>();
  ctx.on(NodeKind::VariableDeclaration, [&ctx, state](Node *node) {
    if (ast::VariableDeclaration(node).kind() != ast::VariableKind::Let) {
      return;
    }
    Node *parent = node->parent;
    if (parent && parent->kind == NodeKind::ForStatement && parent->children[0] == node) {
      return;
    }
    for (Node *declarator : ast::VariableDeclaration(node).declarations()) {
      Vector<Node *, 4> names;
      collectNames(ctx.bindings(), ast::VariableDeclarator(declarator).id(), names);
      for (Node *name : names) {
        state->variables.append(ctx.bindings().declarationOf(name));
      }
    }
  });
  ctx.onExit(NodeKind::Program, [&ctx, state](Node *) { checkProgram(ctx, *state); });
}

const char kSchema[] =
    R"([{"type":"object","properties":{"destructuring":{"enum":["any","all"]},"ignoreReadBeforeAssign":{"type":"boolean"}},"additionalProperties":false}])";

} // namespace

const RuleDef kPreferConst{
    {
        "prefer-const",
        "Require `const` declarations for variables that are never reassigned after "
        "declared",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/prefer-const.md",
        /*recommended=*/false,
        /*fixable=*/true,
        /*hasSuggestions=*/false,
        /*typeAware=*/false,
        messagesOf(kMessages),
        kSchema,
    },
    create,
};

} // namespace fastlint::rules
