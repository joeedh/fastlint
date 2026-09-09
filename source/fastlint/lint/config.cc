#include "fastlint/lint/config.h"

#include "fastlint/lint/glob.h"
#include "fastlint/lint/option_schema.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fastlint::lint {

namespace {

constexpr const char *kConfigName = "fastlint.config.json";

// A case-insensitive filesystem compares paths without regard to case, so a
// glob should match the same way there.
#ifdef _WIN32
constexpr bool kPathCaseInsensitive = true;
#else
constexpr bool kPathCaseInsensitive = false;
#endif

void append(string &out, string_view text)
{
  for (char c : text) {
    out += c;
  }
}

string copy(string_view text)
{
  string out;
  append(out, text);
  return out;
}

string_view view(const string &s)
{
  return string_view(s.c_str(), s.size());
}

} // namespace

const RuleSetting *ResolvedConfig::find(const RuleDef *rule) const
{
  for (const RuleSetting &setting : rules) {
    if (setting.rule == rule) {
      return &setting;
    }
  }
  return nullptr;
}

const char *Config::severityName(Severity severity)
{
  switch (severity) {
  case Severity::Off:
    return "off";
  case Severity::Warn:
    return "warn";
  case Severity::Error:
    return "error";
  }
  return "off";
}

bool Config::parseSeverity(const JsonValue *value, Severity &out)
{
  if (!value) {
    return false;
  }
  if (value->isArray()) {
    return parseSeverity(value->at(0), out);
  }
  if (value->isNumber()) {
    int n = value->asInt(-1);
    if (n < 0 || n > 2) {
      return false;
    }
    out = Severity(n);
    return true;
  }
  if (value->isString()) {
    string_view text = value->asString();
    if (text == "off") {
      out = Severity::Off;
    } else if (text == "warn") {
      out = Severity::Warn;
    } else if (text == "error") {
      out = Severity::Error;
    } else {
      return false;
    }
    return true;
  }
  return false;
}

string Config::find(string_view dir)
{
  std::filesystem::path at{std::string(dir)};
  for (;;) {
    std::filesystem::path candidate = at / kConfigName;
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec)) {
      return copy(candidate.generic_string());
    }
    std::filesystem::path parent = at.parent_path();
    if (parent == at || parent.empty()) {
      return string();
    }
    at = parent;
  }
}

bool Config::load(string_view path, const Registry &registry, string &error)
{
  std::string bytes;
  std::FILE *file = std::fopen(std::string(path).c_str(), "rb");
  if (!file) {
    error = copy("cannot read ");
    append(error, path);
    return false;
  }
  char buffer[16384];
  for (;;) {
    size_t read = std::fread(buffer, 1, sizeof buffer, file);
    if (read == 0) {
      break;
    }
    bytes.append(buffer, read);
  }
  std::fclose(file);
  std::filesystem::path p{std::string(path)};
  std::string dir = p.parent_path().generic_string();
  return parse(bytes, dir, registry, error);
}

bool Config::parse(string_view text,
                   string_view baseDir,
                   const Registry &registry,
                   string &error)
{
  m_layers.clear();
  m_ignores.clear();
  m_pluginPrefixes.clear();
  m_unknownRules.clear();
  m_project = string();
  m_projects.clear();
  m_baseDir = copy(baseDir);
  if (!m_doc.parse(text)) {
    error = copy("config: ");
    append(error, view(m_doc.error()));
    return false;
  }
  const JsonValue *root = m_doc.root();
  if (!root || !root->isObject()) {
    error = copy("config: the top level must be an object");
    return false;
  }

  // `plugins` is read first: it decides whether a namespaced rule name below is
  // a plugin rule to skip or a name to report unknown.
  if (const JsonValue *plugins = root->get("plugins")) {
    if (!plugins->isObject()) {
      error = copy("config: \"plugins\" must map a prefix to a module specifier");
      return false;
    }
    for (int i = 0; i < plugins->size(); i++) {
      const JsonValue *specifier = plugins->items[i];
      if (!specifier || !specifier->isString() || specifier->asString().empty()) {
        error = copy("config: plugin \"");
        append(error, view(plugins->keys[i]));
        append(error, "\" must name a module specifier string");
        return false;
      }
      string_view prefix = view(plugins->keys[i]);
      if (Registry::isAliasPrefix(prefix)) {
        error = copy("config: plugin prefix \"");
        append(error, prefix);
        append(error, "\" is reserved for the built-in rules");
        return false;
      }
      m_pluginPrefixes.append(copy(prefix));
    }
  }

  Layer base;
  if (const JsonValue *extends = root->get("extends")) {
    if (extends->isString()) {
      if (!addPreset(extends->asString(), registry, base, error)) {
        return false;
      }
    } else if (extends->isArray()) {
      for (int i = 0; i < extends->size(); i++) {
        if (!addPreset(extends->at(i)->asString(), registry, base, error)) {
          return false;
        }
      }
    } else {
      error = copy("config: \"extends\" must be a string or an array of strings");
      return false;
    }
  }
  if (const JsonValue *rules = root->get("rules")) {
    if (!addRules(rules, registry, base, error)) {
      return false;
    }
  }
  m_layers.append(std::move(base));

  if (const JsonValue *overrides = root->get("overrides")) {
    if (!overrides->isArray()) {
      error = copy("config: \"overrides\" must be an array");
      return false;
    }
    for (int i = 0; i < overrides->size(); i++) {
      const JsonValue *item = overrides->at(i);
      const JsonValue *files = item->get("files");
      Layer layer;
      if (files && files->isString()) {
        layer.files.append(copy(files->asString()));
      } else if (files && files->isArray()) {
        for (int j = 0; j < files->size(); j++) {
          layer.files.append(copy(files->at(j)->asString()));
        }
      }
      if (layer.files.isEmpty()) {
        error = copy("config: every override needs a \"files\" glob");
        return false;
      }
      if (const JsonValue *rules = item->get("rules")) {
        if (!addRules(rules, registry, layer, error)) {
          return false;
        }
      }
      m_layers.append(std::move(layer));
    }
  }

  if (const JsonValue *project = root->get("project")) {
    if (!project->isString()) {
      error = copy("config: \"project\" must be a tsconfig path string");
      return false;
    }
    m_project = copy(project->asString());
  }
  if (const JsonValue *projects = root->get("projects")) {
    if (!projects->isArray()) {
      error = copy("config: \"projects\" must be an array");
      return false;
    }
    for (int i = 0; i < projects->size(); i++) {
      const JsonValue *item = projects->at(i);
      ProjectMap entry;
      const JsonValue *files = item ? item->get("files") : nullptr;
      if (files && files->isString()) {
        entry.files.append(copy(files->asString()));
      } else if (files && files->isArray()) {
        for (int j = 0; j < files->size(); j++) {
          entry.files.append(copy(files->at(j)->asString()));
        }
      }
      if (entry.files.isEmpty()) {
        error = copy("config: every \"projects\" entry needs a \"files\" glob");
        return false;
      }
      const JsonValue *proj = item->get("project");
      if (!proj || !proj->isString()) {
        error =
            copy("config: every \"projects\" entry needs a \"project\" tsconfig path");
        return false;
      }
      entry.project = copy(proj->asString());
      m_projects.append(std::move(entry));
    }
  }
  if (const JsonValue *ignores = root->get("ignores")) {
    for (int i = 0; i < ignores->size(); i++) {
      m_ignores.append(copy(ignores->at(i)->asString()));
    }
  }
  if (const JsonValue *unused = root->get("reportUnusedDisableDirectives")) {
    if (unused->kind == tsgo::JsonKind::Bool) {
      m_unusedDirectives = unused->asBool() ? Severity::Warn : Severity::Off;
    } else if (!parseSeverity(unused, m_unusedDirectives)) {
      error = copy("config: \"reportUnusedDisableDirectives\" must be a severity");
      return false;
    }
  }
  if (const JsonValue *eslint = root->get("eslintDirectives")) {
    m_eslintDirectives = eslint->asBool(true);
  }
  // Only the npm CLI drives `binary`. It is checked here anyway, so one config
  // reports the same errors whichever side reads it.
  if (const JsonValue *binary = root->get("binary")) {
    if (!binary->isString()) {
      error = copy("config: \"binary\" must be a path to the fastlint executable");
      return false;
    }
  }
  return true;
}

bool Config::addPreset(string_view name,
                       const Registry &registry,
                       Layer &layer,
                       string &error)
{
  bool all = name == "fastlint:all";
  if (!all && name != "fastlint:recommended") {
    error = copy("config: unknown preset \"");
    append(error, name);
    error += '"';
    return false;
  }
  for (const RuleDef *rule : registry.rules()) {
    if (all || rule->meta.recommended) {
      layer.entries.append({rule, Severity::Error, nullptr});
    }
  }
  return true;
}

bool Config::addRules(const JsonValue *rules,
                      const Registry &registry,
                      Layer &layer,
                      string &error)
{
  if (!rules->isObject()) {
    error = copy("config: \"rules\" must be an object");
    return false;
  }
  for (int i = 0; i < rules->size(); i++) {
    string_view name = view(rules->keys[i]);
    const JsonValue *setting = rules->items[i];
    Severity severity;
    if (!parseSeverity(setting, severity)) {
      error = copy("config: rule \"");
      append(error, name);
      append(error, "\" needs a severity (\"off\", \"warn\", \"error\" or 0-2)");
      return false;
    }
    const RuleDef *rule = registry.find(name);
    if (!rule) {
      if (isPluginRule(name)) {
        // The npm CLI imports the plugin and checks the rule name there, since
        // only it knows what the plugin defines. A name under a prefix nothing
        // declares stays unknown, so a typo still warns.
        layer.pluginEntries.append({copy(name), severity, setting});
      } else {
        m_unknownRules.append(copy(name));
      }
      continue;
    }
    if (!validateOptions(*rule, setting, error)) {
      return false;
    }
    layer.entries.append({rule, severity, setting});
  }
  return true;
}

void Config::setRule(const RuleDef *rule, Severity severity)
{
  m_cliEntries.append({rule, severity, nullptr});
}

bool Config::isPluginRule(string_view name) const
{
  // A rule name carries no slash of its own, so everything before the last one
  // is the prefix; a scoped package name keeps its own slash that way.
  size_t slash = name.rfind('/');
  if (slash == string_view::npos) {
    return false;
  }
  string_view prefix = name.substr(0, slash);
  for (const string &declared : m_pluginPrefixes) {
    if (view(declared) == prefix) {
      return true;
    }
  }
  return false;
}

void Config::pluginRuleNames(Vector<string> &out) const
{
  out.clear();
  for (const Layer &layer : m_layers) {
    for (const PluginRule &entry : layer.pluginEntries) {
      if (entry.severity == Severity::Off) {
        continue;
      }
      bool seen = false;
      for (const string &name : out) {
        seen = seen || view(name) == view(entry.name);
      }
      if (!seen) {
        out.append(entry.name);
      }
    }
  }
}

void Config::relativePath(string_view filename, string &out) const
{
  std::string path(filename);
  for (char &c : path) {
    if (c == '\\') {
      c = '/';
    }
  }
  std::string base(m_baseDir.c_str(), m_baseDir.size());
  for (char &c : base) {
    if (c == '\\') {
      c = '/';
    }
  }
  if (!base.empty() && base.back() != '/') {
    base += '/';
  }
  if (!base.empty() && path.size() > base.size() &&
      path.compare(0, base.size(), base) == 0)
  {
    path = path.substr(base.size());
  }
  out = string();
  append(out, path);
}

void Config::resolve(string_view filename, ResolvedConfig &out) const
{
  out.rules.clear();
  out.unknownRules.clear();
  out.pluginRules.clear();
  out.unusedDirectives = m_unusedDirectives;
  out.eslintDirectives = m_eslintDirectives;
  out.ignored = false;
  for (const string &name : m_unknownRules) {
    out.unknownRules.append(name);
  }

  string relative;
  relativePath(filename, relative);
  string_view path = view(relative);
  for (const string &pattern : m_ignores) {
    if (globMatch(view(pattern), path, kPathCaseInsensitive)) {
      out.ignored = true;
    }
  }

  auto apply = [&](const Entry &entry) {
    for (RuleSetting &setting : out.rules) {
      if (setting.rule == entry.rule) {
        setting.severity = entry.severity;
        // A bare severity keeps the options an earlier layer gave.
        if (entry.setting && entry.setting->isArray()) {
          setting.setting = entry.setting;
        }
        return;
      }
    }
    out.rules.append({entry.rule, entry.severity, entry.setting});
  };
  auto applyPlugin = [&](const PluginRule &entry) {
    for (PluginRule &setting : out.pluginRules) {
      if (view(setting.name) == view(entry.name)) {
        setting.severity = entry.severity;
        if (entry.setting && entry.setting->isArray()) {
          setting.setting = entry.setting;
        }
        return;
      }
    }
    out.pluginRules.append({entry.name, entry.severity, entry.setting});
  };
  for (const Layer &layer : m_layers) {
    bool matches = layer.files.isEmpty();
    for (const string &pattern : layer.files) {
      if (globMatch(view(pattern), path, kPathCaseInsensitive)) {
        matches = true;
      }
    }
    if (!matches) {
      continue;
    }
    for (const Entry &entry : layer.entries) {
      apply(entry);
    }
    for (const PluginRule &entry : layer.pluginEntries) {
      applyPlugin(entry);
    }
  }
  for (const Entry &entry : m_cliEntries) {
    apply(entry);
  }
}

string Config::projectFor(string_view filename) const
{
  // A tsconfig path is anchored at the config directory unless it is absolute.
  auto anchored = [&](const string &project) -> string {
    string_view p = view(project);
    bool absolute = p.size() >= 1 && (p[0] == '/' || (p.size() >= 2 && p[1] == ':'));
    if (absolute || m_baseDir.size() == 0) {
      return copy(p);
    }
    string out = m_baseDir;
    if (out.size() != 0 && out[int(out.size()) - 1] != '/') {
      out += '/';
    }
    append(out, p);
    return out;
  };
  string relative;
  relativePath(filename, relative);
  string_view path = view(relative);
  for (const ProjectMap &entry : m_projects) {
    for (const string &pattern : entry.files) {
      if (globMatch(view(pattern), path, kPathCaseInsensitive)) {
        return anchored(entry.project);
      }
    }
  }
  if (m_project.size() != 0) {
    return anchored(m_project);
  }
  return string();
}

} // namespace fastlint::lint
