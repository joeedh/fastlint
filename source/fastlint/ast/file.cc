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
