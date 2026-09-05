#include "fastlint/syntax/parser.h"

#include "fastlint/syntax/parser/internal.h"

// JSX elements, fragments, attributes and children, shaped like tsgo: a
// JsxElement holds its opening element, children and closing element; a
// self-closing tag is a single JsxSelfClosingElement; attribute lists are
// always present as a JsxAttributes node. Text runs between children,
// whitespace-only ones included, are JsxText nodes.
//
// The scanner has no notion of JSX context, so every token that follows a
// JSX token is scanned in the mode the grammar needs here: SingleGreaterThan
// inside a tag (so `>` never merges into `>=`/`>>`), JsxText after a tag or
// `}` inside children, and Normal once the element is complete.

namespace fastlint::syntax {

using litestl::util::string;

using detail::isAlwaysIdentifier;

void Parser::scanNext(ScanMode mode)
{
  m_scanner.setMode(mode);
  m_scanner.scanOne();
  m_scanner.setMode(ScanMode::Normal);
}

/** Identifier, `ns:name`, `a.b.c` or `this`; scans the following token in tag mode. */
NodeId Parser::parseJsxTagName()
{
  uint32_t firstToken = pos();
  if (is(TokenKind::ThisKeyword)) {
    NodeId node = m_tree->beginNode(NodeKind::ThisExpression, firstToken);
    scanNext(ScanMode::SingleGreaterThan);
    NodeId name = m_tree->endNode(node, pos());
    return parseJsxMemberChain(name, firstToken);
  }
  NodeId name = parseJsxIdentifier();
  if (is(TokenKind::ColonToken)) {
    NodeId node = m_tree->beginNode(NodeKind::JsxNamespacedName, firstToken);
    m_tree->addChild(name);
    scanNext(ScanMode::SingleGreaterThan);
    m_tree->addChild(parseJsxIdentifier());
    return m_tree->endNode(node, pos());
  }
  return parseJsxMemberChain(name, firstToken);
}

NodeId Parser::parseJsxMemberChain(NodeId expression, uint32_t firstToken)
{
  while (is(TokenKind::DotToken)) {
    scanNext(ScanMode::SingleGreaterThan);
    NodeId node = m_tree->beginNode(NodeKind::PropertyAccessExpression, firstToken);
    m_tree->addChild(expression);
    m_tree->addChild(parseJsxIdentifier());
    expression = m_tree->endNode(node, pos());
  }
  return expression;
}

/** A JSX name (hyphens allowed, keywords allowed) as an Identifier node. */
NodeId Parser::parseJsxIdentifier()
{
  NodeId node = m_tree->beginNode(NodeKind::Identifier, pos());
  if (isAlwaysIdentifier(kind()) || tokenIsKeyword(kind())) {
    m_scanner.rescanJsxIdentifier();
    scanNext(ScanMode::SingleGreaterThan);
  } else {
    errorAt(token(), 1003, string("Identifier expected."));
    m_tree->node(node).flags |= FLAG_MISSING;
  }
  return m_tree->endNode(node, pos());
}

NodeId Parser::parseJsxAttributes()
{
  NodeId node = m_tree->beginNode(NodeKind::JsxAttributes, pos());
  while (!is(TokenKind::GreaterThanToken) && !is(TokenKind::SlashToken) &&
         !is(TokenKind::EndOfFile))
  {
    uint32_t firstToken = pos();
    if (is(TokenKind::OpenBraceToken)) {
      // `{...expr}`
      NodeId spread = m_tree->beginNode(NodeKind::JsxSpreadAttribute, firstToken);
      m_scanner.scanOne();
      (void)expect(TokenKind::DotDotDotToken);
      m_tree->addChild(parseAssignmentExpression());
      if (is(TokenKind::CloseBraceToken)) {
        scanNext(ScanMode::SingleGreaterThan);
      } else {
        errorAt(token(), 1005, string("'}' expected."));
      }
      m_tree->addChild(m_tree->endNode(spread, pos()));
      continue;
    }
    if (!isAlwaysIdentifier(kind()) && !tokenIsKeyword(kind())) {
      errorAt(token(), 1003, string("Identifier expected."));
      scanNext(ScanMode::SingleGreaterThan); // progress
      continue;
    }
    NodeId attribute = m_tree->beginNode(NodeKind::JsxAttribute, firstToken);
    NodeId name = parseJsxIdentifier();
    if (is(TokenKind::ColonToken)) {
      NodeId namespaced = m_tree->beginNode(NodeKind::JsxNamespacedName, firstToken);
      m_tree->addChild(name);
      scanNext(ScanMode::SingleGreaterThan);
      m_tree->addChild(parseJsxIdentifier());
      name = m_tree->endNode(namespaced, pos());
    }
    m_tree->addChild(name);
    if (is(TokenKind::EqualsToken)) {
      scanNext(ScanMode::SingleGreaterThan);
      if (is(TokenKind::StringLiteral)) {
        NodeId value = m_tree->beginNode(NodeKind::StringLiteral, pos());
        m_scanner.rescanJsxAttributeString();
        scanNext(ScanMode::SingleGreaterThan);
        m_tree->addChild(m_tree->endNode(value, pos()));
      } else if (is(TokenKind::OpenBraceToken)) {
        m_tree->addChild(parseJsxExpression(false));
      } else if (is(TokenKind::LessThanToken)) {
        m_tree->addChild(parseJsxElementOrFragment(false));
      } else {
        errorAt(token(), 17000, string("JSX attribute value expected."));
      }
    }
    m_tree->addChild(m_tree->endNode(attribute, pos()));
  }
  return m_tree->endNode(node, pos());
}

/** `{expr}` or `{...expr}`; `inChildren` picks the scan mode after `}`. */
NodeId Parser::parseJsxExpression(bool inChildren)
{
  NodeId node = m_tree->beginNode(NodeKind::JsxExpression, pos());
  m_scanner.scanOne(); // `{`
  if (eat(TokenKind::DotDotDotToken)) {
    m_tree->node(node).flags |= FLAG_REST;
  }
  if (!is(TokenKind::CloseBraceToken)) {
    detail::FlagScope allowIn(m_disallowIn, false);
    m_tree->addChild(parseExpression());
  }
  if (is(TokenKind::CloseBraceToken)) {
    scanNext(inChildren ? ScanMode::JsxText : ScanMode::SingleGreaterThan);
  } else {
    errorAt(token(), 1005, string("'}' expected."));
  }
  return m_tree->endNode(node, pos());
}

/** Children up to (not including) the `<` of the closing tag. */
void Parser::parseJsxChildren()
{
  for (;;) {
    if (is(TokenKind::JsxText)) {
      if (token().length > 0) {
        NodeId text = m_tree->beginNode(NodeKind::JsxText, pos());
        m_scanner.scanOne();
        m_tree->addChild(m_tree->endNode(text, pos()));
      } else {
        m_scanner.scanOne();
      }
      continue;
    }
    if (is(TokenKind::OpenBraceToken)) {
      m_tree->addChild(parseJsxExpression(true));
      continue;
    }
    if (is(TokenKind::LessThanToken)) {
      if (peekKind() == TokenKind::SlashToken) {
        return;
      }
      m_tree->addChild(parseJsxElementOrFragment(true));
      continue;
    }
    if (is(TokenKind::EndOfFile)) {
      errorAt(token(), 17008, string("JSX element has no corresponding closing tag."));
      return;
    }
    // A stray `}` (or anything else) inside children.
    errorAt(token(), 1381, string("Unexpected token."));
    scanNext(ScanMode::JsxText);
  }
}

/** From `<` through the closing `>`; `inChildren` picks the scan mode after it. */
NodeId Parser::parseJsxElementOrFragment(bool inChildren)
{
  uint32_t firstToken = pos();
  ScanMode after = inChildren ? ScanMode::JsxText : ScanMode::Normal;
  m_scanner.scanOne(); // `<`
  if (is(TokenKind::GreaterThanToken)) {
    // `<>…</>`
    NodeId fragment = m_tree->beginNode(NodeKind::JsxFragment, firstToken);
    NodeId open = m_tree->beginNode(NodeKind::JsxOpeningFragment, firstToken);
    scanNext(ScanMode::JsxText);
    m_tree->addChild(m_tree->endNode(open, pos()));
    parseJsxChildren();
    NodeId close = m_tree->beginNode(NodeKind::JsxClosingFragment, pos());
    if (is(TokenKind::LessThanToken)) {
      m_scanner.scanOne();
      (void)expect(TokenKind::SlashToken);
      if (is(TokenKind::GreaterThanToken)) {
        scanNext(after);
      } else {
        errorAt(token(),
                17014,
                string("Expected corresponding closing tag for JSX fragment."));
      }
    }
    m_tree->addChild(m_tree->endNode(close, pos()));
    return m_tree->endNode(fragment, pos());
  }

  NodeId opening = m_tree->beginNode(NodeKind::JsxOpeningElement, firstToken);
  m_tree->addChild(parseJsxTagName());
  if (is(TokenKind::LessThanToken)) {
    m_tree->addChild(parseTypeArgumentList());
  }
  m_tree->addChild(parseJsxAttributes());
  if (is(TokenKind::SlashToken)) {
    scanNext(ScanMode::SingleGreaterThan);
    m_tree->node(opening).kind = NodeKind::JsxSelfClosingElement;
    if (is(TokenKind::GreaterThanToken)) {
      scanNext(after);
    } else {
      errorAt(token(), 1005, string("'>' expected."));
    }
    return m_tree->endNode(opening, pos());
  }
  if (is(TokenKind::GreaterThanToken)) {
    scanNext(ScanMode::JsxText);
  } else {
    errorAt(token(), 1005, string("'>' expected."));
  }
  NodeId open = m_tree->endNode(opening, pos());

  NodeId element = m_tree->beginNode(NodeKind::JsxElement, firstToken);
  m_tree->addChild(open);
  parseJsxChildren();
  NodeId closing = m_tree->beginNode(NodeKind::JsxClosingElement, pos());
  if (is(TokenKind::LessThanToken)) {
    m_scanner.scanOne();
    (void)expect(TokenKind::SlashToken);
    if (is(TokenKind::GreaterThanToken)) {
      // `</>` closing a named element: the name is missing.
      NodeId name = m_tree->beginNode(NodeKind::Identifier, pos());
      m_tree->node(name).flags |= FLAG_MISSING;
      m_tree->addChild(m_tree->endNode(name, pos()));
      errorAt(token(), 17002, string("Expected corresponding JSX closing tag."));
    } else {
      m_tree->addChild(parseJsxTagName());
    }
    if (is(TokenKind::GreaterThanToken)) {
      scanNext(after);
    } else {
      errorAt(token(), 1005, string("'>' expected."));
    }
  }
  m_tree->addChild(m_tree->endNode(closing, pos()));
  return m_tree->endNode(element, pos());
}

/** In `.tsx`, `<T,>(…) =>` and `<T extends U>(…) =>` are arrows; other `<` open JSX. */
bool Parser::isJsxGenericArrowHead()
{
  Mark mark = begin();
  m_scanner.scanOne(); // `<`
  bool result = false;
  if (isAlwaysIdentifier(kind()) || is(TokenKind::ConstKeyword)) {
    (void)eat(TokenKind::ConstKeyword);
    m_scanner.scanOne();
    if (is(TokenKind::CommaToken)) {
      result = true;
    } else if (eat(TokenKind::ExtendsKeyword)) {
      result = !is(TokenKind::EqualsToken) && !is(TokenKind::GreaterThanToken) &&
               !is(TokenKind::SlashToken);
    }
  }
  rollback(mark);
  return result;
}

} // namespace fastlint::syntax
