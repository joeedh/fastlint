#pragma once

// The built-in rules, one per file in this directory (docs/rules.md "Adding a
// rule"). Each rule file defines its `RuleDef`; builtin.cc lists them.

#include "fastlint/lint/registry.h"
#include "fastlint/lint/rule.h"

namespace fastlint::rules {

extern const lint::RuleDef kNoDebugger;

/** Registers every built-in rule with `registry`. */
void addBuiltinRules(lint::Registry &registry);

} // namespace fastlint::rules
