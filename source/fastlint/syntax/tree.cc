#include "fastlint/syntax/tree.h"

namespace fastlint::syntax {

namespace {

constexpr const char *kNodeKindNames[] = {
#define FASTLINT_NODE(kind) #kind,
#include "fastlint/syntax/node_kind.def"
#undef FASTLINT_NODE
};

} // namespace

const char *nodeKindName(NodeKind kind)
{
  return kNodeKindNames[size_t(kind)];
}

NodeId GrammarTree::beginNode(NodeKind kind, uint32_t firstToken)
{
  Node node;
  node.kind = kind;
  node.firstToken = firstToken;
  m_nodes.append(node);
  m_pendingMarks.append(uint32_t(m_pendingChildren.size()));
  return NodeId(m_nodes.size() - 1);
}

void GrammarTree::addChild(NodeId child)
{
  m_pendingChildren.append(child);
}

NodeId GrammarTree::endNode(NodeId id, uint32_t endToken)
{
  uint32_t mark = m_pendingMarks.last();
  m_pendingMarks.pop_back();
  Node &node = m_nodes[id];
  node.firstChild = uint32_t(m_childIds.size());
  node.childCount = uint32_t(m_pendingChildren.size()) - mark;
  for (uint32_t i = mark; i < uint32_t(m_pendingChildren.size()); ++i) {
    NodeId child = m_pendingChildren[i];
    m_nodes[child].parent = id;
    m_childIds.append(child);
  }
  m_pendingChildren.resize(mark);
  // A missing/error node may own no tokens; otherwise the range is
  // [firstToken, endToken), clamped so an empty production stays empty.
  node.tokenCount = endToken > node.firstToken ? endToken - node.firstToken : 0;
  return id;
}



uint32_t GrammarTree::lineOf(uint32_t offset) const
{
  // Binary search over line starts.
  size_t lo = 0;
  size_t hi = m_lineStarts.size();
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (m_lineStarts[mid] <= offset) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return uint32_t(lo); // 1-based because line 0 starts at offset 0
}

} // namespace fastlint::syntax
