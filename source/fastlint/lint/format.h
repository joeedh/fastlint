#pragma once

// Result formatters (docs/rules.md "Output"): the terminal listing, the
// ESLint-shaped JSON array and the SARIF 2.1.0 log.

#include "fastlint/lint/linter.h"
#include "util/span.h"
#include "util/string.h"

namespace fastlint::lint {

struct FormatOptions {
  bool color = false;
};

/**
 * One block per file with problems: the path, then `line:col  severity
 * message  rule` rows, then a summary of the counts and how many are fixable.
 */
void formatPretty(span<const FileResult> results,
                  const FormatOptions &options,
                  string &out);

/** An array of `{filePath, messages, errorCount, ...}` objects, one per file. */
void formatJson(span<const FileResult> results, string &out);

/** A SARIF 2.1.0 log: one run whose driver lists the reported rules and whose
 * results carry each diagnostic's level, message and physical location. */
void formatSarif(span<const FileResult> results, string &out);

} // namespace fastlint::lint
