#pragma once

// The per-run setup the `lint` and `serve` commands share: the rule-result
// cache, tsconfig discovery and the environment hash that keys the cache.

#include "fastlint/cache/closure.h"
#include "fastlint/cache/store.h"
#include "fastlint/lint/linter.h"
#include "fastlint/lint/registry.h"
#include "fastlint/lint/result_cache.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace fastlint::cli {

/**
 * The rule-result cache for one run: a SQLite store plus the import graph of
 * the linted files. A file whose content, closure and environment hashes match
 * a stored record replays its diagnostics instead of running the rules
 * (docs/rules.md "Result cache").
 */
struct ResultCache {
  cache::Store store;
  cache::ImportGraph graph;
  cache::DiskFileSystem fs;
  uint64_t envHash = 0;
  bool ready = false;

  /** Opens the store and loads the closure of every file in `files`. */
  bool open(const std::string &dbPath,
            uint64_t env,
            const litestl::util::Vector<std::filesystem::path> &files);

  /** Reads `absPath` and its imports into the graph again, so a lookup sees
   * the disk as it is now. `lookup` fails for a file the graph has not seen. */
  void refresh(const std::string &absPath);

  /** Replays a stored result for `absPath`; false on a miss or any error. */
  bool load(const std::string &absPath,
            std::string_view key,
            const lint::Registry &registry,
            lint::FileResult &out);

  void
  save(const std::string &absPath, std::string_view key, const lint::FileResult &result);
};

/** The cache directory for a run, or empty when caching is off. The default is
 * the conventional gitignored JS cache dir under `root`, used only when its
 * `node_modules` exists. */
std::string cacheDirFor(const char *given,
                        bool noCache,
                        bool fix,
                        const std::filesystem::path &root = ".");

/** A TypeScript source, whose type-aware rules a missing tsconfig would silently skip. */
bool isTypeScript(const std::filesystem::path &path);

/** The nearest `tsconfig.json` at `dir` or an ancestor, or empty when none exists. */
std::string tsconfigUpwards(const std::filesystem::path &dir);

/** Hashes everything beyond a file's own content and closure that changes its
 * diagnostics: the lintrix version, the config, the tsconfigs and the lockfile
 * under `lockDir`. */
uint64_t environmentHash(const litestl::util::string &configPath,
                         bool noConfig,
                         bool recommendedFallback,
                         const litestl::util::Vector<std::string> &ruleFlags,
                         const litestl::util::Vector<std::string> &projects,
                         const std::filesystem::path &lockDir = ".");

} // namespace fastlint::cli
