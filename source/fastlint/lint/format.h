#pragma once

// Result formatters (docs/rules.md "Output"): the terminal listing and the
// ESLint-shaped JSON array.

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

} // namespace fastlint::lint
