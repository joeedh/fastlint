#pragma once

// Unicode identifier tables (ID_Start / ID_Continue), generated from Go's
// unicode package (the same tables tsgo's scanner sees on Linux):
//   ID_Start  = L* + Nl + Other_ID_Start
//   ID_Continue = ID_Start + Mn + Mc + Nd + Pc + Other_ID_Continue
// Binary search over sorted code-point ranges.

#include "util/span.h"

#include <cstdint>

namespace fastlint::syntax {

struct CodeRange {
  uint32_t lo;
  uint32_t hi;
};

bool idStartContains(uint32_t cp);
bool idContinueContains(uint32_t cp);

} // namespace fastlint::syntax
