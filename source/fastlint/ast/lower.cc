#include "fastlint/ast/lower.h"

#include "fastlint/ast/generated/kinds.h"
#include "fastlint/syntax/tokens.h"
#include "util/vector.h"

namespace fastlint::ast {

namespace {

using syntax::GrammarTree;
using syntax::NodeId;
using syntax::TokenKind;
using GK = syntax::NodeKind;
using litestl::util::Vector;

constexpr NodeId kNo = syntax::kNoNode;

bool isTypeKind(GK kind)
{
  switch (kind) {
  case GK::KeywordType:
  case GK::TypeReference:
  case GK::UnionType:
  case GK::IntersectionType:
  case GK::ParenthesizedType:
  case GK::FunctionType:
  case GK::ConstructorType:
  case GK::ArrayType:
  case GK::TupleType:
  case GK::NamedTupleMember:
  case GK::OptionalType:
  case GK::RestType:
  case GK::TypeOperator:
  case GK::InferType:
  case GK::IndexedAccessType:
  case GK::ConditionalType:
  case GK::MappedType:
  case GK::LiteralType:
  case GK::TypeQuery:
  case GK::TypePredicate:
  case GK::ThisType:
  case GK::TypeLiteral:
  case GK::ImportType:
  case GK::TemplateLiteralType:
    return true;
  default:
    return false;
  }
}

bool isDeclarationKind(GK kind)
{
  switch (kind) {
  case GK::FunctionDeclaration:
  case GK::ClassDeclaration:
  case GK::InterfaceDeclaration:
  case GK::TypeAliasDeclaration:
  case GK::EnumDeclaration:
  case GK::ModuleDeclaration:
  case GK::VariableStatement:
    return true;
  default:
    return false;
  }
}

bool isModuleKind(GK kind)
{
  switch (kind) {
  case GK::ImportDeclaration:
  case GK::ImportEqualsDeclaration:
  case GK::ExportDeclaration:
  case GK::ExportAssignment:
  case GK::NamespaceExportDeclaration:
    return true;
  default:
    return false;
  }
}

class Lowerer {
public:
  Lowerer(const GrammarTree &tree, AstFile &file) : t(tree), f(file)
  {
  }

  Node *run()
  {
    NodeId root = t.root();
    Node *program = mk(NodeKind::Program, root);
    bool module = false;
    for (NodeId id : kids(root)) {
      if (isModuleKind(gk(id)) || gflag(id, syntax::FLAG_EXPORTED)) {
        module = true;
      }
    }
    lowerStatementList(program, kids(root), true);
    program->setDataByte(0, uint8_t(module ? SourceType::Module : SourceType::Script));
    f.setRoot(program);
    return finish(program);
  }

private:
  const GrammarTree &t;
  AstFile &f;

  // ------------------------------------------------------------ grammar access

  const syntax::Node &g(NodeId id) const
  {
    return t.nodes()[id];
  }
  GK gk(NodeId id) const
  {
    return g(id).kind;
  }
  span<const NodeId> kids(NodeId id) const
  {
    return t.children(id);
  }
  bool gflag(NodeId id, syntax::NodeFlags flag) const
  {
    return syntax::hasFlags(g(id).flags, flag);
  }
  bool isMissing(NodeId id) const
  {
    return id == kNo || gflag(id, syntax::FLAG_MISSING);
  }
  uint32_t endToken(NodeId id) const
  {
    return g(id).firstToken + g(id).tokenCount;
  }
  TokenKind tokenKind(uint32_t index) const
  {
    return t.tokenAt(index).kind;
  }
  string_view tokenText(uint32_t index) const
  {
    return t.tokenText(t.tokenAt(index));
  }
  /** Source text of the node's whole token range. */
  string_view nodeText(NodeId id) const
  {
    const syntax::Node &n = g(id);
    if (n.tokenCount == 0) {
      return {};
    }
    const syntax::Token &first = t.tokenAt(n.firstToken);
    const syntax::Token &last = t.tokenAt(n.firstToken + n.tokenCount - 1);
    return t.sourceFrom(first.offset, last.offset + last.length - first.offset);
  }

  /** Calls `fn(tokenIndex)` for each token of the node no child owns, in order, until
   * it returns false. */
  template <typename F> void forOwnTokens(NodeId id, F &&fn) const
  {
    const syntax::Node &n = g(id);
    span<const NodeId> ch = kids(id);
    size_t ci = 0;
    uint32_t i = n.firstToken;
    uint32_t end = n.firstToken + n.tokenCount;
    while (i < end) {
      bool skipped = false;
      while (ci < ch.size()) {
        const syntax::Node &c = g(ch[ci]);
        uint32_t cEnd = c.firstToken + c.tokenCount;
        if (c.tokenCount == 0 || cEnd <= i) {
          ci++;
          continue;
        }
        if (c.firstToken <= i) {
          i = cEnd;
          ci++;
          skipped = true;
        }
        break;
      }
      if (skipped) {
        continue;
      }
      if (!fn(i)) {
        return;
      }
      i++;
    }
  }

  uint32_t findOwnToken(NodeId id, TokenKind kind) const
  {
    uint32_t found = kNoToken;
    forOwnTokens(id, [&](uint32_t i) {
      if (tokenKind(i) == kind) {
        found = i;
        return false;
      }
      return true;
    });
    return found;
  }
  bool hasOwnToken(NodeId id, TokenKind kind) const
  {
    return findOwnToken(id, kind) != kNoToken;
  }
  /** Kind of the first token no child owns, or EndOfFile when there is none. */
  TokenKind firstOwnTokenKind(NodeId id) const
  {
    TokenKind kind = TokenKind::EndOfFile;
    forOwnTokens(id, [&](uint32_t i) {
      kind = tokenKind(i);
      return false;
    });
    return kind;
  }

  // ----------------------------------------------------------------- building

  Node *mk(NodeKind kind, NodeId link)
  {
    return f.make(kind, GrammarRef{&t, link});
  }
  /** A node standing for the tokens `[first, end)` of grammar node `link`. */
  Node *mkRange(NodeKind kind, NodeId link, uint32_t first, uint32_t end)
  {
    GrammarRef ref{&t, link, first, end > first ? end - first : 0};
    return f.make(kind, ref);
  }
  /** A node standing for the grammar node's tokens from `from` through `to`. */
  Node *mkSpanning(NodeKind kind, NodeId link, NodeId from, NodeId to)
  {
    return mkRange(kind, link, g(from).firstToken, endToken(to));
  }
  Node *leaf(NodeKind kind, NodeId link)
  {
    return finish(mk(kind, link));
  }

  /** Appends a slot that must be filled; a null child marks the parent incomplete. */
  void req(Node *parent, Node *child)
  {
    parent->appendChild(child);
    if (!child) {
      parent->setFlag(Flag::Incomplete);
    }
  }
  void opt(Node *parent, Node *child)
  {
    parent->appendChild(child);
  }
  /** Appends a list element; a missing one is dropped and the parent marked. */
  void elem(Node *parent, Node *child)
  {
    if (child) {
      parent->appendChild(child);
    } else {
      parent->setFlag(Flag::Incomplete);
    }
  }

  /** Sets the span from the node's own tokens and its children. */
  Node *finish(Node *n)
  {
    bool has = false;
    uint32_t start = 0;
    uint32_t end = 0;
    if (n->grammar) {
      uint32_t first = n->grammar.firstToken;
      uint32_t count = n->grammar.tokenCount;
      if (first == kNoToken) {
        first = g(n->grammar.id).firstToken;
        count = g(n->grammar.id).tokenCount;
      }
      if (count > 0) {
        const syntax::Token &a = t.tokenAt(first);
        const syntax::Token &b = t.tokenAt(first + count - 1);
        start = a.offset;
        end = b.offset + b.length;
        has = true;
      } else if (first < t.tokens().size()) {
        start = end = t.tokenAt(first).offset;
      }
    }
    for (Node *c : n->children) {
      if (!c || c->end <= c->start) {
        continue;
      }
      if (!has) {
        start = c->start;
        end = c->end;
        has = true;
      } else {
        start = c->start < start ? c->start : start;
        end = c->end > end ? c->end : end;
      }
    }
    n->start = start;
    n->end = end;
    return n;
  }

  /** Points `n` at `link` instead, keeping its children; recomputes the span. */
  Node *relink(Node *n, NodeId link)
  {
    n->grammar = GrammarRef{&t, link};
    return finish(n);
  }

  Node *errorLeaf(NodeId id)
  {
    return leaf(NodeKind::Error, id);
  }

  Node *identifier(NodeId id)
  {
    Node *n = mk(NodeKind::Identifier, id);
    n->text = nodeText(id);
    opt(n, nullptr);
    return finish(n);
  }

  Node *privateIdentifier(NodeId id)
  {
    Node *n = mk(NodeKind::PrivateIdentifier, id);
    string_view text = nodeText(id);
    n->text = text.size() > 0 && text[0] == '#' ? text.substr(1) : text;
    return finish(n);
  }

  Node *literal(NodeId id, LiteralKind kind)
  {
    Node *n = mk(NodeKind::Literal, id);
    n->text = nodeText(id);
    n->setDataByte(0, uint8_t(kind));
    return finish(n);
  }

  /** A TemplateElement for one template token; strips the delimiters from `text`. */
  Node *templateElement(NodeId link, uint32_t token, bool tail)
  {
    Node *n = mkRange(NodeKind::TemplateElement, link, token, token + 1);
    string_view raw = tokenText(token);
    size_t lead = raw.size() > 0 && (raw[0] == '`' || raw[0] == '}') ? 1 : 0;
    size_t trail = 0;
    if (raw.size() >= lead + 2 && raw.substr(raw.size() - 2) == "${") {
      trail = 2;
    } else if (raw.size() >= lead + 1 && raw.back() == '`') {
      trail = 1;
    }
    n->text = raw.substr(lead, raw.size() - lead - trail);
    if (tail) {
      n->setFlag(Flag::Tail);
    }
    return finish(n);
  }

  // ------------------------------------------------------------------ program

  void lowerStatementList(Node *parent, span<const NodeId> ids, bool prologue)
  {
    bool inPrologue = prologue;
    for (NodeId id : ids) {
      Node *s = lowerStatement(id);
      if (!s) {
        parent->setFlag(Flag::Incomplete);
        continue;
      }
      if (inPrologue) {
        Node *expr = s->kind == NodeKind::ExpressionStatement ? s->children[0] : nullptr;
        bool directive = expr && expr->kind == NodeKind::Literal &&
                         LiteralKind(expr->dataByte(0)) == LiteralKind::String &&
                         !expr->hasFlag(Flag::Parenthesized);
        if (directive) {
          s->setFlag(Flag::Directive);
        } else {
          inPrologue = false;
        }
      }
      parent->appendChild(s);
    }
  }

  // --------------------------------------------------------------- statements

  Node *lowerStatement(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    span<const NodeId> ch = kids(id);
    switch (gk(id)) {
    case GK::Block: {
      Node *n = mk(NodeKind::BlockStatement, id);
      lowerStatementList(n, ch, false);
      return finish(n);
    }
    case GK::EmptyStatement:
      return leaf(NodeKind::EmptyStatement, id);
    case GK::DebuggerStatement:
      return leaf(NodeKind::DebuggerStatement, id);
    case GK::VariableStatement:
      return ch.size() > 0 ? lowerVariableList(ch[0], id) : errorLeaf(id);
    case GK::FunctionDeclaration:
      return lowerFunction(id, NodeKind::FunctionDeclaration);
    case GK::ClassDeclaration:
      return lowerClass(id, NodeKind::ClassDeclaration);
    case GK::InterfaceDeclaration:
      return lowerInterface(id);
    case GK::TypeAliasDeclaration: {
      Node *n = mk(NodeKind::TSTypeAliasDeclaration, id);
      size_t i = 0;
      req(n,
          i < ch.size() && gk(ch[i]) == GK::Identifier ? identifier(ch[i++]) : nullptr);
      opt(n,
          i < ch.size() && gk(ch[i]) == GK::TypeParameters ? lowerTypeParameters(ch[i++])
                                                           : nullptr);
      req(n, i < ch.size() ? lowerType(ch[i]) : nullptr);
      if (hasOwnToken(id, TokenKind::DeclareKeyword)) {
        n->setFlag(Flag::Declare);
      }
      return finish(n);
    }
    case GK::EnumDeclaration:
      return lowerEnum(id);
    case GK::ModuleDeclaration:
      return lowerModule(id);
    case GK::ExpressionStatement: {
      Node *n = mk(NodeKind::ExpressionStatement, id);
      req(n, ch.size() > 0 ? lowerExpression(ch[0]) : nullptr);
      return finish(n);
    }
    case GK::IfStatement: {
      Node *n = mk(NodeKind::IfStatement, id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      req(n, child(ch, 1, &Lowerer::lowerStatement));
      opt(n, child(ch, 2, &Lowerer::lowerStatement));
      return finish(n);
    }
    case GK::DoStatement: {
      Node *n = mk(NodeKind::DoWhileStatement, id);
      req(n, child(ch, 0, &Lowerer::lowerStatement));
      req(n, child(ch, 1, &Lowerer::lowerExpression));
      return finish(n);
    }
    case GK::WhileStatement: {
      Node *n = mk(NodeKind::WhileStatement, id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      req(n, child(ch, 1, &Lowerer::lowerStatement));
      return finish(n);
    }
    case GK::ForStatement: {
      Node *n = mk(NodeKind::ForStatement, id);
      opt(n, ch.size() > 0 ? lowerForInit(ch[0]) : nullptr);
      opt(n, child(ch, 1, &Lowerer::lowerExpression));
      opt(n, child(ch, 2, &Lowerer::lowerExpression));
      req(n, child(ch, 3, &Lowerer::lowerStatement));
      return finish(n);
    }
    case GK::ForInStatement:
    case GK::ForOfStatement: {
      bool of = gk(id) == GK::ForOfStatement;
      Node *n = mk(of ? NodeKind::ForOfStatement : NodeKind::ForInStatement, id);
      req(n, ch.size() > 0 ? lowerForInit(ch[0]) : nullptr);
      req(n, child(ch, 1, &Lowerer::lowerExpression));
      req(n, child(ch, 2, &Lowerer::lowerStatement));
      if (of && gflag(id, syntax::FLAG_AWAIT)) {
        n->setFlag(Flag::Await);
      }
      return finish(n);
    }
    case GK::BreakStatement:
    case GK::ContinueStatement: {
      Node *n = mk(gk(id) == GK::BreakStatement ? NodeKind::BreakStatement
                                                : NodeKind::ContinueStatement,
                   id);
      opt(n, ch.size() > 0 ? identifier(ch[0]) : nullptr);
      return finish(n);
    }
    case GK::ReturnStatement: {
      Node *n = mk(NodeKind::ReturnStatement, id);
      opt(n, child(ch, 0, &Lowerer::lowerExpression));
      return finish(n);
    }
    case GK::ThrowStatement: {
      Node *n = mk(NodeKind::ThrowStatement, id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      return finish(n);
    }
    case GK::TryStatement:
      return lowerTry(id);
    case GK::SwitchStatement: {
      Node *n = mk(NodeKind::SwitchStatement, id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      for (size_t i = 1; i < ch.size(); i++) {
        elem(n, lowerSwitchCase(ch[i]));
      }
      return finish(n);
    }
    case GK::WithStatement: {
      Node *n = mk(NodeKind::WithStatement, id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      req(n, child(ch, 1, &Lowerer::lowerStatement));
      return finish(n);
    }
    case GK::LabeledStatement: {
      Node *n = mk(NodeKind::LabeledStatement, id);
      req(n, ch.size() > 0 ? identifier(ch[0]) : nullptr);
      req(n, child(ch, 1, &Lowerer::lowerStatement));
      return finish(n);
    }
    case GK::ImportDeclaration:
      return lowerImport(id);
    case GK::ImportEqualsDeclaration:
      return lowerImportEquals(id);
    case GK::ExportDeclaration:
      return lowerExport(id);
    case GK::ExportAssignment: {
      Node *n = mk(NodeKind::TSExportAssignment, id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      return finish(n);
    }
    case GK::NamespaceExportDeclaration: {
      Node *n = mk(NodeKind::TSNamespaceExportDeclaration, id);
      req(n, ch.size() > 0 ? identifier(ch[0]) : nullptr);
      return finish(n);
    }
    case GK::ErrorNode:
      return errorLeaf(id);
    default:
      return errorLeaf(id);
    }
  }

  template <typename Fn> Node *child(span<const NodeId> ch, size_t i, Fn fn)
  {
    return i < ch.size() ? (this->*fn)(ch[i]) : nullptr;
  }

  Node *lowerForInit(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    if (gk(id) == GK::VariableDeclarationList) {
      return lowerVariableList(id, id);
    }
    return lowerTarget(id);
  }

  Node *lowerVariableList(NodeId listId, NodeId link)
  {
    Node *n = mk(NodeKind::VariableDeclaration, link);
    VariableKind kind = VariableKind::Var;
    if (gflag(listId, syntax::FLAG_USING)) {
      kind = gflag(listId, syntax::FLAG_AWAIT) ? VariableKind::AwaitUsing
                                               : VariableKind::Using;
    } else if (gflag(listId, syntax::FLAG_CONST)) {
      kind = VariableKind::Const;
    } else if (hasOwnToken(listId, TokenKind::LetKeyword)) {
      kind = VariableKind::Let;
    }
    n->setDataByte(0, uint8_t(kind));
    if (hasOwnToken(link, TokenKind::DeclareKeyword)) {
      n->setFlag(Flag::Declare);
    }
    for (NodeId d : kids(listId)) {
      elem(n, lowerDeclarator(d));
    }
    return finish(n);
  }

  Node *lowerDeclarator(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    if (gk(id) != GK::VariableDeclaration) {
      return errorLeaf(id);
    }
    span<const NodeId> ch = kids(id);
    Node *n = mk(NodeKind::VariableDeclarator, id);
    Node *name = ch.size() > 0 ? lowerBindingName(ch[0]) : nullptr;
    Node *init = nullptr;
    for (size_t i = 1; i < ch.size(); i++) {
      if (isTypeKind(gk(ch[i]))) {
        attachType(name, lowerType(ch[i]));
      } else {
        init = lowerExpression(ch[i]);
      }
    }
    req(n, name);
    opt(n, init);
    if (hasOwnToken(id, TokenKind::ExclamationToken)) {
      n->setFlag(Flag::Definite);
    }
    return finish(n);
  }

  /** Fills the typeAnnotation slot of an Identifier or pattern. */
  void attachType(Node *name, Node *type)
  {
    if (!name || !type) {
      return;
    }
    if (name->kind == NodeKind::Identifier || name->kind == NodeKind::ObjectPattern ||
        name->kind == NodeKind::ArrayPattern)
    {
      name->children[0] = type;
      type->parent = name;
      finish(name);
    }
  }

  Node *lowerTry(NodeId id)
  {
    Node *n = mk(NodeKind::TryStatement, id);
    Node *block = nullptr;
    Node *handler = nullptr;
    Node *finalizer = nullptr;
    for (NodeId c : kids(id)) {
      if (gk(c) == GK::CatchClause) {
        Node *h = mk(NodeKind::CatchClause, c);
        Node *param = nullptr;
        Node *body = nullptr;
        for (NodeId cc : kids(c)) {
          if (gk(cc) == GK::VariableDeclaration) {
            span<const NodeId> pch = kids(cc);
            param = pch.size() > 0 ? lowerBindingName(pch[0]) : nullptr;
            if (pch.size() > 1 && isTypeKind(gk(pch[1]))) {
              attachType(param, lowerType(pch[1]));
            }
          } else if (gk(cc) == GK::Block) {
            body = lowerStatement(cc);
          }
        }
        opt(h, param);
        req(h, body);
        handler = finish(h);
      } else if (gk(c) == GK::Block) {
        if (!block) {
          block = lowerStatement(c);
        } else {
          finalizer = lowerStatement(c);
        }
      }
    }
    req(n, block);
    opt(n, handler);
    opt(n, finalizer);
    return finish(n);
  }

  Node *lowerSwitchCase(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    if (gk(id) != GK::CaseClause && gk(id) != GK::DefaultClause) {
      return errorLeaf(id);
    }
    Node *n = mk(NodeKind::SwitchCase, id);
    span<const NodeId> ch = kids(id);
    size_t i = 0;
    if (gk(id) == GK::CaseClause) {
      req(n, ch.size() > 0 ? lowerExpression(ch[i++]) : nullptr);
    } else {
      opt(n, nullptr);
    }
    lowerStatementList(n, ch.subspan(i), false);
    return finish(n);
  }

  // ------------------------------------------------------------- expressions

  Node *lowerExpression(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    span<const NodeId> ch = kids(id);
    switch (gk(id)) {
    case GK::Identifier:
      return identifier(id);
    case GK::PrivateIdentifier:
      return privateIdentifier(id);
    case GK::ThisExpression:
      return leaf(NodeKind::ThisExpression, id);
    case GK::SuperExpression:
      return leaf(NodeKind::Super, id);
    case GK::NumericLiteral:
      return literal(id, LiteralKind::Number);
    case GK::BigIntLiteral:
      return literal(id, LiteralKind::Bigint);
    case GK::StringLiteral:
      return literal(id, LiteralKind::String);
    case GK::RegularExpressionLiteral:
      return literal(id, LiteralKind::Regex);
    case GK::NullLiteral:
      return literal(id, LiteralKind::Null);
    case GK::TrueLiteral:
    case GK::FalseLiteral:
      return literal(id, LiteralKind::Boolean);
    case GK::NoSubstitutionTemplateLiteral: {
      Node *n = mk(NodeKind::TemplateLiteral, id);
      n->appendChild(templateElement(id, g(id).firstToken, true));
      return finish(n);
    }
    case GK::TemplateExpression:
      return lowerTemplate(id, NodeKind::TemplateLiteral, &Lowerer::lowerExpression);
    case GK::ArrayLiteralExpression: {
      Node *n = mk(NodeKind::ArrayExpression, id);
      for (NodeId c : ch) {
        n->appendChild(isMissing(c) ? nullptr : lowerExpression(c));
      }
      return finish(n);
    }
    case GK::ObjectLiteralExpression: {
      Node *n = mk(NodeKind::ObjectExpression, id);
      for (NodeId c : ch) {
        elem(n, lowerObjectMember(c, false));
      }
      return finish(n);
    }
    case GK::ParenthesizedExpression: {
      NodeId inner = id;
      while (gk(inner) == GK::ParenthesizedExpression && kids(inner).size() > 0) {
        inner = kids(inner)[0];
      }
      Node *n = lowerExpression(inner);
      if (n) {
        n->setFlag(Flag::Parenthesized);
        relink(n, id);
      }
      return n;
    }
    case GK::PropertyAccessExpression:
    case GK::ElementAccessExpression: {
      Node *n = mk(NodeKind::MemberExpression, id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      req(n, child(ch, 1, &Lowerer::lowerExpression));
      if (gk(id) == GK::ElementAccessExpression) {
        n->setFlag(Flag::Computed);
      }
      if (gflag(id, syntax::FLAG_OPTIONAL_CHAIN)) {
        n->setFlag(Flag::Optional);
      }
      return finish(n);
    }
    case GK::CallExpression:
    case GK::OptionalCallExpression:
      return lowerCall(id);
    case GK::NewExpression: {
      Node *n = mk(NodeKind::NewExpression, id);
      NodeId callee = ch.size() > 0 ? ch[0] : kNo;
      NodeId typeArgs = kNo;
      splitTypeArguments(callee, typeArgs);
      req(n, callee == kNo ? nullptr : lowerExpression(callee));
      opt(n, typeArgs == kNo ? nullptr : lowerTypeArguments(typeArgs));
      if (ch.size() > 1) {
        lowerArguments(n, ch[1]);
      }
      return finish(n);
    }
    case GK::MetaProperty: {
      Node *n = mk(NodeKind::MetaProperty, id);
      uint32_t keyword = g(id).firstToken;
      Node *meta = mkRange(NodeKind::Identifier, id, keyword, keyword + 1);
      meta->text = tokenText(keyword);
      meta->appendChild(nullptr);
      req(n, finish(meta));
      req(n, ch.size() > 0 ? identifier(ch[0]) : nullptr);
      return finish(n);
    }
    case GK::PrefixUnaryExpression: {
      TokenKind op = firstOwnTokenKind(id);
      if (op == TokenKind::PlusPlusToken || op == TokenKind::MinusMinusToken) {
        Node *n = mk(NodeKind::UpdateExpression, id);
        req(n, child(ch, 0, &Lowerer::lowerExpression));
        n->setDataByte(0,
                       uint8_t(op == TokenKind::PlusPlusToken
                                   ? UpdateOperator::Increment
                                   : UpdateOperator::Decrement));
        n->setFlag(Flag::Prefix);
        return finish(n);
      }
      UnaryOperator uop = UnaryOperator::Minus;
      switch (op) {
      case TokenKind::PlusToken:
        uop = UnaryOperator::Plus;
        break;
      case TokenKind::ExclamationToken:
        uop = UnaryOperator::Not;
        break;
      case TokenKind::TildeToken:
        uop = UnaryOperator::BitwiseNot;
        break;
      default:
        break;
      }
      return unary(id, uop, ch);
    }
    case GK::PostfixUnaryExpression: {
      Node *n = mk(NodeKind::UpdateExpression, id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      bool inc = hasOwnToken(id, TokenKind::PlusPlusToken);
      n->setDataByte(
          0, uint8_t(inc ? UpdateOperator::Increment : UpdateOperator::Decrement));
      return finish(n);
    }
    case GK::DeleteExpression:
      return unary(id, UnaryOperator::Delete, ch);
    case GK::TypeOfExpression:
      return unary(id, UnaryOperator::Typeof, ch);
    case GK::VoidExpression:
      return unary(id, UnaryOperator::Void, ch);
    case GK::AwaitExpression: {
      Node *n = mk(NodeKind::AwaitExpression, id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      return finish(n);
    }
    case GK::YieldExpression: {
      Node *n = mk(NodeKind::YieldExpression, id);
      opt(n, child(ch, 0, &Lowerer::lowerExpression));
      if (hasOwnToken(id, TokenKind::AsteriskToken)) {
        n->setFlag(Flag::Delegate);
      }
      return finish(n);
    }
    case GK::BinaryExpression:
      return lowerBinary(id);
    case GK::ConditionalExpression: {
      Node *n = mk(NodeKind::ConditionalExpression, id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      req(n, child(ch, 1, &Lowerer::lowerExpression));
      req(n, child(ch, 2, &Lowerer::lowerExpression));
      return finish(n);
    }
    case GK::FunctionExpression:
      return lowerFunction(id, NodeKind::FunctionExpression);
    case GK::ArrowFunction:
      return lowerFunction(id, NodeKind::ArrowFunctionExpression);
    case GK::ClassExpression:
      return lowerClass(id, NodeKind::ClassExpression);
    case GK::AsExpression:
    case GK::SatisfiesExpression: {
      Node *n = mk(gk(id) == GK::AsExpression ? NodeKind::TSAsExpression
                                              : NodeKind::TSSatisfiesExpression,
                   id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      req(n, child(ch, 1, &Lowerer::lowerType));
      return finish(n);
    }
    case GK::NonNullExpression: {
      Node *n = mk(NodeKind::TSNonNullExpression, id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      return finish(n);
    }
    case GK::TypeAssertionExpression: {
      Node *n = mk(NodeKind::TSTypeAssertion, id);
      req(n, child(ch, 0, &Lowerer::lowerType));
      req(n, child(ch, 1, &Lowerer::lowerExpression));
      return finish(n);
    }
    case GK::ExpressionWithTypeArguments: {
      Node *n = mk(NodeKind::TSInstantiationExpression, id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      req(n, child(ch, 1, &Lowerer::lowerTypeArguments));
      return finish(n);
    }
    case GK::TaggedTemplateExpression: {
      Node *n = mk(NodeKind::TaggedTemplateExpression, id);
      NodeId tag = ch.size() > 0 ? ch[0] : kNo;
      NodeId typeArgs = kNo;
      splitTypeArguments(tag, typeArgs);
      req(n, tag == kNo ? nullptr : lowerExpression(tag));
      opt(n, typeArgs == kNo ? nullptr : lowerTypeArguments(typeArgs));
      req(n, child(ch, 1, &Lowerer::lowerExpression));
      return finish(n);
    }
    case GK::SpreadElement: {
      Node *n = mk(NodeKind::SpreadElement, id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      return finish(n);
    }
    case GK::OmittedExpression:
      return nullptr;
    case GK::ImportKeyword: {
      Node *n = mk(NodeKind::Identifier, id);
      n->text = nodeText(id);
      opt(n, nullptr);
      return finish(n);
    }
    case GK::ErrorNode:
      return errorLeaf(id);
    default:
      if (isTypeKind(gk(id))) {
        return lowerType(id);
      }
      return errorLeaf(id);
    }
  }

  Node *unary(NodeId id, UnaryOperator op, span<const NodeId> ch)
  {
    Node *n = mk(NodeKind::UnaryExpression, id);
    req(n, child(ch, 0, &Lowerer::lowerExpression));
    n->setDataByte(0, uint8_t(op));
    return finish(n);
  }

  /** Peels `f<T>` into the expression and its type arguments. */
  void splitTypeArguments(NodeId &expr, NodeId &typeArgs)
  {
    if (expr != kNo && gk(expr) == GK::ExpressionWithTypeArguments) {
      span<const NodeId> ch = kids(expr);
      typeArgs = ch.size() > 1 ? ch[1] : kNo;
      expr = ch.size() > 0 ? ch[0] : kNo;
    }
  }

  void lowerArguments(Node *call, NodeId args)
  {
    if (gk(args) != GK::OmittedExpression) {
      elem(call, lowerExpression(args));
      return;
    }
    for (NodeId a : kids(args)) {
      elem(call, lowerExpression(a));
    }
  }

  Node *lowerCall(NodeId id)
  {
    span<const NodeId> ch = kids(id);
    NodeId callee = ch.size() > 0 ? ch[0] : kNo;
    if (callee != kNo && gk(callee) == GK::ImportKeyword) {
      Node *n = mk(NodeKind::ImportExpression, id);
      span<const NodeId> args = ch.size() > 1 ? kids(ch[1]) : span<const NodeId>{};
      req(n, args.size() > 0 ? lowerExpression(args[0]) : nullptr);
      opt(n, args.size() > 1 ? lowerExpression(args[1]) : nullptr);
      return finish(n);
    }
    Node *n = mk(NodeKind::CallExpression, id);
    NodeId typeArgs = kNo;
    splitTypeArguments(callee, typeArgs);
    req(n, callee == kNo ? nullptr : lowerExpression(callee));
    opt(n, typeArgs == kNo ? nullptr : lowerTypeArguments(typeArgs));
    if (ch.size() > 1) {
      lowerArguments(n, ch[1]);
    }
    if (hasOwnToken(id, TokenKind::QuestionDotToken)) {
      n->setFlag(Flag::Optional);
    }
    return finish(n);
  }

  static bool assignmentOperator(TokenKind kind, AssignmentOperator &op)
  {
    switch (kind) {
    case TokenKind::EqualsToken:
      op = AssignmentOperator::Assign;
      return true;
    case TokenKind::PlusEqualsToken:
      op = AssignmentOperator::AddAssign;
      return true;
    case TokenKind::MinusEqualsToken:
      op = AssignmentOperator::SubtractAssign;
      return true;
    case TokenKind::AsteriskEqualsToken:
      op = AssignmentOperator::MultiplyAssign;
      return true;
    case TokenKind::SlashEqualsToken:
      op = AssignmentOperator::DivideAssign;
      return true;
    case TokenKind::PercentEqualsToken:
      op = AssignmentOperator::RemainderAssign;
      return true;
    case TokenKind::AsteriskAsteriskEqualsToken:
      op = AssignmentOperator::ExponentAssign;
      return true;
    case TokenKind::LessThanLessThanEqualsToken:
      op = AssignmentOperator::ShiftLeftAssign;
      return true;
    case TokenKind::GreaterThanGreaterThanEqualsToken:
      op = AssignmentOperator::ShiftRightAssign;
      return true;
    case TokenKind::GreaterThanGreaterThanGreaterThanEqualsToken:
      op = AssignmentOperator::ShiftRightUnsignedAssign;
      return true;
    case TokenKind::PipeEqualsToken:
      op = AssignmentOperator::BitwiseOrAssign;
      return true;
    case TokenKind::CaretEqualsToken:
      op = AssignmentOperator::BitwiseXorAssign;
      return true;
    case TokenKind::AmpersandEqualsToken:
      op = AssignmentOperator::BitwiseAndAssign;
      return true;
    case TokenKind::PipePipeEqualsToken:
      op = AssignmentOperator::OrAssign;
      return true;
    case TokenKind::AmpersandAmpersandEqualsToken:
      op = AssignmentOperator::AndAssign;
      return true;
    case TokenKind::QuestionQuestionEqualsToken:
      op = AssignmentOperator::NullishAssign;
      return true;
    default:
      return false;
    }
  }

  static bool logicalOperator(TokenKind kind, LogicalOperator &op)
  {
    switch (kind) {
    case TokenKind::PipePipeToken:
      op = LogicalOperator::Or;
      return true;
    case TokenKind::AmpersandAmpersandToken:
      op = LogicalOperator::And;
      return true;
    case TokenKind::QuestionQuestionToken:
      op = LogicalOperator::Nullish;
      return true;
    default:
      return false;
    }
  }

  static BinaryOperator binaryOperator(TokenKind kind)
  {
    switch (kind) {
    case TokenKind::EqualsEqualsToken:
      return BinaryOperator::Equal;
    case TokenKind::ExclamationEqualsToken:
      return BinaryOperator::NotEqual;
    case TokenKind::EqualsEqualsEqualsToken:
      return BinaryOperator::StrictEqual;
    case TokenKind::ExclamationEqualsEqualsToken:
      return BinaryOperator::StrictNotEqual;
    case TokenKind::LessThanToken:
      return BinaryOperator::Less;
    case TokenKind::LessThanEqualsToken:
      return BinaryOperator::LessEqual;
    case TokenKind::GreaterThanToken:
      return BinaryOperator::Greater;
    case TokenKind::GreaterThanEqualsToken:
      return BinaryOperator::GreaterEqual;
    case TokenKind::LessThanLessThanToken:
      return BinaryOperator::ShiftLeft;
    case TokenKind::GreaterThanGreaterThanToken:
      return BinaryOperator::ShiftRight;
    case TokenKind::GreaterThanGreaterThanGreaterThanToken:
      return BinaryOperator::ShiftRightUnsigned;
    case TokenKind::PlusToken:
      return BinaryOperator::Add;
    case TokenKind::MinusToken:
      return BinaryOperator::Subtract;
    case TokenKind::AsteriskToken:
      return BinaryOperator::Multiply;
    case TokenKind::SlashToken:
      return BinaryOperator::Divide;
    case TokenKind::PercentToken:
      return BinaryOperator::Remainder;
    case TokenKind::AsteriskAsteriskToken:
      return BinaryOperator::Exponent;
    case TokenKind::PipeToken:
      return BinaryOperator::BitwiseOr;
    case TokenKind::CaretToken:
      return BinaryOperator::BitwiseXor;
    case TokenKind::AmpersandToken:
      return BinaryOperator::BitwiseAnd;
    case TokenKind::InKeyword:
      return BinaryOperator::In;
    case TokenKind::InstanceOfKeyword:
      return BinaryOperator::Instanceof;
    default:
      return BinaryOperator::Add;
    }
  }

  Node *lowerBinary(NodeId id)
  {
    span<const NodeId> ch = kids(id);
    TokenKind op = firstOwnTokenKind(id);
    if (op == TokenKind::CommaToken) {
      Node *n = mk(NodeKind::SequenceExpression, id);
      flattenSequence(n, id);
      return finish(n);
    }
    AssignmentOperator aop;
    if (assignmentOperator(op, aop)) {
      Node *n = mk(NodeKind::AssignmentExpression, id);
      NodeId left = ch.size() > 0 ? ch[0] : kNo;
      req(n,
          left == kNo ? nullptr
                      : (aop == AssignmentOperator::Assign ? lowerTarget(left)
                                                           : lowerExpression(left)));
      req(n, child(ch, 1, &Lowerer::lowerExpression));
      n->setDataByte(0, uint8_t(aop));
      return finish(n);
    }
    LogicalOperator lop;
    if (logicalOperator(op, lop)) {
      Node *n = mk(NodeKind::LogicalExpression, id);
      req(n, child(ch, 0, &Lowerer::lowerExpression));
      req(n, child(ch, 1, &Lowerer::lowerExpression));
      n->setDataByte(0, uint8_t(lop));
      return finish(n);
    }
    Node *n = mk(NodeKind::BinaryExpression, id);
    req(n, child(ch, 0, &Lowerer::lowerExpression));
    req(n, child(ch, 1, &Lowerer::lowerExpression));
    n->setDataByte(0, uint8_t(binaryOperator(op)));
    return finish(n);
  }

  /** `a, b, c` parses left-nested; one SequenceExpression holds all of them. */
  void flattenSequence(Node *seq, NodeId id)
  {
    span<const NodeId> ch = kids(id);
    for (size_t i = 0; i < ch.size(); i++) {
      NodeId c = ch[i];
      if (i == 0 && !isMissing(c) && gk(c) == GK::BinaryExpression &&
          firstOwnTokenKind(c) == TokenKind::CommaToken)
      {
        flattenSequence(seq, c);
      } else {
        elem(seq, lowerExpression(c));
      }
    }
  }

  template <typename Fn> Node *lowerTemplate(NodeId id, NodeKind kind, Fn lowerPart)
  {
    Node *n = mk(kind, id);
    n->appendChild(templateElement(id, g(id).firstToken, false));
    for (NodeId s : kids(id)) {
      span<const NodeId> sch = kids(s);
      elem(n, sch.size() > 0 ? (this->*lowerPart)(sch[0]) : nullptr);
      uint32_t last = endToken(s) - 1;
      TokenKind tk = g(s).tokenCount > 0 ? tokenKind(last) : TokenKind::EndOfFile;
      if (tk == TokenKind::TemplateMiddle || tk == TokenKind::TemplateTail) {
        n->appendChild(templateElement(s, last, tk == TokenKind::TemplateTail));
      } else {
        n->setFlag(Flag::Incomplete);
      }
    }
    return finish(n);
  }

  // ------------------------------------------------------ assignment targets

  Node *lowerTarget(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    span<const NodeId> ch = kids(id);
    switch (gk(id)) {
    case GK::ObjectLiteralExpression: {
      Node *n = mk(NodeKind::ObjectPattern, id);
      opt(n, nullptr);
      for (NodeId c : ch) {
        elem(n, lowerObjectMember(c, true));
      }
      return finish(n);
    }
    case GK::ArrayLiteralExpression: {
      Node *n = mk(NodeKind::ArrayPattern, id);
      opt(n, nullptr);
      for (NodeId c : ch) {
        if (isMissing(c)) {
          n->appendChild(nullptr);
        } else if (gk(c) == GK::SpreadElement) {
          Node *rest = mk(NodeKind::RestElement, c);
          req(rest, child(kids(c), 0, &Lowerer::lowerTarget));
          opt(rest, nullptr);
          n->appendChild(finish(rest));
        } else {
          n->appendChild(lowerTarget(c));
        }
      }
      return finish(n);
    }
    case GK::BinaryExpression: {
      if (firstOwnTokenKind(id) != TokenKind::EqualsToken) {
        return lowerExpression(id);
      }
      Node *n = mk(NodeKind::AssignmentPattern, id);
      req(n, child(ch, 0, &Lowerer::lowerTarget));
      req(n, child(ch, 1, &Lowerer::lowerExpression));
      return finish(n);
    }
    case GK::ParenthesizedExpression: {
      NodeId inner = id;
      while (gk(inner) == GK::ParenthesizedExpression && kids(inner).size() > 0) {
        inner = kids(inner)[0];
      }
      Node *n = lowerTarget(inner);
      if (n) {
        n->setFlag(Flag::Parenthesized);
        relink(n, id);
      }
      return n;
    }
    default:
      return lowerExpression(id);
    }
  }

  /** The key of a property; sets `computed` for `[expr]`. */
  Node *lowerKey(NodeId id, bool &computed)
  {
    computed = false;
    if (isMissing(id)) {
      return nullptr;
    }
    if (gk(id) == GK::ComputedPropertyName) {
      computed = true;
      return child(kids(id), 0, &Lowerer::lowerExpression);
    }
    return lowerExpression(id);
  }

  Node *lowerObjectMember(NodeId id, bool pattern)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    span<const NodeId> ch = kids(id);
    switch (gk(id)) {
    case GK::PropertyAssignment: {
      Node *n = mk(NodeKind::Property, id);
      bool computed = false;
      req(n, ch.size() > 0 ? lowerKey(ch[0], computed) : nullptr);
      req(n,
          ch.size() > 1 ? (pattern ? lowerTarget(ch[1]) : lowerExpression(ch[1]))
                        : nullptr);
      n->setDataByte(0, uint8_t(PropertyKind::Init));
      if (computed) {
        n->setFlag(Flag::Computed);
      }
      return finish(n);
    }
    case GK::ShorthandPropertyAssignment: {
      Node *n = mk(NodeKind::Property, id);
      req(n, ch.size() > 0 ? identifier(ch[0]) : nullptr);
      Node *value = ch.size() > 0 ? identifier(ch[0]) : nullptr;
      if (ch.size() > 1) {
        Node *assign = mkSpanning(NodeKind::AssignmentPattern, id, ch[0], ch[1]);
        req(assign, value);
        req(assign, lowerExpression(ch[1]));
        value = finish(assign);
      }
      req(n, value);
      n->setDataByte(0, uint8_t(PropertyKind::Init));
      n->setFlag(Flag::Shorthand);
      return finish(n);
    }
    case GK::SpreadAssignment: {
      Node *n = mk(pattern ? NodeKind::RestElement : NodeKind::SpreadElement, id);
      req(n,
          ch.size() > 0 ? (pattern ? lowerTarget(ch[0]) : lowerExpression(ch[0]))
                        : nullptr);
      if (pattern) {
        opt(n, nullptr);
      }
      return finish(n);
    }
    case GK::MethodDeclaration:
    case GK::GetAccessor:
    case GK::SetAccessor: {
      Node *n = mk(NodeKind::Property, id);
      size_t i = skipDecorators(ch);
      bool computed = false;
      Node *key = i < ch.size() ? lowerKey(ch[i], computed) : nullptr;
      req(n, key);
      req(n, methodValue(id, ch, i));
      PropertyKind kind = gk(id) == GK::GetAccessor   ? PropertyKind::Get
                          : gk(id) == GK::SetAccessor ? PropertyKind::Set
                                                      : PropertyKind::Init;
      n->setDataByte(0, uint8_t(kind));
      if (kind == PropertyKind::Init) {
        n->setFlag(Flag::Method);
      }
      if (computed) {
        n->setFlag(Flag::Computed);
      }
      return finish(n);
    }
    case GK::ErrorNode:
      return errorLeaf(id);
    default:
      return errorLeaf(id);
    }
  }

  size_t skipDecorators(span<const NodeId> ch) const
  {
    size_t i = 0;
    while (i < ch.size() && gk(ch[i]) == GK::Decorator) {
      i++;
    }
    return i;
  }

  /** The FunctionExpression value of a method: everything after the key. */
  Node *methodValue(NodeId id, span<const NodeId> ch, size_t keyIndex)
  {
    if (keyIndex >= ch.size()) {
      return nullptr;
    }
    uint32_t first = endToken(ch[keyIndex]);
    return lowerFunctionParts(id,
                              NodeKind::FunctionExpression,
                              nullptr,
                              ch.subspan(keyIndex + 1),
                              first,
                              endToken(id));
  }

  // ---------------------------------------------------------------- bindings

  Node *lowerBindingName(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    span<const NodeId> ch = kids(id);
    switch (gk(id)) {
    case GK::Identifier:
      return identifier(id);
    case GK::ObjectBindingPattern: {
      Node *n = mk(NodeKind::ObjectPattern, id);
      opt(n, nullptr);
      for (NodeId c : ch) {
        elem(n, lowerObjectBindingElement(c));
      }
      return finish(n);
    }
    case GK::ArrayBindingPattern: {
      Node *n = mk(NodeKind::ArrayPattern, id);
      opt(n, nullptr);
      for (NodeId c : ch) {
        n->appendChild(lowerArrayBindingElement(c));
      }
      return finish(n);
    }
    case GK::ErrorNode:
      return errorLeaf(id);
    default:
      return lowerExpression(id);
    }
  }

  Node *lowerObjectBindingElement(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    if (gk(id) != GK::BindingElement) {
      return errorLeaf(id);
    }
    span<const NodeId> ch = kids(id);
    if (gflag(id, syntax::FLAG_REST)) {
      Node *rest = mk(NodeKind::RestElement, id);
      req(rest, ch.size() > 0 ? lowerBindingName(ch[0]) : nullptr);
      opt(rest, nullptr);
      return finish(rest);
    }
    bool hasColon = hasOwnToken(id, TokenKind::ColonToken);
    bool hasInit = hasOwnToken(id, TokenKind::EqualsToken);
    Node *n = mk(NodeKind::Property, id);
    bool computed = false;
    size_t nameIndex = hasColon ? 1 : 0;
    Node *key = ch.size() > 0 ? lowerKey(ch[0], computed) : nullptr;
    NodeId nameId = nameIndex < ch.size() ? ch[nameIndex] : kNo;
    NodeId initId = hasInit && nameIndex + 1 < ch.size() ? ch[nameIndex + 1] : kNo;
    if (!hasColon && ch.size() > 0 && !hasInit && ch.size() == 2) {
      // `{ a: b }` when the colon token was lost to recovery: treat as key, name
      nameId = ch[1];
    }
    Node *value = nameId == kNo ? nullptr : lowerBindingName(nameId);
    if (initId != kNo && nameId != kNo) {
      Node *assign = mkSpanning(NodeKind::AssignmentPattern, id, nameId, initId);
      req(assign, value);
      req(assign, lowerExpression(initId));
      value = finish(assign);
    }
    req(n, key);
    req(n, value);
    n->setDataByte(0, uint8_t(PropertyKind::Init));
    if (!hasColon) {
      n->setFlag(Flag::Shorthand);
    }
    if (computed) {
      n->setFlag(Flag::Computed);
    }
    return finish(n);
  }

  Node *lowerArrayBindingElement(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    if (gk(id) != GK::BindingElement) {
      return errorLeaf(id);
    }
    span<const NodeId> ch = kids(id);
    if (ch.size() == 0 || isMissing(ch[0])) {
      return nullptr;
    }
    if (gflag(id, syntax::FLAG_REST)) {
      Node *rest = mk(NodeKind::RestElement, id);
      req(rest, lowerBindingName(ch[0]));
      opt(rest, nullptr);
      return finish(rest);
    }
    Node *name = lowerBindingName(ch[0]);
    if (ch.size() > 1) {
      Node *assign = mk(NodeKind::AssignmentPattern, id);
      req(assign, name);
      req(assign, lowerExpression(ch[1]));
      return finish(assign);
    }
    return name;
  }

  Node *lowerParameter(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    if (gk(id) != GK::Parameter) {
      return errorLeaf(id);
    }
    span<const NodeId> ch = kids(id);
    size_t i = skipDecorators(ch);
    Node *name = i < ch.size() ? lowerBindingName(ch[i]) : nullptr;
    i++;
    Node *type = nullptr;
    Node *init = nullptr;
    for (; i < ch.size(); i++) {
      if (isTypeKind(gk(ch[i]))) {
        type = lowerType(ch[i]);
      } else {
        init = lowerExpression(ch[i]);
      }
    }
    bool rest = gflag(id, syntax::FLAG_REST);
    if (!rest) {
      attachType(name, type);
    }
    if (name && gflag(id, syntax::FLAG_OPTIONAL) && name->kind == NodeKind::Identifier) {
      name->setFlag(Flag::Optional);
    }
    Node *result = name;
    if (rest) {
      Node *r = mk(NodeKind::RestElement, id);
      req(r, name);
      opt(r, type);
      result = finish(r);
    } else if (init) {
      Node *a = mk(NodeKind::AssignmentPattern, id);
      req(a, name);
      req(a, init);
      result = finish(a);
    }
    Accessibility access = accessibility(id);
    bool readonly = gflag(id, syntax::FLAG_READONLY);
    bool override = gflag(id, syntax::FLAG_OVERRIDE);
    if (access != Accessibility::None || readonly || override) {
      Node *p = mk(NodeKind::TSParameterProperty, id);
      opt(p, nullptr);
      req(p, result);
      p->setDataByte(0, uint8_t(access));
      if (readonly) {
        p->setFlag(Flag::Readonly);
      }
      if (override) {
        p->setFlag(Flag::Override);
      }
      result = finish(p);
    }
    if (!result) {
      return nullptr;
    }
    return result;
  }

  Accessibility accessibility(NodeId id) const
  {
    if (gflag(id, syntax::FLAG_PUBLIC)) {
      return Accessibility::Public;
    }
    if (gflag(id, syntax::FLAG_PRIVATE)) {
      return Accessibility::Private;
    }
    if (gflag(id, syntax::FLAG_PROTECTED)) {
      return Accessibility::Protected;
    }
    return Accessibility::None;
  }

  // --------------------------------------------------------------- functions

  Node *lowerFunction(NodeId id, NodeKind kind)
  {
    span<const NodeId> ch = kids(id);
    size_t i = skipDecorators(ch);
    Node *name = nullptr;
    if (kind != NodeKind::ArrowFunctionExpression && i < ch.size() &&
        gk(ch[i]) == GK::Identifier)
    {
      name = identifier(ch[i]);
      i++;
    }
    return lowerFunctionParts(id, kind, name, ch.subspan(i), kNoToken, 0);
  }

  /**
   * Builds a function-like from the children after its name. A token range
   * narrows the node to part of the grammar node (a method's value).
   */
  Node *lowerFunctionParts(NodeId id,
                           NodeKind kind,
                           Node *name,
                           span<const NodeId> parts,
                           uint32_t firstToken,
                           uint32_t endTok)
  {
    Node *typeParams = nullptr;
    Node *returnType = nullptr;
    Node *body = nullptr;
    bool bodyMissing = false;
    Vector<Node *> params;
    for (NodeId p : parts) {
      switch (gk(p)) {
      case GK::TypeParameters:
        typeParams = lowerTypeParameters(p);
        break;
      case GK::Parameter:
        params.append(lowerParameter(p));
        break;
      case GK::Block:
        if (isMissing(p)) {
          bodyMissing = true;
        } else {
          body = lowerStatement(p);
        }
        break;
      case GK::Decorator:
        break;
      default:
        if (isTypeKind(gk(p))) {
          returnType = lowerType(p);
        } else {
          body = lowerExpression(p);
        }
        break;
      }
    }
    if (!body) {
      if (kind == NodeKind::FunctionDeclaration) {
        kind = NodeKind::TSDeclareFunction;
      } else if (kind == NodeKind::FunctionExpression) {
        kind = NodeKind::TSEmptyBodyFunctionExpression;
      }
    }
    Node *n =
        firstToken == kNoToken ? mk(kind, id) : mkRange(kind, id, firstToken, endTok);
    opt(n, name);
    opt(n, typeParams);
    opt(n, returnType);
    opt(n, body);
    for (Node *p : params) {
      elem(n, p);
    }
    if (!body && !bodyMissing && kind == NodeKind::ArrowFunctionExpression) {
      n->setFlag(Flag::Incomplete);
    }
    if (gflag(id, syntax::FLAG_ASYNC)) {
      n->setFlag(Flag::Async);
    }
    if (gflag(id, syntax::FLAG_GENERATOR)) {
      n->setFlag(Flag::Generator);
    }
    if (kind == NodeKind::ArrowFunctionExpression && body &&
        body->kind != NodeKind::BlockStatement)
    {
      n->setFlag(Flag::Expression);
    }
    if ((kind == NodeKind::FunctionDeclaration || kind == NodeKind::TSDeclareFunction) &&
        hasOwnToken(id, TokenKind::DeclareKeyword))
    {
      n->setFlag(Flag::Declare);
    }
    return finish(n);
  }

  // ----------------------------------------------------------------- classes

  Node *lowerDecorators(NodeId link, span<const NodeId> ch, size_t count)
  {
    if (count == 0) {
      return nullptr;
    }
    Node *n = mkSpanning(NodeKind::Decorators, link, ch[0], ch[count - 1]);
    for (size_t i = 0; i < count; i++) {
      Node *d = mk(NodeKind::Decorator, ch[i]);
      req(d, child(kids(ch[i]), 0, &Lowerer::lowerExpression));
      n->appendChild(finish(d));
    }
    return finish(n);
  }

  Node *lowerClass(NodeId id, NodeKind kind)
  {
    span<const NodeId> ch = kids(id);
    Node *n = mk(kind, id);
    size_t i = skipDecorators(ch);
    opt(n, lowerDecorators(id, ch, i));
    opt(n, i < ch.size() && gk(ch[i]) == GK::Identifier ? identifier(ch[i++]) : nullptr);
    opt(n,
        i < ch.size() && gk(ch[i]) == GK::TypeParameters ? lowerTypeParameters(ch[i++])
                                                         : nullptr);
    Node *superClass = nullptr;
    Node *superTypeArgs = nullptr;
    Vector<Node *> implements;
    while (i < ch.size() && gk(ch[i]) == GK::HeritageClause) {
      NodeId clause = ch[i++];
      bool isExtends = hasOwnToken(clause, TokenKind::ExtendsKeyword);
      for (NodeId h : kids(clause)) {
        if (isExtends && !superClass) {
          NodeId expr = h;
          NodeId typeArgs = kNo;
          splitTypeArguments(expr, typeArgs);
          superClass = expr == kNo ? nullptr : lowerExpression(expr);
          superTypeArgs = typeArgs == kNo ? nullptr : lowerTypeArguments(typeArgs);
        } else {
          implements.append(lowerHeritage(h, NodeKind::TSClassImplements));
        }
      }
    }
    opt(n, superClass);
    opt(n, superTypeArgs);
    Node *body = classBody(id, ch.subspan(i));
    req(n, body);
    for (Node *impl : implements) {
      elem(n, impl);
    }
    if (gflag(id, syntax::FLAG_ABSTRACT)) {
      n->setFlag(Flag::Abstract);
    }
    if (kind == NodeKind::ClassDeclaration && hasOwnToken(id, TokenKind::DeclareKeyword))
    {
      n->setFlag(Flag::Declare);
    }
    return finish(n);
  }

  /** `implements X<T>` or `extends I<T>` on an interface. */
  Node *lowerHeritage(NodeId id, NodeKind kind)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    Node *n = mk(kind, id);
    span<const NodeId> ch = kids(id);
    NodeId expr = ch.size() > 0 ? ch[0] : kNo;
    NodeId typeArgs = ch.size() > 1 ? ch[1] : kNo;
    if (expr != kNo && gk(expr) == GK::TypeReference) {
      span<const NodeId> rch = kids(expr);
      if (rch.size() > 1 && typeArgs == kNo) {
        typeArgs = rch[1];
      }
      expr = rch.size() > 0 ? rch[0] : kNo;
    }
    req(n, expr == kNo ? nullptr : lowerEntityName(expr));
    opt(n, typeArgs == kNo ? nullptr : lowerTypeArguments(typeArgs));
    return finish(n);
  }

  /** The ClassBody stands for the braces and members of the class node. */
  Node *classBody(NodeId classId, span<const NodeId> members)
  {
    uint32_t open = findOwnToken(classId, TokenKind::OpenBraceToken);
    uint32_t close = kNoToken;
    forOwnTokens(classId, [&](uint32_t i) {
      if (tokenKind(i) == TokenKind::CloseBraceToken) {
        close = i;
      }
      return true;
    });
    Node *body =
        open == kNoToken
            ? mkRange(NodeKind::ClassBody, classId, endToken(classId), endToken(classId))
            : mkRange(NodeKind::ClassBody,
                      classId,
                      open,
                      close == kNoToken ? endToken(classId) : close + 1);
    for (NodeId m : members) {
      if (gk(m) == GK::SemicolonClassElement) {
        continue;
      }
      elem(body, lowerClassMember(m));
    }
    if (open == kNoToken) {
      body->setFlag(Flag::Incomplete);
    }
    return finish(body);
  }

  Node *lowerClassMember(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    span<const NodeId> ch = kids(id);
    switch (gk(id)) {
    case GK::PropertyDeclaration: {
      bool abstract = gflag(id, syntax::FLAG_ABSTRACT);
      bool accessor = gflag(id, syntax::FLAG_ACCESSOR);
      NodeKind kind = accessor ? (abstract ? NodeKind::TSAbstractAccessorProperty
                                           : NodeKind::AccessorProperty)
                               : (abstract ? NodeKind::TSAbstractPropertyDefinition
                                           : NodeKind::PropertyDefinition);
      Node *n = mk(kind, id);
      size_t i = skipDecorators(ch);
      opt(n, lowerDecorators(id, ch, i));
      bool computed = false;
      req(n, i < ch.size() ? lowerKey(ch[i], computed) : nullptr);
      Node *type = nullptr;
      Node *value = nullptr;
      for (i++; i < ch.size(); i++) {
        if (isTypeKind(gk(ch[i]))) {
          type = lowerType(ch[i]);
        } else {
          value = lowerExpression(ch[i]);
        }
      }
      opt(n, type);
      opt(n, value);
      n->setDataByte(0, uint8_t(accessibility(id)));
      setMemberFlags(n, id, computed);
      if (hasOwnToken(id, TokenKind::DeclareKeyword)) {
        n->setFlag(Flag::Declare);
      }
      if (gflag(id, syntax::FLAG_READONLY)) {
        n->setFlag(Flag::Readonly);
      }
      if (hasOwnToken(id, TokenKind::ExclamationToken)) {
        n->setFlag(Flag::Definite);
      }
      return finish(n);
    }
    case GK::MethodDeclaration:
    case GK::GetAccessor:
    case GK::SetAccessor:
    case GK::ConstructorNode: {
      bool abstract = gflag(id, syntax::FLAG_ABSTRACT);
      Node *n =
          mk(abstract ? NodeKind::TSAbstractMethodDefinition : NodeKind::MethodDefinition,
             id);
      size_t i = skipDecorators(ch);
      opt(n, lowerDecorators(id, ch, i));
      bool computed = false;
      req(n, i < ch.size() ? lowerKey(ch[i], computed) : nullptr);
      req(n, methodValue(id, ch, i));
      MethodKind kind = MethodKind::Method;
      switch (gk(id)) {
      case GK::GetAccessor:
        kind = MethodKind::Get;
        break;
      case GK::SetAccessor:
        kind = MethodKind::Set;
        break;
      case GK::ConstructorNode:
        kind = MethodKind::Constructor;
        break;
      default:
        break;
      }
      n->setDataByte(0, uint8_t(kind));
      n->setDataByte(1, uint8_t(accessibility(id)));
      setMemberFlags(n, id, computed);
      return finish(n);
    }
    case GK::ClassStaticBlockDeclaration: {
      Node *n = mk(NodeKind::StaticBlock, id);
      if (ch.size() > 0 && gk(ch[0]) == GK::Block) {
        lowerStatementList(n, kids(ch[0]), false);
      } else {
        n->setFlag(Flag::Incomplete);
      }
      return finish(n);
    }
    case GK::IndexSignature:
      return lowerTypeMember(id);
    case GK::ErrorNode:
      return errorLeaf(id);
    default:
      return errorLeaf(id);
    }
  }

  void setMemberFlags(Node *n, NodeId id, bool computed)
  {
    if (gflag(id, syntax::FLAG_STATIC)) {
      n->setFlag(Flag::Static);
    }
    if (gflag(id, syntax::FLAG_OVERRIDE)) {
      n->setFlag(Flag::Override);
    }
    if (gflag(id, syntax::FLAG_OPTIONAL)) {
      n->setFlag(Flag::Optional);
    }
    if (computed) {
      n->setFlag(Flag::Computed);
    }
  }

  // ------------------------------------------------------------ declarations

  Node *lowerInterface(NodeId id)
  {
    span<const NodeId> ch = kids(id);
    Node *n = mk(NodeKind::TSInterfaceDeclaration, id);
    size_t i = 0;
    req(n, i < ch.size() && gk(ch[i]) == GK::Identifier ? identifier(ch[i++]) : nullptr);
    opt(n,
        i < ch.size() && gk(ch[i]) == GK::TypeParameters ? lowerTypeParameters(ch[i++])
                                                         : nullptr);
    Vector<Node *> extends;
    while (i < ch.size() && gk(ch[i]) == GK::HeritageClause) {
      for (NodeId h : kids(ch[i])) {
        extends.append(lowerHeritage(h, NodeKind::TSInterfaceHeritage));
      }
      i++;
    }
    Node *body = nullptr;
    if (i < ch.size() && gk(ch[i]) == GK::TypeLiteral) {
      body = mk(NodeKind::TSInterfaceBody, ch[i]);
      for (NodeId m : kids(ch[i])) {
        elem(body, lowerTypeMember(m));
      }
      finish(body);
    }
    req(n, body);
    for (Node *e : extends) {
      elem(n, e);
    }
    if (hasOwnToken(id, TokenKind::DeclareKeyword)) {
      n->setFlag(Flag::Declare);
    }
    return finish(n);
  }

  Node *lowerEnum(NodeId id)
  {
    span<const NodeId> ch = kids(id);
    Node *n = mk(NodeKind::TSEnumDeclaration, id);
    size_t i = 0;
    req(n, i < ch.size() && gk(ch[i]) == GK::Identifier ? identifier(ch[i++]) : nullptr);
    for (; i < ch.size(); i++) {
      NodeId m = ch[i];
      if (isMissing(m)) {
        n->setFlag(Flag::Incomplete);
        continue;
      }
      if (gk(m) != GK::EnumMember) {
        n->appendChild(errorLeaf(m));
        continue;
      }
      Node *member = mk(NodeKind::TSEnumMember, m);
      span<const NodeId> mch = kids(m);
      bool computed = false;
      req(member, mch.size() > 0 ? lowerKey(mch[0], computed) : nullptr);
      opt(member, child(mch, 1, &Lowerer::lowerExpression));
      if (computed) {
        member->setFlag(Flag::Computed);
      }
      n->appendChild(finish(member));
    }
    if (gflag(id, syntax::FLAG_CONST)) {
      n->setFlag(Flag::Const);
    }
    if (hasOwnToken(id, TokenKind::DeclareKeyword)) {
      n->setFlag(Flag::Declare);
    }
    return finish(n);
  }

  Node *lowerModule(NodeId id)
  {
    span<const NodeId> ch = kids(id);
    Node *n = mk(NodeKind::TSModuleDeclaration, id);
    Vector<NodeId> names;
    Node *body = nullptr;
    for (NodeId c : ch) {
      if (gk(c) == GK::Block) {
        body = mk(NodeKind::TSModuleBlock, c);
        lowerStatementList(body, kids(c), false);
        finish(body);
      } else if (gk(c) == GK::Identifier || gk(c) == GK::StringLiteral) {
        names.append(c);
      }
    }
    Node *name = nullptr;
    if (names.size() == 1) {
      name = lowerExpression(names[0]);
    } else if (names.size() > 1) {
      name = qualifiedChain(id, span<const NodeId>(names.data(), names.size()));
    }
    req(n, name);
    opt(n, body);
    ModuleKind kind = ModuleKind::Global;
    if (hasOwnToken(id, TokenKind::NamespaceKeyword)) {
      kind = ModuleKind::Namespace;
    } else if (hasOwnToken(id, TokenKind::ModuleKeyword)) {
      kind = ModuleKind::Module;
    }
    n->setDataByte(0, uint8_t(kind));
    if (hasOwnToken(id, TokenKind::DeclareKeyword)) {
      n->setFlag(Flag::Declare);
    }
    return finish(n);
  }

  /** `A.B.C` as left-nested TSQualifiedName nodes over the grammar node `link`. */
  Node *qualifiedChain(NodeId link, span<const NodeId> names)
  {
    Node *left = lowerExpression(names[0]);
    for (size_t i = 1; i < names.size(); i++) {
      Node *q = mkSpanning(NodeKind::TSQualifiedName, link, names[0], names[i]);
      req(q, left);
      req(q, identifier(names[i]));
      left = finish(q);
    }
    return left;
  }

  Node *lowerEntityName(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    switch (gk(id)) {
    case GK::Identifier:
      return identifier(id);
    case GK::QualifiedName: {
      span<const NodeId> ch = kids(id);
      if (ch.size() == 0) {
        return errorLeaf(id);
      }
      return qualifiedChain(id, ch);
    }
    case GK::ThisType:
      return leaf(NodeKind::TSThisType, id);
    default:
      return lowerExpression(id);
    }
  }

  // ----------------------------------------------------------------- modules

  ImportKind importKind(NodeId id) const
  {
    return gflag(id, syntax::FLAG_TYPE_ONLY) || hasOwnToken(id, TokenKind::TypeKeyword)
               ? ImportKind::Type
               : ImportKind::Value;
  }

  Node *lowerImport(NodeId id)
  {
    span<const NodeId> ch = kids(id);
    Node *n = mk(NodeKind::ImportDeclaration, id);
    Node *source = nullptr;
    Node *attributes = nullptr;
    Vector<Node *> specifiers;
    for (NodeId c : ch) {
      switch (gk(c)) {
      case GK::StringLiteral:
        source = literal(c, LiteralKind::String);
        break;
      case GK::ImportAttributes:
        attributes = lowerImportAttributes(c);
        break;
      case GK::ImportClause:
        for (NodeId s : kids(c)) {
          switch (gk(s)) {
          case GK::Identifier: {
            Node *d = mk(NodeKind::ImportDefaultSpecifier, s);
            req(d, identifier(s));
            specifiers.append(finish(d));
            break;
          }
          case GK::NamespaceImport: {
            Node *ns = mk(NodeKind::ImportNamespaceSpecifier, s);
            req(ns, child(kids(s), 0, &Lowerer::lowerExpression));
            specifiers.append(finish(ns));
            break;
          }
          case GK::NamedImports:
            for (NodeId spec : kids(s)) {
              specifiers.append(lowerSpecifier(spec, NodeKind::ImportSpecifier));
            }
            break;
          default:
            specifiers.append(errorLeaf(s));
            break;
          }
        }
        break;
      default:
        break;
      }
    }
    req(n, source);
    opt(n, attributes);
    for (Node *s : specifiers) {
      elem(n, s);
    }
    n->setDataByte(0,
                   uint8_t(gflag(id, syntax::FLAG_TYPE_ONLY) ? ImportKind::Type
                                                             : ImportKind::Value));
    return finish(n);
  }

  /** `a as b` or `a`: the imported/local (or local/exported) pair. */
  Node *lowerSpecifier(NodeId id, NodeKind kind)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    if (gk(id) != GK::ImportSpecifier && gk(id) != GK::ExportSpecifier) {
      return errorLeaf(id);
    }
    span<const NodeId> ch = kids(id);
    Node *n = mk(kind, id);
    Node *first = ch.size() > 0 ? lowerExpression(ch[0]) : nullptr;
    Node *second = ch.size() > 1 ? lowerExpression(ch[1])
                                 : (ch.size() > 0 ? lowerExpression(ch[0]) : nullptr);
    req(n, first);
    req(n, second);
    n->setDataByte(0,
                   uint8_t(hasOwnToken(id, TokenKind::TypeKeyword) ? ImportKind::Type
                                                                   : ImportKind::Value));
    return finish(n);
  }

  Node *lowerImportAttributes(NodeId id)
  {
    Node *n = mk(NodeKind::ImportAttributes, id);
    for (NodeId a : kids(id)) {
      if (isMissing(a)) {
        n->setFlag(Flag::Incomplete);
        continue;
      }
      if (gk(a) != GK::ImportAttribute) {
        n->appendChild(errorLeaf(a));
        continue;
      }
      Node *attr = mk(NodeKind::ImportAttribute, a);
      req(attr, child(kids(a), 0, &Lowerer::lowerExpression));
      req(attr, child(kids(a), 1, &Lowerer::lowerExpression));
      n->appendChild(finish(attr));
    }
    return finish(n);
  }

  Node *lowerImportEquals(NodeId id)
  {
    span<const NodeId> ch = kids(id);
    Node *n = mk(NodeKind::TSImportEqualsDeclaration, id);
    req(n, ch.size() > 0 ? identifier(ch[0]) : nullptr);
    Node *ref = nullptr;
    if (ch.size() > 1) {
      if (gk(ch[1]) == GK::ExternalModuleReference) {
        ref = mk(NodeKind::TSExternalModuleReference, ch[1]);
        req(ref, child(kids(ch[1]), 0, &Lowerer::lowerExpression));
        finish(ref);
      } else {
        ref = lowerEntityName(ch[1]);
      }
    }
    req(n, ref);
    n->setDataByte(0, uint8_t(importKind(id)));
    return finish(n);
  }

  Node *lowerExport(NodeId id)
  {
    span<const NodeId> ch = kids(id);
    ImportKind kind =
        gflag(id, syntax::FLAG_TYPE_ONLY) ? ImportKind::Type : ImportKind::Value;
    if (gflag(id, syntax::FLAG_DEFAULT)) {
      Node *n = mk(NodeKind::ExportDefaultDeclaration, id);
      NodeId d = ch.size() > 0 ? ch[0] : kNo;
      req(n,
          d == kNo ? nullptr
                   : (isDeclarationKind(gk(d)) ? lowerStatement(d) : lowerExpression(d)));
      n->setDataByte(0, uint8_t(kind));
      return finish(n);
    }
    Node *source = nullptr;
    Node *attributes = nullptr;
    Node *declaration = nullptr;
    Node *exported = nullptr;
    Vector<Node *> specifiers;
    bool named = false;
    bool all = hasOwnToken(id, TokenKind::AsteriskToken);
    for (NodeId c : ch) {
      switch (gk(c)) {
      case GK::StringLiteral:
        source = literal(c, LiteralKind::String);
        break;
      case GK::ImportAttributes:
        attributes = lowerImportAttributes(c);
        break;
      case GK::NamedExports:
        named = true;
        for (NodeId s : kids(c)) {
          specifiers.append(lowerSpecifier(s, NodeKind::ExportSpecifier));
        }
        break;
      case GK::NamespaceExport:
        all = true;
        exported = child(kids(c), 0, &Lowerer::lowerExpression);
        break;
      default:
        declaration = lowerStatement(c);
        break;
      }
    }
    if (all && !named) {
      Node *n = mk(NodeKind::ExportAllDeclaration, id);
      opt(n, exported);
      req(n, source);
      opt(n, attributes);
      n->setDataByte(0, uint8_t(kind));
      return finish(n);
    }
    Node *n = mk(NodeKind::ExportNamedDeclaration, id);
    opt(n, declaration);
    opt(n, source);
    opt(n, attributes);
    for (Node *s : specifiers) {
      elem(n, s);
    }
    if (!declaration && !named) {
      n->setFlag(Flag::Incomplete);
    }
    n->setDataByte(0, uint8_t(kind));
    return finish(n);
  }

  // ------------------------------------------------------------------- types

  Node *lowerTypeParameters(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    Node *n = mk(NodeKind::TSTypeParameterDeclaration, id);
    for (NodeId p : kids(id)) {
      elem(n, lowerTypeParameter(p));
    }
    return finish(n);
  }

  Node *lowerTypeParameter(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    if (gk(id) != GK::TypeParameter) {
      return errorLeaf(id);
    }
    span<const NodeId> ch = kids(id);
    Node *n = mk(NodeKind::TSTypeParameter, id);
    size_t i = 0;
    if (i < ch.size() && gk(ch[i]) == GK::Identifier) {
      n->text = nodeText(ch[i]);
      i++;
    } else {
      n->setFlag(Flag::Incomplete);
    }
    // `in` before the name is variance; after it, a mapped type's constraint.
    uint32_t inToken = findOwnToken(id, TokenKind::InKeyword);
    uint32_t nameToken = ch.size() > 0 ? g(ch[0]).firstToken : g(id).firstToken;
    bool varianceIn = inToken != kNoToken && inToken < nameToken;
    bool mappedIn = inToken != kNoToken && !varianceIn;
    bool hasConstraint = hasOwnToken(id, TokenKind::ExtendsKeyword) || mappedIn;
    bool hasDefault = hasOwnToken(id, TokenKind::EqualsToken);
    Node *constraint = nullptr;
    Node *def = nullptr;
    if (hasConstraint && i < ch.size()) {
      constraint = lowerType(ch[i++]);
    }
    if (hasDefault && i < ch.size()) {
      def = lowerType(ch[i++]);
    }
    opt(n, constraint);
    opt(n, def);
    if (varianceIn) {
      n->setFlag(Flag::In);
    }
    if (hasOwnToken(id, TokenKind::OutKeyword)) {
      n->setFlag(Flag::Out);
    }
    if (hasOwnToken(id, TokenKind::ConstKeyword)) {
      n->setFlag(Flag::Const);
    }
    return finish(n);
  }

  Node *lowerTypeArguments(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    Node *n = mk(NodeKind::TSTypeParameterInstantiation, id);
    for (NodeId p : kids(id)) {
      elem(n, lowerType(p));
    }
    return finish(n);
  }

  static Keyword keywordFor(TokenKind kind)
  {
    switch (kind) {
    case TokenKind::AnyKeyword:
      return Keyword::Any;
    case TokenKind::BigIntKeyword:
      return Keyword::Bigint;
    case TokenKind::BooleanKeyword:
      return Keyword::Boolean;
    case TokenKind::IntrinsicKeyword:
      return Keyword::Intrinsic;
    case TokenKind::NeverKeyword:
      return Keyword::Never;
    case TokenKind::NullKeyword:
      return Keyword::Null;
    case TokenKind::NumberKeyword:
      return Keyword::Number;
    case TokenKind::ObjectKeyword:
      return Keyword::Object;
    case TokenKind::StringKeyword:
      return Keyword::String;
    case TokenKind::SymbolKeyword:
      return Keyword::Symbol;
    case TokenKind::UndefinedKeyword:
      return Keyword::Undefined;
    case TokenKind::UnknownKeyword:
      return Keyword::Unknown;
    case TokenKind::VoidKeyword:
      return Keyword::Void;
    default:
      return Keyword::Unknown;
    }
  }

  Node *lowerType(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    span<const NodeId> ch = kids(id);
    switch (gk(id)) {
    case GK::KeywordType: {
      Node *n = mk(NodeKind::TSKeywordType, id);
      n->setDataByte(0, uint8_t(keywordFor(tokenKind(g(id).firstToken))));
      return finish(n);
    }
    case GK::TypeReference: {
      Node *n = mk(NodeKind::TSTypeReference, id);
      req(n, child(ch, 0, &Lowerer::lowerEntityName));
      opt(n, child(ch, 1, &Lowerer::lowerTypeArguments));
      return finish(n);
    }
    case GK::QualifiedName:
    case GK::Identifier:
      return lowerEntityName(id);
    case GK::UnionType:
    case GK::IntersectionType: {
      Node *n = mk(gk(id) == GK::UnionType ? NodeKind::TSUnionType
                                           : NodeKind::TSIntersectionType,
                   id);
      for (NodeId c : ch) {
        elem(n, lowerType(c));
      }
      return finish(n);
    }
    case GK::ParenthesizedType: {
      NodeId inner = id;
      while (gk(inner) == GK::ParenthesizedType && kids(inner).size() > 0) {
        inner = kids(inner)[0];
      }
      Node *n = lowerType(inner);
      if (n) {
        n->setFlag(Flag::Parenthesized);
        relink(n, id);
      }
      return n;
    }
    case GK::FunctionType:
    case GK::ConstructorType: {
      Node *n = mk(gk(id) == GK::FunctionType ? NodeKind::TSFunctionType
                                              : NodeKind::TSConstructorType,
                   id);
      lowerSignature(n, ch);
      if (gflag(id, syntax::FLAG_ABSTRACT)) {
        n->setFlag(Flag::Abstract);
      }
      return finish(n);
    }
    case GK::ArrayType: {
      Node *n = mk(NodeKind::TSArrayType, id);
      req(n, child(ch, 0, &Lowerer::lowerType));
      return finish(n);
    }
    case GK::TupleType: {
      Node *n = mk(NodeKind::TSTupleType, id);
      for (NodeId c : ch) {
        elem(n, lowerType(c));
      }
      return finish(n);
    }
    case GK::NamedTupleMember: {
      Node *n = mk(NodeKind::TSNamedTupleMember, id);
      req(n, ch.size() > 0 ? identifier(ch[0]) : nullptr);
      req(n, child(ch, 1, &Lowerer::lowerType));
      if (gflag(id, syntax::FLAG_OPTIONAL)) {
        n->setFlag(Flag::Optional);
      }
      finish(n);
      if (gflag(id, syntax::FLAG_REST)) {
        Node *rest = mk(NodeKind::TSRestType, id);
        req(rest, n);
        return finish(rest);
      }
      return n;
    }
    case GK::OptionalType:
    case GK::RestType: {
      Node *n =
          mk(gk(id) == GK::OptionalType ? NodeKind::TSOptionalType : NodeKind::TSRestType,
             id);
      req(n, child(ch, 0, &Lowerer::lowerType));
      return finish(n);
    }
    case GK::TypeOperator: {
      Node *n = mk(NodeKind::TSTypeOperator, id);
      req(n, child(ch, 0, &Lowerer::lowerType));
      TypeOperator op = TypeOperator::Keyof;
      switch (firstOwnTokenKind(id)) {
      case TokenKind::UniqueKeyword:
        op = TypeOperator::Unique;
        break;
      case TokenKind::ReadOnlyKeyword:
        op = TypeOperator::Readonly;
        break;
      default:
        break;
      }
      n->setDataByte(0, uint8_t(op));
      return finish(n);
    }
    case GK::InferType: {
      Node *n = mk(NodeKind::TSInferType, id);
      req(n, child(ch, 0, &Lowerer::lowerTypeParameter));
      return finish(n);
    }
    case GK::IndexedAccessType: {
      Node *n = mk(NodeKind::TSIndexedAccessType, id);
      req(n, child(ch, 0, &Lowerer::lowerType));
      req(n, child(ch, 1, &Lowerer::lowerType));
      return finish(n);
    }
    case GK::ConditionalType: {
      Node *n = mk(NodeKind::TSConditionalType, id);
      for (size_t i = 0; i < 4; i++) {
        req(n, child(ch, i, &Lowerer::lowerType));
      }
      return finish(n);
    }
    case GK::MappedType:
      return lowerMappedType(id);
    case GK::LiteralType: {
      NodeId lit = ch.size() > 0 ? ch[0] : kNo;
      if (lit != kNo && gk(lit) == GK::NullLiteral) {
        Node *n = mk(NodeKind::TSKeywordType, id);
        n->setDataByte(0, uint8_t(Keyword::Null));
        return finish(n);
      }
      Node *n = mk(NodeKind::TSLiteralType, id);
      Node *value = lit == kNo ? nullptr : lowerExpression(lit);
      if (value && hasOwnToken(id, TokenKind::MinusToken)) {
        Node *neg = mk(NodeKind::UnaryExpression, id);
        req(neg, value);
        neg->setDataByte(0, uint8_t(UnaryOperator::Minus));
        value = finish(neg);
      }
      req(n, value);
      return finish(n);
    }
    case GK::TypeQuery: {
      Node *n = mk(NodeKind::TSTypeQuery, id);
      req(n, child(ch, 0, &Lowerer::lowerEntityName));
      opt(n, child(ch, 1, &Lowerer::lowerTypeArguments));
      return finish(n);
    }
    case GK::TypePredicate: {
      Node *n = mk(NodeKind::TSTypePredicate, id);
      req(n, child(ch, 0, &Lowerer::lowerEntityName));
      opt(n, child(ch, 1, &Lowerer::lowerType));
      if (hasOwnToken(id, TokenKind::AssertsKeyword)) {
        n->setFlag(Flag::Asserts);
      }
      return finish(n);
    }
    case GK::ThisType:
      return leaf(NodeKind::TSThisType, id);
    case GK::TypeLiteral: {
      Node *n = mk(NodeKind::TSTypeLiteral, id);
      for (NodeId m : ch) {
        elem(n, lowerTypeMember(m));
      }
      return finish(n);
    }
    case GK::ImportType:
      return lowerImportType(id);
    case GK::TemplateLiteralType:
      return lowerTemplate(id, NodeKind::TSTemplateLiteralType, &Lowerer::lowerType);
    case GK::TypeParameters:
      return lowerTypeParameters(id);
    case GK::TypeArguments:
      return lowerTypeArguments(id);
    case GK::ErrorNode:
      return errorLeaf(id);
    default:
      return errorLeaf(id);
    }
  }

  Node *lowerMappedType(NodeId id)
  {
    span<const NodeId> ch = kids(id);
    Node *n = mk(NodeKind::TSMappedType, id);
    size_t i = 0;
    req(n,
        i < ch.size() && gk(ch[i]) == GK::TypeParameter ? lowerTypeParameter(ch[i++])
                                                        : nullptr);
    bool hasAs = hasOwnToken(id, TokenKind::AsKeyword);
    bool hasColon = hasOwnToken(id, TokenKind::ColonToken);
    Node *nameType = hasAs && i < ch.size() ? lowerType(ch[i++]) : nullptr;
    Node *type = hasColon && i < ch.size() ? lowerType(ch[i++]) : nullptr;
    opt(n, nameType);
    opt(n, type);
    Modifier readonly = Modifier::None;
    Modifier optional = Modifier::None;
    TokenKind prev = TokenKind::EndOfFile;
    forOwnTokens(id, [&](uint32_t tokenIndex) {
      TokenKind k = tokenKind(tokenIndex);
      if (k == TokenKind::ReadOnlyKeyword) {
        readonly = prev == TokenKind::MinusToken ? Modifier::Minus : Modifier::Plus;
      } else if (k == TokenKind::QuestionToken) {
        optional = prev == TokenKind::MinusToken ? Modifier::Minus : Modifier::Plus;
      }
      prev = k;
      return true;
    });
    n->setDataByte(0, uint8_t(readonly));
    n->setDataByte(1, uint8_t(optional));
    return finish(n);
  }

  Node *lowerImportType(NodeId id)
  {
    span<const NodeId> ch = kids(id);
    bool query = hasOwnToken(id, TokenKind::TypeOfKeyword);
    Node *n =
        query ? mkRange(NodeKind::TSImportType, id, g(id).firstToken + 1, endToken(id))
              : mk(NodeKind::TSImportType, id);
    size_t i = 0;
    req(n, child(ch, i, &Lowerer::lowerType));
    i++;
    Node *qualifier = nullptr;
    Node *typeArgs = nullptr;
    for (; i < ch.size(); i++) {
      if (gk(ch[i]) == GK::TypeArguments) {
        typeArgs = lowerTypeArguments(ch[i]);
      } else {
        qualifier = lowerEntityName(ch[i]);
      }
    }
    opt(n, qualifier);
    opt(n, typeArgs);
    finish(n);
    if (!query) {
      return n;
    }
    Node *q = mk(NodeKind::TSTypeQuery, id);
    req(q, n);
    opt(q, nullptr);
    return finish(q);
  }

  /** Fills typeParameters?, returnType?, params... from a signature's children. */
  void lowerSignature(Node *n, span<const NodeId> ch)
  {
    Node *typeParams = nullptr;
    Node *returnType = nullptr;
    Vector<Node *> params;
    for (NodeId c : ch) {
      if (gk(c) == GK::TypeParameters) {
        typeParams = lowerTypeParameters(c);
      } else if (gk(c) == GK::Parameter) {
        params.append(lowerParameter(c));
      } else if (isTypeKind(gk(c))) {
        returnType = lowerType(c);
      } else {
        params.append(errorLeaf(c));
      }
    }
    opt(n, typeParams);
    opt(n, returnType);
    for (Node *p : params) {
      elem(n, p);
    }
  }

  Node *lowerTypeMember(NodeId id)
  {
    if (isMissing(id)) {
      return nullptr;
    }
    span<const NodeId> ch = kids(id);
    switch (gk(id)) {
    case GK::PropertySignature: {
      Node *n = mk(NodeKind::TSPropertySignature, id);
      bool computed = false;
      req(n, ch.size() > 0 ? lowerKey(ch[0], computed) : nullptr);
      opt(n, child(ch, 1, &Lowerer::lowerType));
      if (computed) {
        n->setFlag(Flag::Computed);
      }
      if (gflag(id, syntax::FLAG_OPTIONAL)) {
        n->setFlag(Flag::Optional);
      }
      if (gflag(id, syntax::FLAG_READONLY)) {
        n->setFlag(Flag::Readonly);
      }
      return finish(n);
    }
    case GK::MethodSignature:
    case GK::GetAccessorSignature:
    case GK::SetAccessorSignature: {
      Node *n = mk(NodeKind::TSMethodSignature, id);
      bool computed = false;
      req(n, ch.size() > 0 ? lowerKey(ch[0], computed) : nullptr);
      lowerSignature(n, ch.size() > 0 ? ch.subspan(1) : ch);
      MethodKind kind = gk(id) == GK::GetAccessorSignature   ? MethodKind::Get
                        : gk(id) == GK::SetAccessorSignature ? MethodKind::Set
                                                             : MethodKind::Method;
      n->setDataByte(0, uint8_t(kind));
      if (computed) {
        n->setFlag(Flag::Computed);
      }
      if (gflag(id, syntax::FLAG_OPTIONAL)) {
        n->setFlag(Flag::Optional);
      }
      return finish(n);
    }
    case GK::CallSignature:
    case GK::ConstructSignature: {
      Node *n =
          mk(gk(id) == GK::CallSignature ? NodeKind::TSCallSignatureDeclaration
                                         : NodeKind::TSConstructSignatureDeclaration,
             id);
      lowerSignature(n, ch);
      return finish(n);
    }
    case GK::IndexSignature: {
      Node *n = mk(NodeKind::TSIndexSignature, id);
      Node *type = nullptr;
      Vector<Node *> params;
      for (NodeId c : ch) {
        if (gk(c) == GK::Parameter) {
          params.append(lowerParameter(c));
        } else if (isTypeKind(gk(c))) {
          type = lowerType(c);
        }
      }
      opt(n, type);
      for (Node *p : params) {
        elem(n, p);
      }
      if (gflag(id, syntax::FLAG_READONLY)) {
        n->setFlag(Flag::Readonly);
      }
      if (gflag(id, syntax::FLAG_STATIC)) {
        n->setFlag(Flag::Static);
      }
      return finish(n);
    }
    case GK::ErrorNode:
      return errorLeaf(id);
    default:
      return errorLeaf(id);
    }
  }
};

} // namespace

Node *lower(const syntax::GrammarTree &tree, AstFile &file)
{
  Lowerer lowerer(tree, file);
  return lowerer.run();
}

} // namespace fastlint::ast
