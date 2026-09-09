#include "fastlint/embed/lint_text.h"

#include "fastlint/lint/config.h"
#include "fastlint/lint/format.h"
#include "fastlint/lint/linter.h"
#include "fastlint/lint/registry.h"
#include "util/span.h"

namespace fastlint::embed {

using litestl::util::span;

/**
 * The recommended preset, resolved once. Building it parses a config document
 * and walks the registry, and an embedding lints many buffers against the same
 * one, so the cost is paid on the first call only.
 */
static const lint::Config &recommended()
{
  static lint::Config config;
  static const bool ready = [] {
    string error;
    return config.parse(
        "{\"extends\": \"lintrix:recommended\"}", "", lint::builtinRegistry(), error);
  }();
  (void)ready;
  return config;
}

namespace {

/** Lints through `config` and writes the one-element JSON array. */
void run(const lint::Config &config,
         std::string_view source,
         std::string_view filename,
         string &out)
{
  lint::Linter linter(lint::builtinRegistry(), config);

  lint::LintOptions options;
  // An editor wants each problem's edit alongside it; nothing here writes files.
  options.fixEdits = true;

  lint::FileResult result;
  linter.lintSource(source, filename, options, result);
  lint::formatJson(span<const lint::FileResult>(&result, 1), out);
}

/** True when `text` equals the litestl string `held`. */
bool same(const string &held, std::string_view text)
{
  return std::string_view(held.c_str(), held.size()) == text;
}

} // namespace

void lintText(std::string_view source, std::string_view filename, string &out)
{
  run(recommended(), source, filename, out);
}

void lintTextWithConfig(std::string_view source,
                        std::string_view filename,
                        std::string_view configJson,
                        std::string_view baseDir,
                        string &out,
                        string &error)
{
  error = string();
  if (configJson.empty()) {
    run(recommended(), source, filename, out);
    return;
  }

  // Parsing a config walks the registry and holds the document the settings
  // point into, so the last one is kept for as long as the host asks for it.
  static lint::Config config;
  static string heldJson;
  static string heldBase;
  static bool ready = false;
  if (!ready || !same(heldJson, configJson) || !same(heldBase, baseDir)) {
    ready = config.parse(configJson, baseDir, lint::builtinRegistry(), error);
    if (!ready) {
      out = string();
      return;
    }
    heldJson = string();
    for (char c : configJson) {
      heldJson += c;
    }
    heldBase = string();
    for (char c : baseDir) {
      heldBase += c;
    }
  }
  run(config, source, filename, out);
}

} // namespace fastlint::embed
