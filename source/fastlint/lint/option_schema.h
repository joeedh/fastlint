#pragma once

// Validates a rule's configured options against its ESLint-shaped JSON Schema
// (`RuleMeta::schema`) at config load (docs/rules.md "Option schema").

#include "fastlint/lint/rule.h"
#include "fastlint/tsgo/json.h"
#include "util/string.h"

namespace fastlint::lint {

/**
 * Checks a rule's configured options against `rule.meta.schema`. `setting` is
 * the `[severity, ...options]` array the rule is configured with. Passes when
 * the rule has no schema or when `setting` carries only a severity. On a
 * mismatch returns false and fills `error` with a `config: rule "name"
 * option ...` message.
 */
bool validateOptions(const RuleDef &rule, const JsonValue *setting, string &error);

} // namespace fastlint::lint
