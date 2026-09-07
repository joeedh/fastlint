// curly: consistent braces on control statements (docs/rules/curly.md).

#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;

constexpr Message kMessages[] = {
    {"missingCurlyAfter", "Expected { after '{{name}}'."},
    {"missingCurlyAfterCondition", "Expected { after '{{name}}' condition."},
    {"unexpectedCurlyAfter", "Unnecessary { after '{{name}}'."},
    {"unexpectedCurlyAfterCondition", "Unnecessary { after '{{name}}' condition."},
};

enum class Mode : uint8_t { All, Multi, MultiLine, MultiOrNest };

struct Options {
  Mode mode = Mode::All;
  bool consistent = false;
};

/** A body's verdict: whether it has braces and whether it should. -1 means no opinion. */
struct Check {
  Node *statement;
  Node *body;
  const char *name;
  bool condition;
  bool actual;
  int expected;
};

bool isLexicalDeclaration(const Node *n)
{
  switch (n->kind) {
  case NodeKind::FunctionDeclaration:
  case NodeKind::ClassDeclaration:
    return true;
  case NodeKind::VariableDeclaration:
    return ast::VariableDeclaration(const_cast<Node *>(n)).kind() !=
           ast::VariableKind::Var;
  default:
    return false;
  }
}

/** Whether `n` ends in an `if` without `else`, which a following `else` would capture. */
bool hasUnsafeIf(const Node *n)
{
  switch (n->kind) {
  case NodeKind::IfStatement: {
    ast::IfStatement view(const_cast<Node *>(n));
    return !view.alternate() || hasUnsafeIf(view.alternate());
  }
  case NodeKind::ForStatement:
  case NodeKind::ForInStatement:
  case NodeKind::ForOfStatement:
  case NodeKind::WhileStatement:
  case NodeKind::LabeledStatement:
  case NodeKind::WithStatement:
    return hasUnsafeIf(n->children[int(n->children.size()) - 1]);
  default:
    return false;
  }
}

/** The offset of the last token of `n`, ignoring a closing semicolon. */
uint32_t lastTokenOffset(string_view source, const Node *n)
{
  uint32_t end = n->end;
  if (end > n->start && source[end - 1] == ';') {
    end--;
    if (end > n->start) {
      return lastNonSpaceBefore(source, end);
    }
  }
  return end > 0 ? end - 1 : 0;
}

struct Checker {
  RuleContext &ctx;
  const Options &options;
  string_view source;

  bool isCollapsedOneLiner(const Node *body) const
  {
    uint32_t before = lastNonSpaceBefore(source, body->start);
    return lineOf(ctx, before) == lineOf(ctx, lastTokenOffset(source, body));
  }

  bool isOneLiner(const Node *n) const
  {
    if (n->kind == NodeKind::EmptyStatement) {
      return true;
    }
    return lineOf(ctx, n->start) == lineOf(ctx, lastTokenOffset(source, n));
  }

  bool bracesNecessary(Node *statement, Node *block) const
  {
    Node *first = ast::BlockStatement(block).body()[0];
    if (isLexicalDeclaration(first)) {
      return true;
    }
    bool followedByElse = statement->kind == NodeKind::IfStatement &&
                          ast::IfStatement(statement).consequent() == block &&
                          ast::IfStatement(statement).alternate() != nullptr;
    return followedByElse && hasUnsafeIf(first);
  }

  Check prepare(Node *statement, Node *body, const char *name, bool condition) const
  {
    bool hasBlock = body->kind == NodeKind::BlockStatement;
    int expected = -1;
    if (hasBlock && (ast::BlockStatement(body).body().size() != 1 ||
                     bracesNecessary(statement, body)))
    {
      expected = 1;
    } else if (options.mode == Mode::Multi) {
      expected = 0;
    } else if (options.mode == Mode::MultiLine) {
      if (!isCollapsedOneLiner(body)) {
        expected = 1;
      }
    } else if (options.mode == Mode::MultiOrNest) {
      if (hasBlock) {
        Node *inner = ast::BlockStatement(body).body()[0];
        bool comments =
            hasCommentBetween(*ctx.file().grammar(), body->start, inner->start);
        expected = (!isOneLiner(inner) || comments) ? 1 : 0;
      } else {
        expected = isOneLiner(body) ? 0 : 1;
      }
    } else {
      expected = 1;
    }
    return Check{statement, body, name, condition, hasBlock, expected};
  }

  void run(const Check &check) const
  {
    if (check.expected < 0 || bool(check.expected) == check.actual) {
      return;
    }
    Node *statement = check.statement;
    Node *body = check.body;
    Report r;
    r.node = body;
    r.at(body->start, body->end);
    r.data.append({"name", check.name});
    if (check.expected) {
      r.messageId = check.condition ? "missingCurlyAfterCondition" : "missingCurlyAfter";
      r.fix = [statement, body](ast::Fixer &fixer) {
        int slot = -1;
        for (int i = 0; i < int(statement->children.size()); i++) {
          if (statement->children[i] == body) {
            slot = i;
          }
        }
        if (slot < 0) {
          return;
        }
        fixer.detach(body);
        fixer.set(statement, slot, fixer.block({body}));
      };
    } else {
      r.messageId =
          check.condition ? "unexpectedCurlyAfterCondition" : "unexpectedCurlyAfter";
      Node *inner = ast::BlockStatement(body).body()[0];
      // Without a closing semicolon the statement could join what follows the
      // block (or the `while` of a do-while), so that case stays as it is.
      string_view innerText = ctx.textOf(inner);
      bool closed =
          !innerText.empty() && (innerText.back() == ';' || innerText.back() == '}');
      if (closed) {
        r.fix = [statement, body, inner](ast::Fixer &fixer) {
          int slot = -1;
          for (int i = 0; i < int(statement->children.size()); i++) {
            if (statement->children[i] == body) {
              slot = i;
            }
          }
          if (slot < 0) {
            return;
          }
          fixer.detach(inner);
          fixer.set(statement, slot, inner);
        };
      }
    }
    ctx.report(std::move(r));
  }
};

void create(RuleContext &ctx)
{
  Options *options = ctx.state<Options>();
  const JsonValue *mode = ctx.option(0);
  string_view modeText = mode ? mode->asString() : string_view("all");
  if (modeText == "multi") {
    options->mode = Mode::Multi;
  } else if (modeText == "multi-line") {
    options->mode = Mode::MultiLine;
  } else if (modeText == "multi-or-nest") {
    options->mode = Mode::MultiOrNest;
  }
  const JsonValue *second = ctx.option(1);
  options->consistent = second && second->asString() == "consistent";

  ctx.on(NodeKind::IfStatement, [&ctx, options](Node *node) {
    Node *parent = node->parent;
    if (parent && parent->kind == NodeKind::IfStatement &&
        ast::IfStatement(parent).alternate() == node)
    {
      return;
    }
    Checker checker{ctx, *options, ctx.source()};
    Vector<Check, 4> checks;
    for (Node *current = node; current;) {
      ast::IfStatement view(current);
      checks.append(checker.prepare(current, view.consequent(), "if", true));
      Node *alternate = view.alternate();
      if (alternate && alternate->kind != NodeKind::IfStatement) {
        checks.append(checker.prepare(current, alternate, "else", false));
        break;
      }
      current = alternate;
    }
    if (options->consistent) {
      // One branch needing braces means every branch gets them.
      bool expected = false;
      for (const Check &c : checks) {
        expected = expected || (c.expected >= 0 ? c.expected == 1 : c.actual);
      }
      for (Check &c : checks) {
        c.expected = expected ? 1 : 0;
      }
    }
    for (const Check &c : checks) {
      checker.run(c);
    }
  });
  ctx.on(NodeKind::WhileStatement, [&ctx, options](Node *node) {
    Checker checker{ctx, *options, ctx.source()};
    checker.run(checker.prepare(node, ast::WhileStatement(node).body(), "while", true));
  });
  ctx.on(NodeKind::DoWhileStatement, [&ctx, options](Node *node) {
    Checker checker{ctx, *options, ctx.source()};
    checker.run(checker.prepare(node, ast::DoWhileStatement(node).body(), "do", false));
  });
  ctx.on(NodeKind::ForStatement, [&ctx, options](Node *node) {
    Checker checker{ctx, *options, ctx.source()};
    checker.run(checker.prepare(node, ast::ForStatement(node).body(), "for", true));
  });
  ctx.on(NodeKind::ForInStatement, [&ctx, options](Node *node) {
    Checker checker{ctx, *options, ctx.source()};
    checker.run(checker.prepare(node, ast::ForInStatement(node).body(), "for-in", false));
  });
  ctx.on(NodeKind::ForOfStatement, [&ctx, options](Node *node) {
    Checker checker{ctx, *options, ctx.source()};
    checker.run(checker.prepare(node, ast::ForOfStatement(node).body(), "for-of", false));
  });
}

} // namespace

const RuleDef kCurly{
    {
        "curly",
        "Enforce consistent brace style for all control statements",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/curly.md",
        /*recommended=*/false,
        /*fixable=*/true,
        /*hasSuggestions=*/false,
        /*typeAware=*/false,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
