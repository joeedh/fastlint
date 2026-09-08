// The N-API addon (docs/tasklists/MASTER.md task 7.2). It exposes the core over
// the C N-API directly rather than through node-addon-api, because the plugin
// ABI the rest of the embedding work is built on is already a C surface and
// exceptions are off in this tree.
//
// Two surfaces: `lintText`, the whole recommended preset in one call, and the
// node accessors (`parse`, `kind`, `child`, ...) the generated TypeScript
// runtime (plugin/generated/ts/views.ts) calls to let a TS rule read the tree.

#include "fastlint/ast/access.h"
#include "fastlint/ast/node.h"
#include "fastlint/embed/ast_session.h"
#include "fastlint/embed/lint_text.h"
#include "fastlint/version.h"

#include "util/alloc.h"
#include "util/string.h"
#include "util/vector.h"

#include <node_api.h>

#include <string_view>

namespace {

using fastlint::embed::AstSession;
using litestl::util::string;
using litestl::util::Vector;
using Node = fastlint::ast::Node;

/** Throws a JS TypeError and returns null, which N-API reads as "threw". */
napi_value typeError(napi_env env, const char *message)
{
  napi_throw_type_error(env, nullptr, message);
  return nullptr;
}

/**
 * Copies a JS string out as UTF-8 into `out`, which ends up holding the bytes
 * and a terminator. Returns false when the value is not a string.
 */
bool utf8Arg(napi_env env, napi_value value, Vector<char> &out)
{
  size_t length = 0;
  if (napi_get_value_string_utf8(env, value, nullptr, 0, &length) != napi_ok) {
    return false;
  }
  out.resize(length + 1);
  size_t written = 0;
  if (napi_get_value_string_utf8(env, value, out.data(), length + 1, &written) != napi_ok)
  {
    return false;
  }
  out.resize(written + 1);
  return true;
}

// `buffer` is non-const because litestl's Vector::data() has no const overload.
std::string_view view(Vector<char> &buffer)
{
  return buffer.size() == 0 ? std::string_view()
                            : std::string_view(buffer.data(), buffer.size() - 1);
}

/** Reads up to `max` arguments; returns the count N-API reported. */
size_t args(napi_env env, napi_callback_info info, napi_value *out, size_t max)
{
  size_t argc = max;
  if (napi_get_cb_info(env, info, &argc, out, nullptr, nullptr) != napi_ok) {
    return 0;
  }
  return argc;
}

int32_t int32Arg(napi_env env, napi_value value)
{
  int32_t out = 0;
  napi_get_value_int32(env, value, &out);
  return out;
}

/** The `ast::Node *` an accessor's first argument wraps, or null. */
const Node *nodeArg(napi_env env, napi_value value)
{
  void *data = nullptr;
  if (napi_get_value_external(env, value, &data) != napi_ok) {
    return nullptr;
  }
  return static_cast<const Node *>(data);
}

/** Wraps a node as an external handle, or JS null. Child handles carry no
 * finalizer; the session external owns the tree they point into. */
napi_value nodeHandle(napi_env env, const Node *node)
{
  if (!node) {
    napi_value null = nullptr;
    napi_get_null(env, &null);
    return null;
  }
  napi_value handle = nullptr;
  napi_create_external(env, const_cast<Node *>(node), nullptr, nullptr, &handle);
  return handle;
}

napi_value uint32Value(napi_env env, uint32_t value)
{
  napi_value out = nullptr;
  napi_create_uint32(env, value, &out);
  return out;
}

napi_value version(napi_env env, napi_callback_info)
{
  napi_value out = nullptr;
  napi_create_string_utf8(env, fastlint::version(), NAPI_AUTO_LENGTH, &out);
  return out;
}

/** `lintText(source, filename?)` -> the ESLint-shaped JSON `--format json` prints. */
napi_value lintText(napi_env env, napi_callback_info info)
{
  napi_value argv[2] = {nullptr, nullptr};
  size_t argc = args(env, info, argv, 2);
  if (argc < 1) {
    return typeError(env, "lintText(source, filename?) needs a source string");
  }

  Vector<char> source;
  if (!utf8Arg(env, argv[0], source)) {
    return typeError(env, "source must be a string");
  }

  Vector<char> filename;
  if (argc >= 2) {
    napi_valuetype type = napi_undefined;
    napi_typeof(env, argv[1], &type);
    if (type == napi_string && !utf8Arg(env, argv[1], filename)) {
      return typeError(env, "filename must be a string");
    }
  }
  std::string_view name =
      filename.size() > 1 ? view(filename) : std::string_view("input.ts");

  string json;
  fastlint::embed::lintText(view(source), name, json);

  napi_value out = nullptr;
  napi_create_string_utf8(env, json.c_str(), json.size(), &out);
  return out;
}

/** Frees the session when JS drops the handle `parse` handed back. */
void finalizeSession(napi_env, void *data, void *)
{
  litestl::alloc::Delete(static_cast<AstSession *>(data));
}

/** `parse(source, filename?)` -> a session handle owning the parsed tree. */
napi_value parse(napi_env env, napi_callback_info info)
{
  napi_value argv[2] = {nullptr, nullptr};
  size_t argc = args(env, info, argv, 2);
  if (argc < 1) {
    return typeError(env, "parse(source, filename?) needs a source string");
  }

  Vector<char> source;
  if (!utf8Arg(env, argv[0], source)) {
    return typeError(env, "source must be a string");
  }
  Vector<char> filename;
  if (argc >= 2) {
    napi_valuetype type = napi_undefined;
    napi_typeof(env, argv[1], &type);
    if (type == napi_string && !utf8Arg(env, argv[1], filename)) {
      return typeError(env, "filename must be a string");
    }
  }
  std::string_view name =
      filename.size() > 1 ? view(filename) : std::string_view("input.ts");

  AstSession *session = AstSession::parse(view(source), name);
  napi_value handle = nullptr;
  napi_create_external(env, session, finalizeSession, nullptr, &handle);
  return handle;
}

/** `root(session)` -> the root node handle. */
napi_value root(napi_env env, napi_callback_info info)
{
  napi_value argv[1] = {nullptr};
  args(env, info, argv, 1);
  void *data = nullptr;
  if (napi_get_value_external(env, argv[0], &data) != napi_ok || !data) {
    return typeError(env, "root(session) needs a session handle");
  }
  return nodeHandle(env, static_cast<AstSession *>(data)->root());
}

napi_value kind(napi_env env, napi_callback_info info)
{
  napi_value argv[1] = {nullptr};
  args(env, info, argv, 1);
  const Node *node = nodeArg(env, argv[0]);
  return uint32Value(env, node ? uint32_t(fastlint::ast::access::kind(node)) : 0);
}

napi_value flags(napi_env env, napi_callback_info info)
{
  napi_value argv[1] = {nullptr};
  args(env, info, argv, 1);
  const Node *node = nodeArg(env, argv[0]);
  return uint32Value(env, node ? fastlint::ast::access::flags(node) : 0);
}

napi_value parent(napi_env env, napi_callback_info info)
{
  napi_value argv[1] = {nullptr};
  args(env, info, argv, 1);
  const Node *node = nodeArg(env, argv[0]);
  return nodeHandle(env, node ? fastlint::ast::access::parent(node) : nullptr);
}

napi_value childCount(napi_env env, napi_callback_info info)
{
  napi_value argv[1] = {nullptr};
  args(env, info, argv, 1);
  const Node *node = nodeArg(env, argv[0]);
  return uint32Value(env, node ? uint32_t(fastlint::ast::access::childCount(node)) : 0);
}

napi_value child(napi_env env, napi_callback_info info)
{
  napi_value argv[2] = {nullptr, nullptr};
  if (args(env, info, argv, 2) < 2) {
    return typeError(env, "child(node, index) needs an index");
  }
  const Node *node = nodeArg(env, argv[0]);
  int32_t index = int32Arg(env, argv[1]);
  if (!node || index < 0 || index >= fastlint::ast::access::childCount(node)) {
    return nodeHandle(env, nullptr);
  }
  return nodeHandle(env, fastlint::ast::access::child(node, index));
}

napi_value text(napi_env env, napi_callback_info info)
{
  napi_value argv[1] = {nullptr};
  args(env, info, argv, 1);
  const Node *node = nodeArg(env, argv[0]);
  std::string_view t = node ? fastlint::ast::access::text(node) : std::string_view();
  napi_value out = nullptr;
  napi_create_string_utf8(env, t.data(), t.size(), &out);
  return out;
}

napi_value dataByte(napi_env env, napi_callback_info info)
{
  napi_value argv[2] = {nullptr, nullptr};
  if (args(env, info, argv, 2) < 2) {
    return typeError(env, "dataByte(node, byte) needs a byte index");
  }
  const Node *node = nodeArg(env, argv[0]);
  int32_t byte = int32Arg(env, argv[1]);
  uint32_t value = node ? uint32_t(fastlint::ast::access::dataByte(node, byte)) : 0;
  return uint32Value(env, value);
}

napi_value start(napi_env env, napi_callback_info info)
{
  napi_value argv[1] = {nullptr};
  args(env, info, argv, 1);
  const Node *node = nodeArg(env, argv[0]);
  return uint32Value(env, node ? node->start : 0);
}

napi_value end(napi_env env, napi_callback_info info)
{
  napi_value argv[1] = {nullptr};
  args(env, info, argv, 1);
  const Node *node = nodeArg(env, argv[0]);
  return uint32Value(env, node ? node->end : 0);
}

/** `descendants(session, node, kind)` -> the matching nodes as handles. The
 * session is passed so the walk uses its shared preorder helper. */
napi_value descendants(napi_env env, napi_callback_info info)
{
  napi_value argv[3] = {nullptr, nullptr, nullptr};
  if (args(env, info, argv, 3) < 3) {
    return typeError(env, "descendants(session, node, kind) needs three arguments");
  }
  void *data = nullptr;
  if (napi_get_value_external(env, argv[0], &data) != napi_ok || !data) {
    return typeError(env, "descendants needs a session handle");
  }
  const Node *node = nodeArg(env, argv[1]);
  int32_t kindValue = int32Arg(env, argv[2]);

  Vector<const Node *> found;
  static_cast<AstSession *>(data)->descendants(
      node, fastlint::ast::NodeKind(kindValue), found);

  napi_value out = nullptr;
  napi_create_array_with_length(env, found.size(), &out);
  for (size_t i = 0; i < found.size(); i++) {
    napi_set_element(env, out, uint32_t(i), nodeHandle(env, found[int(i)]));
  }
  return out;
}

napi_value method(napi_env env, const char *name, napi_callback fn)
{
  napi_value value = nullptr;
  napi_create_function(env, name, NAPI_AUTO_LENGTH, fn, nullptr, &value);
  return value;
}

napi_value init(napi_env env, napi_value exports)
{
  const struct {
    const char *name;
    napi_callback fn;
  } table[] = {
      {"version", version},
      {"lintText", lintText},
      {"parse", parse},
      {"root", root},
      {"kind", kind},
      {"flags", flags},
      {"parent", parent},
      {"childCount", childCount},
      {"child", child},
      {"text", text},
      {"dataByte", dataByte},
      {"start", start},
      {"end", end},
      {"descendants", descendants},
  };
  for (const auto &entry : table) {
    napi_set_named_property(env, exports, entry.name, method(env, entry.name, entry.fn));
  }
  return exports;
}

} // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
