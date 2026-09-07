#pragma once

// `fastlint.config.json` (docs/rules.md "Config"): rule severities and
// options, `extends` presets, `overrides` by glob, `ignores`, and the
// directive settings. Resolved per file into the rules the linter runs.

#include "fastlint/lint/registry.h"
#include "fastlint/lint/rule.h"
#include "fastlint/tsgo/json.h"
#include "util/string.h"
#include "util/vector.h"

#include <string_view>

namespace fastlint::lint {

struct RuleSetting {
  const RuleDef *rule;
  Severity severity;
  /** The configured value: a severity, or an array of severity then options. Null
   * when the rule came from a preset or the command line. */
  const JsonValue *setting;
};

/** What one file is linted with. */
struct ResolvedConfig {
  /** Every configured rule, including those set to `off`, in registry order. */
  Vector<RuleSetting> rules;
  /** Names configured that no rule answers to. */
  Vector<string> unknownRules;
  Severity unusedDirectives = Severity::Warn;
  bool eslintDirectives = true;
  /** Matched an `ignores` glob. */
  bool ignored = false;

  const RuleSetting *find(const RuleDef *rule) const;
};

class Config {
public:
  Config() = default;
  Config(const Config &) = delete;
  Config &operator=(const Config &) = delete;

  /** Parses a config text; relative globs are anchored at `baseDir`. */
  bool
  parse(string_view text, string_view baseDir, const Registry &registry, string &error);
  /** Reads and parses `path`. */
  bool load(string_view path, const Registry &registry, string &error);

  /** Sets `rule` on top of everything the file says, as the `--rule` flag does. */
  void setRule(const RuleDef *rule, Severity severity);
  void setUnusedDirectives(Severity severity)
  {
    m_unusedDirectives = severity;
  }
  void setEslintDirectives(bool on)
  {
    m_eslintDirectives = on;
  }

  /** The rules `filename` is linted with, after every matching override. */
  void resolve(string_view filename, ResolvedConfig &out) const;

  /** `dir` joined with the file name, or empty when no config exists there or above. */
  static string find(string_view dir);
  /** `"off"`, `"warn"`, `"error"` or 0, 1, 2; false on anything else. */
  static bool parseSeverity(const JsonValue *value, Severity &out);
  static const char *severityName(Severity severity);

private:
  struct Entry {
    const RuleDef *rule;
    Severity severity;
    const JsonValue *setting;
  };
  struct Layer {
    /** Empty for the base layer, which applies to every file. */
    Vector<string> files;
    Vector<Entry> entries;
  };

  tsgo::JsonDocument m_doc;
  string m_baseDir;
  Vector<Layer> m_layers;
  Vector<string> m_ignores;
  Vector<string> m_unknownRules;
  Vector<Entry> m_cliEntries;
  Severity m_unusedDirectives = Severity::Warn;
  bool m_eslintDirectives = true;

  bool
  addRules(const JsonValue *rules, const Registry &registry, Layer &layer, string &error);
  bool addPreset(string_view name, const Registry &registry, Layer &layer, string &error);
  /** `filename` relative to the base directory, with forward slashes. */
  void relativePath(string_view filename, string &out) const;
};

} // namespace fastlint::lint
