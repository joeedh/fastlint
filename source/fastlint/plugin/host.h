#pragma once

// The host side of the native plugin ABI (docs/ast-design.md "Plugins"):
// the services table every plugin receives, and a loader that opens a shared
// library, checks its ABI version and nodes.def hash, and wraps its rules as
// `RuleDef`s for the linter's registry.

#include "fastlint/lint/rule.h"
#include "fastlint/plugin/abi.h"
#include "util/string.h"
#include "util/vector.h"

#include <string_view>

namespace fastlint::plugin {

using litestl::util::span;
using litestl::util::string;
using litestl::util::Vector;

/** The host services table handed to every plugin at init. */
const fl_host_api *hostApi();

/** A loaded plugin. Its rules stay valid while it lives; destroying it unloads
 * the library, so it must outlive any registry the rules were added to. */
class Plugin {
public:
  ~Plugin();
  Plugin(const Plugin &) = delete;
  Plugin &operator=(const Plugin &) = delete;

  /** Opens `path`, runs `fastlint_plugin_init`, checks the ABI version and the
   * nodes.def hash, and builds a `RuleDef` per plugin rule. Null with `error`
   * on any failure. */
  static Plugin *load(std::string_view path, string &error);

  /** The rules to add to a registry, valid for this plugin's lifetime. */
  span<const lint::RuleDef *const> rules() const
  {
    Vector<const lint::RuleDef *> &rules =
        const_cast<Vector<const lint::RuleDef *> &>(m_rules);
    return {rules.data(), rules.size()};
  }

private:
  Plugin() = default;

  /** A plugin rule's `RuleDef` next to the `fl_rule` a shared `create` recovers
   * from it. `def` is first so a `RuleDef *` casts back to this. */
  struct Wrapper {
    lint::RuleDef def;
    const fl_rule *rule;
  };

  /** The shared `RuleDef::create` for every plugin rule; recovers the `fl_rule`
   * from the `RuleDef` and registers its kind listeners. */
  static void create(lint::RuleContext &ctx);

  void *m_handle = nullptr;
  Vector<Wrapper *> m_wrappers;
  Vector<const lint::RuleDef *> m_rules;
};

} // namespace fastlint::plugin
