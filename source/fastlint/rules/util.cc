#include "fastlint/rules/util.h"

#include "fastlint/ast/binder.h"

namespace fastlint::rules {

using namespace ast;

bool listExits(span<Node *> list)
{
  for (Node *stmt : list) {
    if (stmt && alwaysExits(stmt)) {
      return true;
    }
  }
  return false;
}

bool alwaysExits(const Node *stmt)
{
  if (!stmt) {
    return false;
  }
  switch (stmt->kind) {
  case NodeKind::ReturnStatement:
  case NodeKind::ThrowStatement:
  case NodeKind::BreakStatement:
  case NodeKind::ContinueStatement:
    return true;
  case NodeKind::BlockStatement:
    return listExits(BlockStatement(const_cast<Node *>(stmt)).body());
  case NodeKind::IfStatement: {
    IfStatement view(const_cast<Node *>(stmt));
    return view.alternate() && alwaysExits(view.consequent()) &&
           alwaysExits(view.alternate());
  }
  case NodeKind::TryStatement: {
    TryStatement view(const_cast<Node *>(stmt));
    if (view.finalizer() && alwaysExits(view.finalizer())) {
      return true;
    }
    if (!alwaysExits(view.block())) {
      return false;
    }
    if (!view.handler()) {
      return true;
    }
    return alwaysExits(CatchClause(view.handler()).body());
  }
  case NodeKind::DoWhileStatement: {
    // The body runs once; if it exits without a jump, the loop never finishes.
    const Node *body = DoWhileStatement(const_cast<Node *>(stmt)).body();
    return alwaysExits(body) && !containsJump(body, true);
  }
  case NodeKind::WhileStatement: {
    WhileStatement view(const_cast<Node *>(stmt));
    const Node *test = view.test();
    bool forever = test->kind == NodeKind::Literal && test->text == "true";
    return forever && !containsJump(view.body(), false);
  }
  case NodeKind::ForStatement: {
    ForStatement view(const_cast<Node *>(stmt));
    const Node *test = view.test();
    bool forever = !test || (test->kind == NodeKind::Literal && test->text == "true");
    return forever && !containsJump(view.body(), false);
  }
  default:
    return false;
  }
}

bool containsJump(const Node *node, bool orContinue)
{
  if (!node) {
    return false;
  }
  if (node->kind == NodeKind::BreakStatement ||
      (orContinue && node->kind == NodeKind::ContinueStatement))
  {
    return true;
  }
  if (FunctionLike::matches(node->kind)) {
    return false;
  }
  for (const Node *child : node->children) {
    if (containsJump(child, orContinue)) {
      return true;
    }
  }
  return false;
}

string_view staticPropertyName(const Node *key, bool computed)
{
  if (!key) {
    return {};
  }
  if (!computed) {
    if (key->kind == NodeKind::Identifier || key->kind == NodeKind::PrivateIdentifier) {
      return key->text;
    }
  }
  if (key->kind == NodeKind::Literal) {
    Literal literal(const_cast<Node *>(key));
    if (literal.literalKind() == LiteralKind::String && key->text.size() >= 2) {
      return key->text.substr(1, key->text.size() - 2);
    }
    if (literal.literalKind() == LiteralKind::Number) {
      return key->text;
    }
  }
  if (key->kind == NodeKind::TemplateLiteral) {
    span<Node *> parts = TemplateLiteral(const_cast<Node *>(key)).parts();
    if (parts.size() == 1 && parts[0]->kind == NodeKind::TemplateElement) {
      return parts[0]->text;
    }
  }
  return {};
}

string_view staticMemberName(const Node *member)
{
  MemberExpression view(const_cast<Node *>(member));
  return staticPropertyName(view.property(), view.isComputed());
}

bool isSameReference(const Node *a, const Node *b)
{
  if (!a || !b || a->kind != b->kind) {
    return false;
  }
  switch (a->kind) {
  case NodeKind::ThisExpression:
  case NodeKind::Super:
    return true;
  case NodeKind::Identifier:
  case NodeKind::PrivateIdentifier:
    return a->text == b->text;
  case NodeKind::MemberExpression: {
    MemberExpression ma(const_cast<Node *>(a));
    MemberExpression mb(const_cast<Node *>(b));
    string_view nameA = staticMemberName(a);
    if (!nameA.empty()) {
      return isSameReference(ma.object(), mb.object()) && nameA == staticMemberName(b);
    }
    return ma.isComputed() == mb.isComputed() &&
           isSameReference(ma.object(), mb.object()) &&
           isSameReference(ma.property(), mb.property());
  }
  default:
    return false;
  }
}

bool literalTruthy(const Node *literal)
{
  Literal view(const_cast<Node *>(literal));
  string_view text = literal->text;
  switch (view.literalKind()) {
  case LiteralKind::String:
    return text.size() > 2;
  case LiteralKind::Boolean:
    return text == "true";
  case LiteralKind::Null:
    return false;
  case LiteralKind::Regex:
    return true;
  case LiteralKind::Bigint:
  case LiteralKind::Number: {
    // Every zero spelling is falsy: 0, 0.0, .0, 0x0, 0n, 0e5.
    bool sawDigit = false;
    for (size_t i = 0; i < text.size(); i++) {
      char c = text[i];
      if (c == '0' || c == '.' || c == '_' || c == 'n') {
        continue;
      }
      if ((c == 'x' || c == 'X' || c == 'o' || c == 'O' || c == 'b' || c == 'B') &&
          i == 1)
      {
        continue;
      }
      if (c == 'e' || c == 'E') {
        break;
      }
      sawDigit = true;
      break;
    }
    return sawDigit;
  }
  }
  return true;
}

bool isStatementListParent(const Node *node)
{
  switch (node->kind) {
  case NodeKind::Program:
  case NodeKind::BlockStatement:
  case NodeKind::SwitchCase:
  case NodeKind::StaticBlock:
  case NodeKind::TSModuleBlock:
    return true;
  default:
    return false;
  }
}

void commentsBetween(const syntax::GrammarTree &tree,
                     uint32_t from,
                     uint32_t to,
                     litestl::util::function_ref<void(const syntax::Trivia &)> fn)
{
  span<const syntax::Trivia> trivia = tree.trivia();
  // Trivia is in source order; binary search for the first at or after `from`.
  size_t lo = 0, hi = trivia.size();
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (trivia[mid].offset < from) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  for (size_t i = lo; i < trivia.size() && trivia[i].offset < to; i++) {
    const syntax::Trivia &t = trivia[i];
    if (t.kind == syntax::Trivia::Kind::SingleLineComment ||
        t.kind == syntax::Trivia::Kind::MultiLineComment)
    {
      fn(t);
    }
  }
}

bool hasCommentBetween(const syntax::GrammarTree &tree, uint32_t from, uint32_t to)
{
  bool found = false;
  commentsBetween(tree, from, to, [&](const syntax::Trivia &) { found = true; });
  return found;
}

uint32_t lineOf(const RuleContext &ctx, uint32_t offset)
{
  return ctx.file().grammar()->lineOf(offset);
}

uint32_t columnOf(const RuleContext &ctx, uint32_t offset)
{
  const syntax::GrammarTree *tree = ctx.file().grammar();
  uint32_t line = tree->lineOf(offset);
  return offset - tree->lineStarts()[int(line) - 1] + 1;
}

uint32_t findToken(string_view source, uint32_t from, uint32_t to, string_view token)
{
  if (to > source.size()) {
    to = uint32_t(source.size());
  }
  for (uint32_t i = from; i + token.size() <= to; i++) {
    if (source.substr(i, token.size()) == token) {
      return i;
    }
  }
  return from;
}

namespace {

bool isSpace(char c)
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

} // namespace

uint32_t lastNonSpaceBefore(string_view source, uint32_t end)
{
  uint32_t i = end;
  while (i > 0 && isSpace(source[i - 1])) {
    i--;
  }
  return i == 0 ? end : i - 1;
}

uint32_t firstNonSpaceAt(string_view source, uint32_t start)
{
  uint32_t i = start;
  while (i < source.size() && isSpace(source[i])) {
    i++;
  }
  return i;
}

bool isGlobalReference(const RuleContext &ctx, const Node *id)
{
  const Reference *ref = ctx.bindings().referenceOf(id);
  return ref && ref->resolved == nullptr;
}

} // namespace fastlint::rules
