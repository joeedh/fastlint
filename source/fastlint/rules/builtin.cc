#include "fastlint/rules/rules.h"

namespace fastlint::rules {

void addBuiltinRules(lint::Registry &registry)
{
  registry.add(kArrayType);
  registry.add(kCurly);
  registry.add(kEqeqeq);
  registry.add(kNoConsole);
  registry.add(kNoConstantCondition);
  registry.add(kNoDebugger);
  registry.add(kNoDuplicateCase);
  registry.add(kNoEmpty);
  registry.add(kNoFallthrough);
  registry.add(kNoNonNullAssertion);
  registry.add(kNoSelfAssign);
  registry.add(kNoShadow);
  registry.add(kNoUnreachable);
  registry.add(kNoVar);
  registry.add(kPreferAsConst);
  registry.add(kPreferConst);
}

} // namespace fastlint::rules
