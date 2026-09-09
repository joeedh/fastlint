// The WASM module's C entry points (docs/tasklists/MASTER.md task 7.3). They
// are plain C functions over char pointers rather than an embind surface, so
// the JS side is the same shape as the N-API addon's and needs no C++ glue in
// the generated wrapper.

#include "fastlint/ast/access.h"
#include "fastlint/ast/node.h"
#include "fastlint/embed/ast_session.h"
#include "fastlint/embed/lint_text.h"
#include "fastlint/version.h"

#include "util/alloc.h"
#include "util/vector.h"

#include <emscripten/emscripten.h>

#include <cstdint>
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

// The node accessors, the WASM twin of the addon's. Handles are plain heap
// pointers, which cross to JS as numbers; the generated `Host` reads through
// them exactly as it reads through the N-API externals. The session owns the
// tree, so a node handle stays valid until `fl_wasm_session_free`.

using fastlint::ast::Node;
using fastlint::embed::AstSession;

EMSCRIPTEN_KEEPALIVE AstSession *fl_wasm_parse(const char *source, const char *filename)
{
  return AstSession::parse(
      std::string_view(source ? source : ""),
      std::string_view(filename && *filename ? filename : "input.ts"));
}

EMSCRIPTEN_KEEPALIVE void fl_wasm_session_free(AstSession *session)
{
  litestl::alloc::Delete(session);
}

EMSCRIPTEN_KEEPALIVE const Node *fl_wasm_root(AstSession *session)
{
  return session ? session->root() : nullptr;
}

EMSCRIPTEN_KEEPALIVE int fl_wasm_kind(const Node *node)
{
  return node ? int(fastlint::ast::access::kind(node)) : 0;
}

EMSCRIPTEN_KEEPALIVE uint32_t fl_wasm_flags(const Node *node)
{
  return node ? fastlint::ast::access::flags(node) : 0;
}

EMSCRIPTEN_KEEPALIVE const Node *fl_wasm_parent(const Node *node)
{
  return node ? fastlint::ast::access::parent(node) : nullptr;
}

EMSCRIPTEN_KEEPALIVE int fl_wasm_child_count(const Node *node)
{
  return node ? fastlint::ast::access::childCount(node) : 0;
}

EMSCRIPTEN_KEEPALIVE const Node *fl_wasm_child(const Node *node, int index)
{
  if (!node || index < 0 || index >= fastlint::ast::access::childCount(node)) {
    return nullptr;
  }
  return fastlint::ast::access::child(node, index);
}

// Text is a slice of the source, not NUL-terminated, so JS reads `len` bytes
// from `ptr` off the heap rather than through UTF8ToString.
EMSCRIPTEN_KEEPALIVE const char *fl_wasm_text_ptr(const Node *node)
{
  return node ? fastlint::ast::access::text(node).data() : nullptr;
}

EMSCRIPTEN_KEEPALIVE int fl_wasm_text_len(const Node *node)
{
  return node ? int(fastlint::ast::access::text(node).size()) : 0;
}

EMSCRIPTEN_KEEPALIVE int fl_wasm_data_byte(const Node *node, int byte)
{
  return node ? int(fastlint::ast::access::dataByte(node, byte)) : 0;
}

EMSCRIPTEN_KEEPALIVE uint32_t fl_wasm_start(const Node *node)
{
  return node ? node->start : 0;
}

EMSCRIPTEN_KEEPALIVE uint32_t fl_wasm_end(const Node *node)
{
  return node ? node->end : 0;
}

/**
 * The descendants of `node` of kind `kind`, as a `malloc`ed array of handles
 * whose length is written to `*outCount`. The caller reads the array off the
 * heap and hands it back to `fl_wasm_free`. Returns null (and count zero) on an
 * allocation failure or an empty result.
 */
EMSCRIPTEN_KEEPALIVE const Node **
fl_wasm_descendants(AstSession *session, const Node *node, int kind, int *outCount)
{
  litestl::util::Vector<const Node *> found;
  if (session) {
    session->descendants(node, fastlint::ast::NodeKind(kind), found);
  }
  if (outCount) {
    *outCount = int(found.size());
  }
  if (found.size() == 0) {
    return nullptr;
  }
  const Node **out =
      static_cast<const Node **>(std::malloc(found.size() * sizeof(const Node *)));
  if (out) {
    std::memcpy(out, found.data(), found.size() * sizeof(const Node *));
  } else if (outCount) {
    *outCount = 0;
  }
  return out;
}

} // extern "C"
