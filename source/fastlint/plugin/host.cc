#include "fastlint/plugin/host.h"

#include "fastlint/ast/access.h"
#include "fastlint/ast/fixer.h"
#include "fastlint/ast/generated/kinds.h"
#include "fastlint/ast/node.h"
#include "fastlint/ast/template.h"
#include "util/alloc.h"

#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// The ABI's opaque handles, completed for the host.
struct fl_report {
  fastlint::lint::RuleContext *ctx;
};
struct fl_fixer {
  fastlint::ast::Fixer *fixer;
};

namespace fastlint::plugin {

namespace {

using namespace fastlint::ast;

// A plugin's fl_message[] is read as Message[], so the layouts must match.
static_assert(sizeof(fl_message) == sizeof(lint::Message));
static_assert(alignof(fl_message) == alignof(lint::Message));

const Node *toNode(const fl_node *n)
{
  return reinterpret_cast<const Node *>(n);
}
Node *toNode(fl_node *n)
{
  return reinterpret_cast<Node *>(n);
}
fl_node *toFl(Node *n)
{
  return reinterpret_cast<fl_node *>(n);
}

extern "C" {

uint16_t hKind(const fl_node *n)
{
  return uint16_t(access::kind(toNode(n)));
}
uint32_t hFlags(const fl_node *n)
{
  return access::flags(toNode(n));
}
int hHasFlag(const fl_node *n, uint32_t flag)
{
  return access::hasFlag(toNode(n), Flag(flag)) ? 1 : 0;
}
uint8_t hDataByte(const fl_node *n, int index)
{
  return access::dataByte(toNode(n), index);
}
fl_str hText(const fl_node *n)
{
  string_view s = access::text(toNode(n));
  return {s.data(), s.size()};
}
fl_node *hParent(const fl_node *n)
{
  return toFl(access::parent(toNode(n)));
}
int hChildCount(const fl_node *n)
{
  return access::childCount(toNode(n));
}
fl_node *hChild(const fl_node *n, int index)
{
  return toFl(access::child(toNode(n), index));
}
fl_nodes hTail(const fl_node *n, int from)
{
  span<Node *> t = access::tail(toNode(n), from);
  return {reinterpret_cast<fl_node *const *>(t.data()), t.size()};
}

void hReport(fl_report *r, const fl_node *node, const char *messageId)
{
  r->ctx->report(const_cast<Node *>(toNode(node)), messageId);
}
void hReportFix(fl_report *r,
                const fl_node *node,
                const char *messageId,
                fl_fix_fn fix,
                void *fixUserdata)
{
  lint::Report report;
  report.node = const_cast<Node *>(toNode(node));
  report.messageId = messageId;
  report.fix = [fix, fixUserdata](ast::Fixer &fixer) {
    fl_fixer wrapper{&fixer};
    fix(&wrapper, fixUserdata);
  };
  r->ctx->report(std::move(report));
}

const fl_template *hTemplateCompile(fl_str text, int mode, int jsx)
{
  const Template *t =
      Template::compile(string_view(text.ptr, text.len), Template::Mode(mode), jsx != 0);
  return reinterpret_cast<const fl_template *>(t);
}

fl_node *
hFixerInstantiate(fl_fixer *f, const fl_template *t, const fl_targ *args, size_t argCount)
{
  const Template *tmpl = reinterpret_cast<const Template *>(t);
  if (!tmpl || !tmpl->ok()) {
    return nullptr;
  }
  TemplateArgs bindings;
  for (size_t i = 0; i < argCount; i++) {
    string_view name(args[i].name.ptr, args[i].name.len);
    if (args[i].node_count == 1) {
      bindings.set(name,
                   reinterpret_cast<Node *>(const_cast<fl_node *>(args[i].nodes[0])));
    } else {
      span<Node *const> nodes(reinterpret_cast<Node *const *>(args[i].nodes),
                              args[i].node_count);
      bindings.set(name, nodes);
    }
  }
  return toFl(tmpl->instantiate(f->fixer->file(), bindings));
}

int hFixerReplace(fl_fixer *f, fl_node *oldNode, fl_node *fresh)
{
  return f->fixer->replace(toNode(oldNode), toNode(fresh)) ? 1 : 0;
}

} // extern "C"

const fl_host_api g_api = {
    FL_ABI_VERSION,
    hKind,
    hFlags,
    hHasFlag,
    hDataByte,
    hText,
    hParent,
    hChildCount,
    hChild,
    hTail,
    hReport,
    hReportFix,
    hTemplateCompile,
    hFixerInstantiate,
    hFixerReplace,
};

#ifdef _WIN32
void *openLibrary(const char *path)
{
  return reinterpret_cast<void *>(LoadLibraryA(path));
}
void *findSymbol(void *handle, const char *name)
{
  return reinterpret_cast<void *>(
      GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
}
void closeLibrary(void *handle)
{
  FreeLibrary(reinterpret_cast<HMODULE>(handle));
}
#else
void *openLibrary(const char *path)
{
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}
void *findSymbol(void *handle, const char *name)
{
  return dlsym(handle, name);
}
void closeLibrary(void *handle)
{
  dlclose(handle);
}
#endif

lint::RuleMeta metaFor(const fl_rule *rule)
{
  lint::RuleMeta meta{};
  meta.name = rule->name;
  meta.description = "";
  meta.docsUrl = "";
  meta.recommended = false;
  meta.fixable = rule->fixable != 0;
  meta.hasSuggestions = false;
  meta.typeAware = false;
  meta.messages = span<const lint::Message>(
      reinterpret_cast<const lint::Message *>(rule->messages), rule->message_count);
  meta.schema = nullptr;
  return meta;
}

} // namespace

const fl_host_api *hostApi()
{
  return &g_api;
}

void Plugin::create(lint::RuleContext &ctx)
{
  // `def` is the first member of Wrapper, so the RuleDef address is the Wrapper.
  const Wrapper *wrapper = reinterpret_cast<const Wrapper *>(&ctx.rule());
  const fl_rule *rule = wrapper->rule;
  for (int i = 0; i < rule->kind_count; i++) {
    NodeKind kind = NodeKind(rule->kinds[i]);
    ctx.on(kind, [rule, &ctx](Node *node) {
      fl_report report{&ctx};
      rule->fn(toFl(node), rule->userdata, &report);
    });
  }
}

Plugin::~Plugin()
{
  for (Wrapper *wrapper : m_wrappers) {
    litestl::alloc::Delete(wrapper);
  }
  if (m_handle) {
    closeLibrary(m_handle);
  }
}

Plugin *Plugin::load(std::string_view path, string &error)
{
  std::string cpath(path);
  void *handle = openLibrary(cpath.c_str());
  if (!handle) {
    error = string("cannot load plugin ");
    for (char c : path) {
      error += c;
    }
    return nullptr;
  }
  auto init =
      reinterpret_cast<fl_plugin_init_fn>(findSymbol(handle, "fastlint_plugin_init"));
  if (!init) {
    error = string("plugin has no fastlint_plugin_init");
    closeLibrary(handle);
    return nullptr;
  }
  const fl_plugin *info = init(hostApi());
  if (!info) {
    error = string("plugin declined to initialize");
    closeLibrary(handle);
    return nullptr;
  }
  if (info->abi_version != FL_ABI_VERSION) {
    error = string("plugin was built for a different ABI version");
    closeLibrary(handle);
    return nullptr;
  }
  if (info->nodes_def_hash != ast::nodesDefHash) {
    error = string("plugin was built against a different nodes.def");
    closeLibrary(handle);
    return nullptr;
  }
  Plugin *plugin = new (litestl::alloc::alloc("plugin", sizeof(Plugin))) Plugin();
  plugin->m_handle = handle;
  for (int i = 0; i < info->rule_count; i++) {
    const fl_rule *rule = &info->rules[i];
    Wrapper *wrapper = litestl::alloc::New<Wrapper>("plugin rule");
    wrapper->def.meta = metaFor(rule);
    wrapper->def.create = &Plugin::create;
    wrapper->rule = rule;
    plugin->m_wrappers.append(wrapper);
    plugin->m_rules.append(&wrapper->def);
  }
  return plugin;
}

} // namespace fastlint::plugin
