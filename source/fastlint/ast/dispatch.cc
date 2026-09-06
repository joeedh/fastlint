#include "fastlint/ast/dispatch.h"

namespace fastlint::ast {

void Dispatcher::sortEntries()
{
  // Counting sort keeps registration order within a kind, which is the order
  // rules expect their own listeners to fire in.
  Vector<uint32_t> counts;
  counts.resize(size_t(kindCount) * 2 + 1);
  for (uint32_t &c : counts) {
    c = 0;
  }
  auto bucket = [](const Entry &e) { return int(e.kind) * 2 + (e.exit ? 1 : 0); };
  for (const Entry &e : m_entries) {
    counts[bucket(e) + 1]++;
  }
  for (int i = 1; i < int(counts.size()); i++) {
    counts[i] += counts[i - 1];
  }
  m_starts.clear();
  m_starts.resize(size_t(kindCount) * 2 + 1);
  for (int i = 0; i < int(counts.size()); i++) {
    m_starts[i] = counts[i];
  }
  Vector<uint32_t> slots;
  slots.resize(m_entries.size());
  for (uint32_t i = 0; i < m_entries.size(); i++) {
    slots[int(counts[bucket(m_entries[int(i)])]++)] = i;
  }
  Vector<Entry> sorted;
  for (uint32_t i : slots) {
    sorted.append(m_entries[int(i)]);
  }
  m_entries = std::move(sorted);
  m_sorted = true;
}

bool Dispatcher::hasListeners(NodeKind kind, bool exit) const
{
  int b = int(kind) * 2 + (exit ? 1 : 0);
  return m_starts[b + 1] > m_starts[b];
}

void Dispatcher::fire(NodeKind kind, bool exit, Node *node)
{
  int b = int(kind) * 2 + (exit ? 1 : 0);
  for (uint32_t i = m_starts[b]; i < m_starts[b + 1]; i++) {
    m_entries[int(i)].listener(node);
  }
}

void Dispatcher::run(const AstFile &file)
{
  if (!m_sorted) {
    sortEntries();
  }
  span<const PreorderEntry> order = file.preorder();
  // Nodes whose exit has not fired yet, innermost last; an entry leaves the
  // stack when the scan reaches its subtree end.
  Vector<uint32_t, 64> open;
  auto exitOpen = [&]() {
    uint32_t i = open.pop_back();
    fire(order[i].node->kind, true, order[i].node);
  };
  for (uint32_t i = 0; i < order.size(); i++) {
    while (!open.isEmpty() && order[open[int(open.size()) - 1]].subtreeEnd == i) {
      exitOpen();
    }
    NodeKind kind = order[i].node->kind;
    fire(kind, false, order[i].node);
    if (hasListeners(kind, true)) {
      open.append(i);
    }
  }
  while (!open.isEmpty()) {
    exitOpen();
  }
}

} // namespace fastlint::ast
