#include "fastlint/rules/rules.h"

namespace fastlint::rules {

void addBuiltinRules(lint::Registry &registry)
{
  registry.add(kNoDebugger);
}

} // namespace fastlint::rules
