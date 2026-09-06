#pragma once

// Per-kind layout and name tables, implemented in generated/tables.cc. The
// dump, the binding layer and generic tooling read these instead of views.

#include "fastlint/ast/generated/kinds.h"

#include <cstdint>

namespace fastlint::ast {

struct EnumField {
  const char *name;
  const char *const *values;
  int count;
};

struct KindInfo {
  const char *name;
  /** Slots before the list; optional slots count. */
  uint8_t fixedChildren;
  bool hasList;
  /** The list may hold nullptr elements (array holes). */
  bool nullableElements;
  bool usesText;
  /** One name per fixed slot, then the list name. */
  const char *const *childNames;
  uint8_t childCount;
  /** Enum fields in Node::data byte order. */
  const EnumField *enums;
  uint8_t enumCount;
  /** Every flag bit the kind may set. */
  uint32_t flagMask;
  /** Bit i set when fixed slot i must hold a node. */
  uint32_t requiredMask;
};

const KindInfo &kindInfo(NodeKind kind);
const char *kindName(NodeKind kind);
const char *flagName(int bit);

} // namespace fastlint::ast
