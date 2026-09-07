#pragma once

#include "util/map.h"
#include "util/span.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

namespace fastlint::ast {
class AstFile;
struct Node;
} // namespace fastlint::ast

namespace fastlint::tsgo {

using litestl::util::Map;
using litestl::util::span;
using litestl::util::Vector;
using string = litestl::util::string;

/** Marks a node-list pseudo-entry in the encoded node table. */
constexpr uint32_t kNodeListKind = 0xFFFFFFFF;

/** One record of the flat node table `getSourceFile` returns; spans are UTF-16 code unit
 * offsets, as everywhere in TypeScript, and `pos` includes leading trivia
 * (tsc/internal/api/encoder/encoder.go). Our scanner counts UTF-8 bytes; `Utf16Offsets`
 * converts. */
struct EncodedNode {
  uint32_t kind = 0;
  uint32_t pos = 0;
  uint32_t end = 0;
  uint32_t next = 0;
  uint32_t parent = 0;
  uint32_t data = 0;
  uint32_t flags = 0;
};

/** The header and node table of an encoded source file. Node indices are the ones a
 * `NodeHandle` carries. */
struct EncodedSourceFile {
  uint32_t protocolVersion = 0;
  uint64_t contentHashLow = 0;
  uint64_t contentHashHigh = 0;
  size_t bytes = 0;
  Vector<EncodedNode> nodes;

  /** Decodes `data`; false with `error` set when the header or node section is malformed.
   */
  bool decode(span<const uint8_t> data, string &error);

  /** Index of the narrowest real node covering `[pos, end)`, restricted to `kind` unless
   * it is 0. Returns -1 when nothing covers the span. */
  int findNode(uint32_t pos, uint32_t end, uint32_t kind = 0) const;
};

/** The handle string the server resolves back to a node: `index.kind.path`. */
string nodeHandle(int index, uint32_t kind, std::string_view canonicalPath);

/** Path form the server uses in handles: forward slashes, lower-cased on a
 * case-insensitive file system. */
string canonicalPath(std::string_view path, bool caseSensitive);

/** Converts UTF-8 byte offsets in a source text to the server's UTF-16 code unit
 * offsets, which also skip a leading byte order mark. Only the non-ASCII characters are
 * recorded, so an ASCII file costs nothing. */
class Utf16Offsets {
public:
  void build(std::string_view source);
  void clear();
  /** The UTF-16 offset of the character starting at `byteOffset`; an offset inside a
   * multi-byte character maps to that character's start. */
  uint32_t at(uint32_t byteOffset) const;
  bool ascii() const
  {
    return m_marks.size() == 0;
  }

private:
  /** One multi-byte character. */
  struct Mark {
    uint32_t start;
    uint32_t bytes;
    /** Bytes minus code units over every multi-byte character up to and including this
     * one. */
    uint32_t deltaAfter;
  };
  Vector<Mark> m_marks;
};

/** Maps our AST nodes to tsgo node indices for one file. Rebuilt whenever the file's
 * content changes, since both tables are keyed by offset. */
class NodeIndexTable {
public:
  /** Pairs every node of `file` whose span an encoded node shares, after converting our
   * byte offsets to the server's UTF-16 units. Nodes without a counterpart (zero-width,
   * error, or shaped differently by our parser) are left unmapped. */
  void build(const ast::AstFile &file, const EncodedSourceFile &encoded);
  void clear();

  /** The tsgo index for `node`, or -1 when unmapped. */
  int lookup(const ast::Node *node) const;
  /** Handle for `node` in `canonicalPath`, or an empty string when unmapped. */
  string handle(const ast::Node *node, std::string_view canonicalPath) const;
  int mappedCount() const
  {
    return m_mapped;
  }

private:
  Map<const ast::Node *, int> m_indices;
  const EncodedSourceFile *m_encoded = nullptr;
  int m_mapped = 0;
};

} // namespace fastlint::tsgo
