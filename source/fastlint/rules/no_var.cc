// no-var: require `let` or `const` instead of `var` (docs/rules/no-var.md).
// The fix follows ESLint's safety conditions, answered from the binder.

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Declaration;
using ast::Node;
using ast::NodeKind;
using ast::Reference;

constexpr Message kMessages[] = {
    {"unexpectedVar", "Unexpected var, use let or const instead."},
};

bool isInside(const Node *node, const Node *ancestor)
{
  for (const Node *n = node; n; n = n->parent) {
    if (n == ancestor) {
      return true;
    }
  }
  return false;
}

/** The nearest enclosing loop that no function boundary separates from `node`. */
const Node *enclosingLoop(const Node *node)
{
  for (const Node *n = node->parent; n; n = n->parent) {
    if (ast::Loop::matches(n->kind)) {
      return n;
    }
    if (ast::FunctionLike::matches(n->kind)) {
      return nullptr;
    }
  }
  return nullptr;
}

/** Whether `let` in place of this `var` keeps the program's meaning. */
bool canFix(RuleContext &ctx, Node *node)
{
  ast::VariableDeclaration declaration(node);
  Node *parent = node->parent;
  if (!parent || parent->kind == NodeKind::SwitchCase) {
    return false;
  }
  bool loopHead = (parent->kind == NodeKind::ForInStatement ||
                   parent->kind == NodeKind::ForOfStatement) &&
                  parent->children[0] == node;
  bool forInit = parent->kind == NodeKind::ForStatement && parent->children[0] == node;
  if (!loopHead && !forInit && !isStatementListParent(parent)) {
    return false;
  }
  // The scope `let` would give the names.
  const Node *scopeNode = parent;
  const Node *loop = enclosingLoop(node);
  ast::Bindings &bindings = ctx.bindings();
  bool script = isScript(ctx);

  Vector<Node *, 4> ids;
  Vector<Declaration *, 4> declared;
  for (Node *declarator : declaration.declarations()) {
    Node *id = ast::VariableDeclarator(declarator).id();
    if (id->kind == NodeKind::Identifier) {
      ids.append(id);
    } else {
      id->descendants<ast::Identifier>([&](ast::Identifier leaf) {
        if (bindings.declarationOf(leaf.node())) {
          ids.append(leaf.node());
        }
      });
    }
  }
  for (Node *id : ids) {
    Declaration *decl = bindings.declarationOf(id);
    if (!decl) {
      return false;
    }
    declared.append(decl);
    if (id->text == "let") {
      return false;
    }
    // A global `var` is a property of the global object; `let` is not.
    if (script && decl->scope->kind == ast::ScopeKind::Module) {
      return false;
    }
    // Redeclared names, or names in a catch's shadow, need the merge `var` gives.
    if (decl->nextSameName || decl->scope->lookupLocal(decl->name) != decl) {
      return false;
    }
    for (Reference *ref : decl->references) {
      if (ref->isInit()) {
        continue;
      }
      if (!isInside(ref->id, scopeNode)) {
        return false;
      }
      if (ref->id->start < node->start) {
        return false;
      }
      if (loop && ref->id->enclosingFunction() != node->enclosingFunction()) {
        return false;
      }
    }
  }
  // A name read in its own initializer would hit the temporal dead zone,
  // unless the initializer is a function that runs later.
  for (Node *declarator : declaration.declarations()) {
    ast::VariableDeclarator view(declarator);
    Node *init = view.init();
    if (!init || ast::FunctionLike::matches(init->kind)) {
      continue;
    }
    bool self = false;
    auto check = [&](Node *leaf) {
      if (leaf->kind != NodeKind::Identifier || !bindings.referenceOf(leaf)) {
        return;
      }
      for (Declaration *decl : declared) {
        if (leaf->text == decl->name && decl->node == declarator) {
          self = true;
        }
      }
    };
    check(init);
    init->descendants<ast::Identifier>([&](ast::Identifier leaf) { check(leaf.node()); });
    if (self) {
      return false;
    }
  }
  if (loop && !loopHead) {
    for (Node *declarator : declaration.declarations()) {
      if (!ast::VariableDeclarator(declarator).init()) {
        return false;
      }
    }
  }
  return true;
}

void create(RuleContext &ctx)
{
  ctx.onExit(NodeKind::VariableDeclaration, [&ctx](Node *node) {
    if (ast::VariableDeclaration(node).kind() != ast::VariableKind::Var) {
      return;
    }
    // `declare global { var x }` describes the host; nothing else can go there.
    Node *parent = node->parent;
    if (parent && parent->kind == NodeKind::TSModuleBlock && parent->parent &&
        parent->parent->kind == NodeKind::TSModuleDeclaration &&
        ast::TSModuleDeclaration(parent->parent).kind() == ast::ModuleKind::Global)
    {
      return;
    }
    Report r;
    r.node = node;
    r.messageId = "unexpectedVar";
    if (canFix(ctx, node)) {
      r.fix = [node](ast::Fixer &fixer) {
        fixer.setData(node, 0, uint8_t(ast::VariableKind::Let));
      };
    }
    ctx.report(std::move(r));
  });
}

} // namespace

const RuleDef kNoVar{
    {
        "no-var",
        "Require `let` or `const` instead of `var`",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/no-var.md",
        /*recommended=*/false,
        /*fixable=*/true,
        /*hasSuggestions=*/false,
        /*typeAware=*/false,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
