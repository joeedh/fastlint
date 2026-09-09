#pragma once

// The set of rules a linter knows, looked up by configured name. ESLint
// plugin prefixes (`@typescript-eslint/`) are accepted and dropped so
// existing configs and disable comments resolve.

#include "fastlint/lint/rule.h"
#include "util/vector.h"

#include <string_view>

namespace fastlint::lint {

class Registry {
public:
  void add(const RuleDef &rule)
  {
    m_rules.append(&rule);
  }
  /** The rule `name` configures, or null; `name` may carry a known plugin prefix. */
  const RuleDef *find(string_view name) const;
  span<const RuleDef *const> rules() const
  {
    return {const_cast<Vector<const RuleDef *> &>(m_rules).data(), m_rules.size()};
  }
  int size() const
  {
    return int(m_rules.size());
  }

  /** `name` without a plugin prefix this registry recognizes. */
  static string_view canonical(string_view name);
  /** True when `name` is a prefix `canonical` strips, which a config's
   * `plugins` may not claim. */
  static bool isAliasPrefix(string_view name);

private:
  Vector<const RuleDef *> m_rules;
};

/** Every rule built into fastlint (source/fastlint/rules/). */
const Registry &builtinRegistry();

} // namespace fastlint::lint
