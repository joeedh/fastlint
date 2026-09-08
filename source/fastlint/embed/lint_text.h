#pragma once

// The one call the N-API addon and the WASM module are both built around: lint
// a buffer and hand back the ESLint-shaped JSON `--format json` prints. Neither
// embedding has a filesystem it can look a config up in, so the recommended
// preset applies and only the syntactic rules run.

#include "util/string.h"

#include <string_view>

namespace fastlint::embed {

using litestl::util::string;

/**
 * Lints `source` as if it were saved at `filename`, whose extension selects the
 * parser options. `out` receives a one-element JSON array. Type-aware rules are
 * skipped: typing a file needs a tsgo process and a tsconfig, and an embedding
 * has neither.
 */
void lintText(std::string_view source, std::string_view filename, string &out);

} // namespace fastlint::embed
