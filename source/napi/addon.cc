// The N-API addon (docs/tasklists/MASTER.md task 7.2). It exposes the core over
// the C N-API directly rather than through node-addon-api, because the plugin
// ABI the rest of the embedding work is built on is already a C surface and
// exceptions are off in this tree.

#include "fastlint/embed/lint_text.h"
#include "fastlint/version.h"

#include "util/string.h"
#include "util/vector.h"

#include <node_api.h>

#include <string_view>

namespace {

using litestl::util::string;
using litestl::util::Vector;

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

napi_value version(napi_env env, napi_callback_info)
{
  napi_value out = nullptr;
  napi_create_string_utf8(env, fastlint::version(), NAPI_AUTO_LENGTH, &out);
  return out;
}

/** `lintText(source, filename?)` -> the ESLint-shaped JSON `--format json` prints. */
napi_value lintText(napi_env env, napi_callback_info info)
{
  size_t argc = 2;
  napi_value argv[2] = {nullptr, nullptr};
  if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok) {
    return typeError(env, "could not read the arguments");
  }
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
  // The extension picks the parser options, so an unnamed buffer needs a
  // default rather than an empty name.
  std::string_view name =
      filename.size() > 1 ? view(filename) : std::string_view("input.ts");

  string json;
  fastlint::embed::lintText(view(source), name, json);

  napi_value out = nullptr;
  napi_create_string_utf8(env, json.c_str(), json.size(), &out);
  return out;
}

napi_value init(napi_env env, napi_value exports)
{
  const napi_property_descriptor properties[] = {
      {"version", nullptr, version, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"lintText", nullptr, lintText, nullptr, nullptr, nullptr, napi_default, nullptr},
  };
  napi_define_properties(
      env, exports, sizeof(properties) / sizeof(properties[0]), properties);
  return exports;
}

} // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
