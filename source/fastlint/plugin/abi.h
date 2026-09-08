#pragma once

// The stable C ABI a native plugin links against (docs/ast-design.md
// "Plugins"). A plugin exports `fastlint_plugin_init`, receives an
// `fl_host_api` of function pointers, and returns an `fl_plugin` rule table.
// Node reads, reporting, templates and the fixer all cross as plain calls, so
// litestl never appears in the plugin surface. The node vocabulary and the
// `nodes.def` hash come from the generated header beside this one.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bumped when the shapes below change; `fastlint_plugin_init` refuses a mismatch. */
#define FL_ABI_VERSION 1u

typedef struct fl_node fl_node;         /* an AST node, read through the host */
typedef struct fl_fixer fl_fixer;       /* a fix pass, live only in a fix callback */
typedef struct fl_report fl_report;     /* a rule invocation's report sink */
typedef struct fl_template fl_template; /* a compiled template, process lifetime */

typedef struct {
  const char *ptr;
  size_t len;
} fl_str;

typedef struct {
  fl_node *const *ptr;
  size_t len;
} fl_nodes;

/** One template argument: a name bound to one node, or several for a list slot. */
typedef struct {
  fl_str name;
  fl_node *const *nodes;
  size_t node_count;
} fl_targ;

/** A fix the host runs during its fix pass, calling back with a live `fl_fixer`. */
typedef void (*fl_fix_fn)(fl_fixer *fixer, void *userdata);

/** Host services handed to the plugin at init, stable for `FL_ABI_VERSION`. */
typedef struct fl_host_api {
  uint32_t abi_version;

  /* Node reads (mirror ast/access.h). */
  uint16_t (*kind)(const fl_node *n);
  uint32_t (*flags)(const fl_node *n);
  int (*has_flag)(const fl_node *n, uint32_t flag);
  uint8_t (*data_byte)(const fl_node *n, int index);
  fl_str (*text)(const fl_node *n);
  fl_node *(*parent)(const fl_node *n);
  int (*child_count)(const fl_node *n);
  fl_node *(*child)(const fl_node *n, int index);
  fl_nodes (*tail)(const fl_node *n, int from);

  /* Reporting, valid only inside a rule callback. */
  void (*report)(fl_report *r, const fl_node *node, const char *message_id);
  void (*report_fix)(fl_report *r,
                     const fl_node *node,
                     const char *message_id,
                     fl_fix_fn fix,
                     void *fix_userdata);

  /* Templates. `mode` and `jsx` mirror ast::Template::compile. */
  const fl_template *(*template_compile)(fl_str text, int mode, int jsx);

  /* Fixer ops, valid only inside a fix callback. */
  fl_node *(*fixer_instantiate)(fl_fixer *f,
                                const fl_template *t,
                                const fl_targ *args,
                                size_t arg_count);
  int (*fixer_replace)(fl_fixer *f, fl_node *old_node, fl_node *fresh);
} fl_host_api;

/** A message template, `{{name}}` placeholders resolved by the host. */
typedef struct {
  const char *id;
  const char *text;
} fl_message;

/** A rule callback: the matched node, the plugin's data, and the report sink. */
typedef void (*fl_rule_fn)(fl_node *node, void *userdata, fl_report *report);

/** One rule the plugin contributes to the host's kind-to-rules dispatch. */
typedef struct {
  const char *name;
  const fl_message *messages; /* the ids `report` may name */
  int message_count;
  const uint16_t *kinds; /* the node kinds this rule listens to */
  int kind_count;
  fl_rule_fn fn;
  void *userdata;
  int fixable; /* the rule offers a fix, for the output's fixable counts */
} fl_rule;

/** The plugin's answer to `fastlint_plugin_init`; refused on a version or hash mismatch.
 */
typedef struct {
  uint32_t abi_version;    /* must equal FL_ABI_VERSION */
  uint32_t nodes_def_hash; /* must equal the host's nodes.def hash */
  const fl_rule *rules;
  int rule_count;
} fl_plugin;

/** Exported by the plugin shared library; returns null to decline the host. */
typedef const fl_plugin *(*fl_plugin_init_fn)(const fl_host_api *host);

#ifdef __cplusplus
} /* extern "C" */
#endif
