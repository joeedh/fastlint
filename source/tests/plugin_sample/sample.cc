// A sample native plugin, built against the plugin headers only (no litestl,
// no host library). It compiles the generated C++ views in plugin mode and
// contributes one rule, `sample/no-foo`, that renames the identifier `foo` to
// `bar` through a template fix. The test harness loads it from disk.

#include "fastlint/ast/generated/views.h"
#include "fastlint/plugin/abi.h"
#include "fastlint/plugin/generated/ast.h"

#ifdef _WIN32
#define FL_EXPORT __declspec(dllexport)
#else
#define FL_EXPORT __attribute__((visibility("default")))
#endif

// The host table every access:: read and fixer op routes through.
namespace fastlint::ast {
const fl_host_api *g_flHost = nullptr;
}

namespace {

using fastlint::ast::g_flHost;

// Runs inside the host's fix pass: build `bar` from a template and swap it in.
void renameToBar(fl_fixer *fixer, void *userdata)
{
  fl_node *node = static_cast<fl_node *>(userdata);
  fl_str source{"bar", 3};
  const fl_template *bar =
      g_flHost->template_compile(source, /*Template::Mode::Expression*/ 1, /*jsx=*/0);
  fl_node *fresh = g_flHost->fixer_instantiate(fixer, bar, nullptr, 0);
  if (fresh) {
    g_flHost->fixer_replace(fixer, node, fresh);
  }
}

void checkIdentifier(fl_node *node, void * /*userdata*/, fl_report *report)
{
  fastlint::ast::Identifier id(reinterpret_cast<fastlint::ast::Node *>(node));
  if (id.text() == "foo") {
    g_flHost->report_fix(report, node, "renameFoo", renameToBar, node);
  }
}

const fl_message kMessages[] = {{"renameFoo", "Rename 'foo' to 'bar'."}};
const uint16_t kKinds[] = {fl_kind_Identifier};
const fl_rule kRules[] = {
    {"sample/no-foo", kMessages, 1, kKinds, 1, checkIdentifier, nullptr, /*fixable=*/1},
};
const fl_plugin kPlugin = {FL_ABI_VERSION, FL_NODES_DEF_HASH, kRules, 1};

} // namespace

extern "C" FL_EXPORT const fl_plugin *fastlint_plugin_init(const fl_host_api *host)
{
  if (host->abi_version != FL_ABI_VERSION) {
    return nullptr;
  }
  g_flHost = host;
  return &kPlugin;
}
