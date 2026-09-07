#include "fastlint/tsgo/source_file.h"

#include "fastlint/ast/file.h"
#include "fastlint/ast/node.h"

#include <cstdio>

namespace fastlint::tsgo {

namespace {

constexpr size_t kHeaderSize = 44;
constexpr size_t kNodeSize = 28;
constexpr size_t kNodesOffsetField = 40;

uint32_t readU32(span<const uint8_t> data, size_t at)
{
  return uint32_t(data[at]) | (uint32_t(data[at + 1]) << 8) |
         (uint32_t(data[at + 2]) << 16) | (uint32_t(data[at + 3]) << 24);
}

uint64_t readU64(span<const uint8_t> data, size_t at)
{
  return uint64_t(readU32(data, at)) | (uint64_t(readU32(data, at + 4)) << 32);
}

} // namespace

bool EncodedSourceFile::decode(span<const uint8_t> data, string &error)
{
  nodes.clear();
  bytes = data.size();
  if (data.size() < kHeaderSize) {
    error = string("encoded source file shorter than its header");
    return false;
  }
  // 7.0.2 stores the version in the high byte of the first word, while encoder.go
  // documents byte 0; whichever byte is set is the version.
  protocolVersion = data[0] ? data[0] : data[3];
  contentHashLow = readU64(data, 4);
  contentHashHigh = readU64(data, 12);
  size_t nodesOffset = readU32(data, kNodesOffsetField);
  if (nodesOffset < kHeaderSize || nodesOffset > data.size() ||
      (data.size() - nodesOffset) % kNodeSize != 0)
  {
    char buf[96];
    snprintf(buf,
             sizeof buf,
             "node table offset %zu does not fit %zu bytes",
             nodesOffset,
             data.size());
    error = string(buf);
    return false;
  }
  size_t count = (data.size() - nodesOffset) / kNodeSize;
  nodes.ensure_capacity(count);
  for (size_t i = 0; i < count; i++) {
    size_t at = nodesOffset + i * kNodeSize;
    EncodedNode n;
    n.kind = readU32(data, at);
    n.pos = readU32(data, at + 4);
    n.end = readU32(data, at + 8);
    n.next = readU32(data, at + 12);
    n.parent = readU32(data, at + 16);
    n.data = readU32(data, at + 20);
    n.flags = readU32(data, at + 24);
    nodes.append(n);
  }
  return true;
}

int EncodedSourceFile::findNode(uint32_t pos, uint32_t end, uint32_t kind) const
{
  int best = -1;
  uint32_t bestWidth = 0;
  for (int i = 0; i < int(nodes.size()); i++) {
    const EncodedNode &n = nodes[i];
    if (n.kind == 0 || n.kind == kNodeListKind) {
      continue;
    }
    if (n.pos > pos || n.end < end) {
      continue;
    }
    if (kind != 0 && n.kind != kind) {
      continue;
    }
    // Equal widths are an ancestor and its only child; table order is preorder, so the
    // later one is the deeper.
    uint32_t width = n.end - n.pos;
    if (best < 0 || width <= bestWidth) {
      best = i;
      bestWidth = width;
    }
  }
  return best;
}

string nodeHandle(int index, uint32_t kind, std::string_view canonicalPath)
{
  char buf[48];
  snprintf(buf, sizeof buf, "%d.%u.", index, kind);
  string out(buf);
  for (char c : canonicalPath) {
    out += c;
  }
  return out;
}

string canonicalPath(std::string_view path, bool caseSensitive)
{
  string out;
  for (char c : path) {
    if (c == '\\') {
      c = '/';
    } else if (!caseSensitive && c >= 'A' && c <= 'Z') {
      c = char(c - 'A' + 'a');
    }
    out += c;
  }
  return out;
}

// ---------------------------------------------------------------- NodeIndexTable

void NodeIndexTable::clear()
{
  m_indices.clear();
  m_encoded = nullptr;
  m_mapped = 0;
}

void NodeIndexTable::build(const ast::AstFile &file, const EncodedSourceFile &encoded)
{
  clear();
  m_encoded = &encoded;

  // Real nodes sorted by end, then by pos, then by table order: every group of candidates
  // for one span ends with the innermost node.
  Vector<int> order;
  order.ensure_capacity(encoded.nodes.size());
  for (int i = 0; i < int(encoded.nodes.size()); i++) {
    const EncodedNode &n = encoded.nodes[i];
    if (n.kind != 0 && n.kind != kNodeListKind && n.end > n.pos) {
      order.append(i);
    }
  }
  const Vector<EncodedNode> &nodes = encoded.nodes;
  order.sort([&](int a, int b) -> int {
    const EncodedNode &na = nodes[a];
    const EncodedNode &nb = nodes[b];
    if (na.end != nb.end) {
      return na.end < nb.end ? -1 : 1;
    }
    if (na.pos != nb.pos) {
      return na.pos < nb.pos ? -1 : 1;
    }
    return a - b;
  });

  Utf16Offsets utf16;
  if (file.grammar()) {
    utf16.build(file.grammar()->source());
  }

  // Same-span ancestors of a node precede it in our preorder, as they do in tsgo's table,
  // so the k-th of ours with a span takes the k-th tsgo node with that span.
  uint32_t runStart = 0, runEnd = 0;
  int runCount = 0;
  for (const ast::PreorderEntry &entry : file.preorder()) {
    const ast::Node *node = entry.node;
    if (!node || node->end <= node->start || node->kind == ast::NodeKind::Error) {
      continue;
    }
    if (node->start == runStart && node->end == runEnd) {
      runCount++;
    } else {
      runStart = node->start;
      runEnd = node->end;
      runCount = 0;
    }
    uint32_t start = utf16.at(node->start);
    uint32_t end = utf16.at(node->end);

    int lo = 0, hi = int(order.size());
    while (lo < hi) {
      int mid = (lo + hi) / 2;
      if (nodes[order[mid]].end < end) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    // The tightest candidates share the largest pos not past our start; a tsgo pos
    // includes leading trivia, so it is at most our start.
    int groupStart = -1, groupCount = 0;
    uint32_t groupPos = 0;
    for (int i = lo; i < int(order.size()) && nodes[order[i]].end == end; i++) {
      uint32_t pos = nodes[order[i]].pos;
      if (pos > start) {
        break;
      }
      if (groupStart < 0 || pos != groupPos) {
        groupStart = i;
        groupCount = 1;
        groupPos = pos;
      } else {
        groupCount++;
      }
    }
    if (groupStart >= 0) {
      int pick = runCount < groupCount ? runCount : groupCount - 1;
      m_indices.add_overwrite(node, order[groupStart + pick]);
      m_mapped++;
    }
  }
}

void Utf16Offsets::build(std::string_view source)
{
  clear();
  uint32_t delta = 0;
  size_t i = 0;
  // The server drops a UTF-8 byte order mark before scanning; our scanner keeps it as
  // whitespace, so it is three bytes that count for nothing.
  if (source.size() >= 3 && source.substr(0, 3) == "\xef\xbb\xbf") {
    delta = 3;
    m_marks.append(Mark{0, 3, delta});
    i = 3;
  }
  for (; i < source.size();) {
    unsigned char lead = static_cast<unsigned char>(source[i]);
    if (lead < 0x80) {
      i++;
      continue;
    }
    // Two- and three-byte sequences are one UTF-16 unit; four-byte ones are a surrogate
    // pair. A stray continuation byte counts as one unit, as the server's decoder does.
    uint32_t bytes = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
    if (i + bytes > source.size()) {
      bytes = uint32_t(source.size() - i);
    }
    uint32_t units = bytes == 4 ? 2 : 1;
    delta += bytes - units;
    m_marks.append(Mark{uint32_t(i), bytes, delta});
    i += bytes;
  }
}

void Utf16Offsets::clear()
{
  m_marks.clear();
}

uint32_t Utf16Offsets::at(uint32_t byteOffset) const
{
  // The last character starting at or before the offset decides the delta.
  int lo = 0, hi = int(m_marks.size());
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (m_marks[mid].start <= byteOffset) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo == 0) {
    return byteOffset;
  }
  const Mark &mark = m_marks[lo - 1];
  uint32_t deltaBefore = lo >= 2 ? m_marks[lo - 2].deltaAfter : 0;
  if (byteOffset < mark.start + mark.bytes) {
    return mark.start - deltaBefore;
  }
  return byteOffset - mark.deltaAfter;
}

int NodeIndexTable::lookup(const ast::Node *node) const
{
  const int *found =
      const_cast<Map<const ast::Node *, int> &>(m_indices).lookup_ptr(node);
  return found ? *found : -1;
}

string NodeIndexTable::handle(const ast::Node *node, std::string_view path) const
{
  int index = lookup(node);
  if (index < 0 || !m_encoded) {
    return string();
  }
  return nodeHandle(index, m_encoded->nodes[index].kind, path);
}

} // namespace fastlint::tsgo
