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
        "{\"extends\": \"fastlint:recommended\"}", "", lint::builtinRegistry(), error);
  }();
  (void)ready;
  return config;
}

void lintText(std::string_view source, std::string_view filename, string &out)
{
  lint::Linter linter(lint::builtinRegistry(), recommended());

  lint::LintOptions options;
  // An editor wants each problem's edit alongside it; nothing here writes files.
  options.fixEdits = true;

  lint::FileResult result;
  linter.lintSource(source, filename, options, result);
  lint::formatJson(span<const lint::FileResult>(&result, 1), out);
}

} // namespace fastlint::embed
