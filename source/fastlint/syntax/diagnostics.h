#pragma once

#include "util/string.h"
#include "util/vector.h"

#include <cstdint>

namespace fastlint::syntax {

using litestl::util::string;
using litestl::util::Vector;

/** One parse or scan error. Offsets are byte offsets into the source. */
struct Diagnostic {
  /** TS-compatible message code where one exists, 0 otherwise. */
  uint32_t code = 0;
  uint32_t offset = 0;
  uint32_t length = 0;
  string message;
};

/** Collects diagnostics in source order. */
class Diagnostics {
public:
  void report(uint32_t code, uint32_t offset, uint32_t length, string message)
  {
    m_items.append(Diagnostic{code, offset, length, std::move(message)});
  }

  const Vector<Diagnostic> &items() const
  {
    return m_items;
  }
  bool empty() const
  {
    return m_items.isEmpty();
  }
  size_t size() const
  {
    return m_items.size();
  }
  void resize(size_t count)
  {
    m_items.resize(count);
  }
  void clear()
  {
    m_items.clear();
  }

private:
  Vector<Diagnostic> m_items;
};

} // namespace fastlint::syntax
