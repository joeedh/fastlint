#pragma once

// The grammar tree: a faithful parse tree over the token stream, laid out as a
// flat per-file arena (docs/STRATEGY.md "AST"). Nodes hold ranges into one
// shared child-id vector and one shared token array; nothing owns inline
// vectors. `Error`/`Missing` shapes come from node flags, not separate kinds,
// so recovery can attach them to any production.

#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/tokens.h"
#include "util/span.h"

#include <span>
#include "util/string.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

namespace fastlint::syntax {

using litestl::util::span;
using litestl::util::string;
using std::string_view;
using litestl::util::Vector;

enum class NodeKind : uint16_t {
#define FASTLINT_NODE(kind) kind,
#include "fastlint/syntax/node_kind.def"
#undef FASTLINT_NODE
};

constexpr int nodeKindCount = 0
#define FASTLINT_NODE(kind) + 1
#include "fastlint/syntax/node_kind.def"
#undef FASTLINT_NODE
    ;

const char *nodeKindName(NodeKind kind);

enum NodeFlags : uint32_t {
  FLAG_NONE = 0,
  /** Node synthesized by error recovery for something the source lacks. */
  FLAG_MISSING = 1 << 0,
  /** Node wraps source the parser could not make sense of. */
  FLAG_ERROR = 1 << 1,
  /** Definition in an ambient context (`declare`, interface bodies, …). */
  FLAG_AMBIENT = 1 << 2,
  /** `export` or `export default` was present. */
  FLAG_EXPORTED = 1 << 3,
  /** `default` was present. */
  FLAG_DEFAULT = 1 << 4,
  /** `async` was present. */
  FLAG_ASYNC = 1 << 5,
  /** `const`, `readonly`, `static`, `abstract`, `override`, `declare`, `accessor`. */
  FLAG_CONST = 1 << 6,
  FLAG_READONLY = 1 << 7,
  FLAG_STATIC = 1 << 8,
  FLAG_ABSTRACT = 1 << 9,
  FLAG_OVERRIDE = 1 << 10,
  FLAG_ACCESSOR = 1 << 11,
  /** Optional member (`foo?:`) or optional parameter (`foo?`). */
  FLAG_OPTIONAL = 1 << 12,
  /** Rest parameter, spread element or variadic type element. */
  FLAG_REST = 1 << 13,
  /** Generator (`function*`, `*method()`). */
  FLAG_GENERATOR = 1 << 14,
  /** Question-dot chain (`a?.b`, `a?.[b]`, `a?.()`). */
  FLAG_OPTIONAL_CHAIN = 1 << 15,
  /** Statement ended by automatic semicolon insertion rather than a `;`. */
  FLAG_ASI = 1 << 16,
  /** `public`, `private`, `protected` on a member or constructor parameter. */
  FLAG_PUBLIC = 1 << 17,
  FLAG_PRIVATE = 1 << 18,
  FLAG_PROTECTED = 1 << 19,
};

inline NodeFlags operator|(NodeFlags a, NodeFlags b)
{
  return NodeFlags(uint32_t(a) | uint32_t(b));
}
inline bool hasFlags(uint32_t flags, NodeFlags test)
{
  return (flags & uint32_t(test)) != 0;
}

using NodeId = uint32_t;
constexpr NodeId kNoNode = 0xffffffffu;

/** One grammar-tree node. Children are a range into the tree's shared id vector. */
struct Node {
  NodeKind kind;
  uint32_t flags = FLAG_NONE;
  NodeId parent = kNoNode;
  /** Index into the shared child-id vector; children are firstChild..+childCount. */
  uint32_t firstChild = 0;
  uint32_t childCount = 0;
  /** Range into the tree's token array; zero-length for Missing nodes. */
  uint32_t firstToken = 0;
  uint32_t tokenCount = 0;
};

/** One trivia run (whitespace, newline, comment, shebang). */
struct Trivia {
  enum class Kind : uint8_t { Whitespace, NewLine, SingleLineComment, MultiLineComment, Shebang };
  Kind kind;
  uint32_t offset;
  uint32_t length;
  /** True when the run contains at least one newline. */
  bool lineBreak;
};

/**
 * A parsed file. Owns the node arena, the shared child-id vector, the token
 * and trivia arrays, and the line-start table. Ids, not pointers.
 */
class GrammarTree {
public:
  GrammarTree() = default;
  GrammarTree(string_view source, string fileName)
      : m_source(source), m_fileName(std::move(fileName))
  {
  }

  string_view source() const
  {
    return m_source;
  }
  const string &fileName() const
  {
    return m_fileName;
  }

  span<const Node> nodes()
  {
    return {m_nodes.data(), m_nodes.size()};
  }
  span<const NodeId> children(NodeId id)
  {
    const Node &node = m_nodes[id];
    return {m_childIds.data() + node.firstChild, size_t(node.childCount)};
  }
  span<const Token> tokens()
  {
    return {m_tokens.data(), m_tokens.size()};
  }
  span<const Trivia> trivia()
  {
    return {m_trivia.data(), m_trivia.size()};
  }
  /** Byte offsets of the first character of every line (0 = line 1). */
  span<const uint32_t> lineStarts()
  {
    return {m_lineStarts.data(), m_lineStarts.size()};
  }

  NodeId root() const
  {
    return m_root;
  }

  /** Source text of a token. */
  string_view tokenText(const Token &token) const
  {
    return m_source.substr(token.offset, token.length);
  }

  /** 1-based line of a byte offset; computes from the line-start table. */
  uint32_t lineOf(uint32_t offset) const;

  const Token &tokenAt(uint32_t index) const
  {
    return m_tokens[index];
  }

  // ------------------------------------------------------- building (parser)

  /** Trailing slots are for the builder; see parser.cc. */
  Node &node(NodeId id)
  {
    return m_nodes[id];
  }
  Vector<NodeId> &childIds()
  {
    return m_childIds;
  }
  Vector<Token> &tokenArray()
  {
    return m_tokens;
  }
  Vector<Trivia> &triviaArray()
  {
    return m_trivia;
  }
  Vector<uint32_t> &lineStartsForBuild()
  {
    return m_lineStarts;
  }
  void setSourceForBuild(string_view source)
  {
    m_source = source;
  }
  void setRoot(NodeId id)
  {
    m_root = id;
  }

  /** Save/restore point for parser speculation; truncateNodes discards
   * partially built nodes when a speculation is rolled back. */
  struct BuildState {
    size_t nodes;
    size_t children;
    size_t marks;
  };
  BuildState buildState() const
  {
    return {m_nodes.size(), m_pendingChildren.size(), m_pendingMarks.size()};
  }
  void restoreBuild(const BuildState &state)
  {
    m_nodes.resize(state.nodes);
    m_pendingChildren.resize(state.children);
    m_pendingMarks.resize(state.marks);
  }

  // --- building. The parser nests beginNode/endNode strictly (a stack), and
  // adds children to whichever node is innermost right now.

  /** Opens a node; ids are handed out immediately so children can be built. */
  NodeId beginNode(NodeKind kind, uint32_t firstToken);
  /** Appends `child` to the innermost open node. */
  void addChild(NodeId child);
  /** Closes the node: commits its child range and token range. */
  NodeId endNode(NodeId id, uint32_t endToken);

  string_view sourceFrom(uint32_t offset, uint32_t length) const
  {
    return m_source.substr(offset, length);
  }

private:
  string_view m_source;
  string m_fileName;
  Vector<Node> m_nodes;
  Vector<NodeId> m_childIds;
  Vector<Token> m_tokens;
  Vector<Trivia> m_trivia;
  Vector<uint32_t> m_lineStarts;
  NodeId m_root = kNoNode;
  // Builder state: scratch of child ids per open node, with one mark per open
  // node. Committed contiguously to m_childIds on endNode.
  Vector<NodeId> m_pendingChildren;
  Vector<uint32_t> m_pendingMarks;
};

} // namespace fastlint::syntax
