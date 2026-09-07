#include "fastlint/rules/rules.h"

namespace fastlint::rules {

void addBuiltinRules(lint::Registry &registry)
{
  registry.add(kArrayType);
  registry.add(kAwaitThenable);
  registry.add(kCurly);
  registry.add(kEqeqeq);
  registry.add(kNoConsole);
  registry.add(kNoConstantCondition);
  registry.add(kNoDebugger);
  registry.add(kNoDuplicateCase);
  registry.add(kNoEmpty);
  registry.add(kNoFallthrough);
  registry.add(kNoFloatingPromises);
  registry.add(kNoNonNullAssertion);
  registry.add(kNoSelfAssign);
  registry.add(kNoShadow);
  registry.add(kNoUnsafeArgument);
  registry.add(kNoUnsafeAssignment);
  registry.add(kNoUnsafeCall);
  registry.add(kNoUnsafeMemberAccess);
  registry.add(kNoUnsafeReturn);
  registry.add(kNoUnusedVars);
  registry.add(kConsistentTypeImports);
  registry.add(kNoUnreachable);
  registry.add(kNoVar);
  registry.add(kPreferAsConst);
  registry.add(kPreferConst);
  registry.add(kPreferNullishCoalescing);
  registry.add(kRestrictTemplateExpressions);
  registry.add(kNoUnnecessaryTypeAssertion);
}

} // namespace fastlint::rules
