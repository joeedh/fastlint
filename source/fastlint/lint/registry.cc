#include "fastlint/lint/registry.h"

#include "fastlint/rules/rules.h"

namespace fastlint::lint {

namespace {

constexpr string_view kPrefixes[] = {"@typescript-eslint/", "typescript-eslint/"};

} // namespace

string_view Registry::canonical(string_view name)
{
  for (string_view prefix : kPrefixes) {
    if (name.size() > prefix.size() && name.substr(0, prefix.size()) == prefix) {
      return name.substr(prefix.size());
    }
  }
  return name;
}

const RuleDef *Registry::find(string_view name) const
{
  string_view wanted = canonical(name);
  for (const RuleDef *rule : m_rules) {
    if (wanted == rule->meta.name) {
      return rule;
    }
  }
  return nullptr;
}

const Registry &builtinRegistry()
{
  static Registry registry = [] {
    litestl::alloc::PermanentGuard guard;
    Registry r;
    rules::addBuiltinRules(r);
    return r;
  }();
  return registry;
}

} // namespace fastlint::lint
