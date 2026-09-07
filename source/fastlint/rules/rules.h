#pragma once

// The built-in rules, one per file in this directory (docs/rules.md "Adding a
// rule"). Each rule file defines its `RuleDef`; builtin.cc lists them.

#include "fastlint/lint/registry.h"
#include "fastlint/lint/rule.h"

#include <cstddef>

namespace fastlint::rules {

/** The message table of a rule as a span. */
template <size_t N>
inline lint::span<const lint::Message> messagesOf(const lint::Message (&m)[N])
{
  return lint::span<const lint::Message>(m, N);
}

extern const lint::RuleDef kArrayType;
extern const lint::RuleDef kAwaitThenable;
extern const lint::RuleDef kCurly;
extern const lint::RuleDef kEqeqeq;
extern const lint::RuleDef kNoConsole;
extern const lint::RuleDef kNoConstantCondition;
extern const lint::RuleDef kNoDebugger;
extern const lint::RuleDef kNoDuplicateCase;
extern const lint::RuleDef kNoEmpty;
extern const lint::RuleDef kNoFallthrough;
extern const lint::RuleDef kNoFloatingPromises;
extern const lint::RuleDef kNoNonNullAssertion;
extern const lint::RuleDef kNoSelfAssign;
extern const lint::RuleDef kNoShadow;
extern const lint::RuleDef kNoUnsafeArgument;
extern const lint::RuleDef kNoUnsafeAssignment;
extern const lint::RuleDef kNoUnsafeCall;
extern const lint::RuleDef kNoUnsafeMemberAccess;
extern const lint::RuleDef kNoUnsafeReturn;
extern const lint::RuleDef kNoUnusedVars;
extern const lint::RuleDef kConsistentTypeImports;
extern const lint::RuleDef kNoUnreachable;
extern const lint::RuleDef kNoVar;
extern const lint::RuleDef kPreferAsConst;
extern const lint::RuleDef kPreferConst;
extern const lint::RuleDef kPreferNullishCoalescing;
extern const lint::RuleDef kRestrictTemplateExpressions;

/** Registers every built-in rule with `registry`. */
void addBuiltinRules(lint::Registry &registry);

} // namespace fastlint::rules
