// no-fallthrough: a case that runs into the next one needs a
// `falls through` comment (docs/rules/no-fallthrough.md).

#include "fastlint/lint/regex.h"
#include "fastlint/rules/rules.h"
#include "fastlint/rules/util.h"

namespace fastlint::rules {

namespace {

using namespace lint;
using ast::Node;
using ast::NodeKind;

constexpr Message kMessages[] = {
    {"case", "Expected a 'break' statement before 'case'."},
    {"default", "Expected a 'break' statement before 'default'."},
};

struct Options {
  bool allowEmptyCase = false;
  Regex pattern;
  /** Directive comments never count as fallthrough comments. */
  Regex directive;
};

/** The last comment starting in `[from, to)`, or null. */
const syntax::Trivia *lastCommentIn(RuleContext &ctx, uint32_t from, uint32_t to)
{
  const syntax::Trivia *last = nullptr;
  commentsBetween(*ctx.file().grammar(), from, to, [&](const syntax::Trivia &comment) {
    last = &comment;
  });
  return last;
}

bool isFallthroughComment(RuleContext &ctx,
                          const Options &options,
                          const syntax::Trivia &comment)
{
  string_view text = ctx.source().substr(comment.offset, comment.length);
  string_view body = comment.kind == syntax::Trivia::Kind::SingleLineComment
                         ? text.substr(2)
                         : text.substr(2, text.size() > 4 ? text.size() - 4 : 0);
  string_view trimmed = body;
  while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
    trimmed = trimmed.substr(1);
  }
  return options.pattern.search(body) && !options.directive.search(trimmed);
}

/**
 * Whether a comment permits the fall from `current` into `next`: the last
 * comment before a sole block's closing brace, or the last comment before
 * the next clause, as ESLint reads them.
 */
bool hasFallthroughComment(RuleContext &ctx,
                           const Options &options,
                           Node *current,
                           Node *next)
{
  span<Node *> body = ast::SwitchCase(current).consequent();
  if (body.size() == 1 && body[0]->kind == NodeKind::BlockStatement) {
    Node *block = body[0];
    span<Node *> inner = ast::BlockStatement(block).body();
    uint32_t from = inner.size() > 0 ? inner[inner.size() - 1]->end : block->start + 1;
    if (const syntax::Trivia *comment = lastCommentIn(ctx, from, block->end)) {
      if (isFallthroughComment(ctx, options, *comment)) {
        return true;
      }
    }
  }
  const syntax::Trivia *comment = lastCommentIn(ctx, current->end, next->start);
  return comment && isFallthroughComment(ctx, options, *comment);
}

void create(RuleContext &ctx)
{
  Options *options = ctx.state<Options>();
  string_view pattern = "falls?\\s?through";
  string_view flags = "i";
  if (const JsonValue *given = ctx.option(0)) {
    options->allowEmptyCase = given->getBool("allowEmptyCase", false);
    string_view custom = given->getString("commentPattern");
    if (!custom.empty()) {
      pattern = custom;
      flags = "";
    }
  }
  if (!options->pattern.compile(pattern, flags)) {
    // An unsupported custom pattern falls back to the default.
    options->pattern.compile("falls?\\s?through", "i");
  }
  options->directive.compile("^(eslint(-env|-enable|-disable(-line|-next-line)?)?|"
                             "fastlint(-enable|-disable(-line|-next-"
                             "line)?)?|exported|globals?)(\\s|$)");

  ctx.on(NodeKind::SwitchStatement, [&ctx, options](Node *node) {
    span<Node *> cases = ast::SwitchStatement(node).cases();
    for (size_t i = 0; i + 1 < cases.size(); i++) {
      Node *current = cases[i];
      Node *next = cases[i + 1];
      span<Node *> body = ast::SwitchCase(current).consequent();
      bool fallsThrough;
      if (body.size() == 0) {
        // An empty case is a deliberate grouping unless a blank line separates them.
        fallsThrough = !options->allowEmptyCase &&
                       lineOf(ctx, next->start) > lineOf(ctx, current->end) + 1;
      } else {
        fallsThrough = !listExits(body);
      }
      if (fallsThrough && !hasFallthroughComment(ctx, *options, current, next)) {
        ctx.report(next, ast::SwitchCase(next).test() ? "case" : "default");
      }
    }
  });
}

} // namespace

const RuleDef kNoFallthrough{
    {
        "no-fallthrough",
        "Disallow fallthrough of `case` statements",
        "https://github.com/joeedh/fastlint/blob/master/docs/rules/no-fallthrough.md",
        /*recommended=*/true,
        /*fixable=*/false,
        /*hasSuggestions=*/false,
        /*typeAware=*/false,
        messagesOf(kMessages),
    },
    create,
};

} // namespace fastlint::rules
