#pragma once

// Serializes a file's lint result to and from the opaque payload the rule-result
// cache stores (docs/rules.md "Result cache"). One payload per file holds every
// diagnostic, so a fresh file replays its result without running the rules.

#include "fastlint/lint/linter.h"
#include "fastlint/lint/registry.h"
#include "util/string.h"

namespace fastlint::lint {

/** Encodes `result`'s diagnostics and counts as a JSON payload. */
void serializeResult(const FileResult &result, string &out);

/**
 * Fills `out` from a payload `serializeResult` wrote. Rule names resolve
 * through `registry`; a message id resolves to its rule's static string.
 * Returns false on a malformed payload, which the caller treats as a miss.
 */
bool deserializeResult(string_view payload, const Registry &registry, FileResult &out);

} // namespace fastlint::lint
