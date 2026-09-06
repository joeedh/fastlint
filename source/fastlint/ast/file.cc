#include "fastlint/ast/file.h"

#include "util/alloc.h"

#include <cstring>

namespace fastlint::ast {

namespace {
constexpr size_t kChunkSize = 4096;
}

AstFile::~AstFile()
{
  for (char *chunk : m_chunks) {
    litestl::alloc::release(chunk);
  }
}

Node *AstFile::make(NodeKind kind, GrammarRef grammar)
{
  Node *node = m_pool.alloc(kind);
  node->grammar = grammar;
  return node;
}

void AstFile::buildPreorder()
{
  m_preorder.clear();
  if (!m_root) {
    return;
  }
  // Iterative so a deep tree cannot overflow the stack; each frame is the node
  // plus how far into its children the walk has got.
  struct Frame {
    Node *node;
    uint32_t index;
    uint32_t nextChild;
  };
  Vector<Frame, 64> stack;
  m_preorder.append({m_root, 0});
  stack.append({m_root, 0, 0});
  while (!stack.isEmpty()) {
    Frame &top = stack[int(stack.size()) - 1];
    Vector<Node *, 3> &children = top.node->children;
    while (top.nextChild < children.size() && !children[int(top.nextChild)]) {
      top.nextChild++;
    }
    if (top.nextChild == children.size()) {
      m_preorder[int(top.index)].subtreeEnd = uint32_t(m_preorder.size());
      stack.pop_back();
      continue;
    }
    Node *child = children[int(top.nextChild++)];
    uint32_t index = uint32_t(m_preorder.size());
    m_preorder.append({child, 0});
    stack.append({child, index, 0});
  }
}

void AstFile::moveComments(const Node *from, const Node *to)
{
  CommentList moved;
  if (!m_comments.remove(from, &moved)) {
    return;
  }
  CommentList &dest = m_comments[to];
  for (const Comment &c : moved) {
    dest.append(c);
  }
}

string_view AstFile::intern(string_view text)
{
  if (text.empty()) {
    return {};
  }
  if (m_chunks.isEmpty() || m_chunkUsed + text.size() > m_chunkSize) {
    m_chunkSize = text.size() > kChunkSize ? text.size() : kChunkSize;
    m_chunks.append(
        static_cast<char *>(litestl::alloc::alloc("ast::AstFile::strings", m_chunkSize)));
    m_chunkUsed = 0;
  }
  char *dest = m_chunks.last() + m_chunkUsed;
  std::memcpy(dest, text.data(), text.size());
  m_chunkUsed += text.size();
  return {dest, text.size()};
}

} // namespace fastlint::ast
