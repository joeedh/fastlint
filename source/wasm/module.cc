// The WASM module's C entry points (docs/tasklists/MASTER.md task 7.3). They
// are plain C functions over char pointers rather than an embind surface, so
// the JS side is the same shape as the N-API addon's and needs no C++ glue in
// the generated wrapper.

#include "fastlint/embed/lint_text.h"
#include "fastlint/version.h"

#include <emscripten/emscripten.h>

#include <cstdlib>
#include <cstring>
#include <string_view>

extern "C" {

EMSCRIPTEN_KEEPALIVE const char *fl_wasm_version(void)
{
  return fastlint::version();
}

/**
 * Lints `source` as if it were saved at `filename` and returns the JSON as a
 * NUL-terminated buffer the caller passes back to `fl_wasm_free`. Returns null
 * when the allocation fails.
 */
EMSCRIPTEN_KEEPALIVE char *fl_wasm_lint(const char *source, const char *filename)
{
  litestl::util::string json;
  fastlint::embed::lintText(
      std::string_view(source ? source : ""),
      std::string_view(filename && *filename ? filename : "input.ts"),
      json);

  // malloc rather than the litestl allocator: the buffer crosses into JS and
  // comes back through fl_wasm_free, so it must match what the JS heap views
  // and _free() understand.
  char *out = static_cast<char *>(std::malloc(json.size() + 1));
  if (out) {
    std::memcpy(out, json.c_str(), json.size() + 1);
  }
  return out;
}

EMSCRIPTEN_KEEPALIVE void fl_wasm_free(char *text)
{
  std::free(text);
}

} // extern "C"
