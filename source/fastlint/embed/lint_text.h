#pragma once

// The one call the N-API addon and the WASM module are both built around: lint
// a buffer and hand back the ESLint-shaped JSON `--format json` prints. Neither
// embedding has a filesystem it can look a config up in, so the recommended
// preset applies unless the host passes a config document of its own, and only
// the syntactic rules run.

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

/**
 * Lints `source` with `configJson`, a `fastlint.config.json` document the host
 * read, whose globs are anchored at `baseDir` as they would be on disk. The
 * document is parsed once and reused while the host keeps passing the same one,
 * since a run lints many buffers against one config. An empty `configJson` is
 * the recommended preset, and a document that will not parse leaves `error`
 * filled and `out` empty.
 */
void lintTextWithConfig(std::string_view source,
                        std::string_view filename,
                        std::string_view configJson,
                        std::string_view baseDir,
                        string &out,
                        string &error);

} // namespace fastlint::embed
