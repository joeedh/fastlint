#include "cli/run.h"

#include "cli/files.h"
#include "fastlint/version.h"

#include <cstdio>

namespace fastlint::cli {

using litestl::util::string;
using litestl::util::Vector;

bool ResultCache::open(const std::string &dbPath,
                       uint64_t env,
                       const Vector<std::filesystem::path> &files)
{
  cache::StoreOptions options;
  options.path = string(dbPath.c_str());
  // A lintrix upgrade may change rule logic, so its version keys the store.
  options.tsgoVersion = string(fastlint::version());
  string error;
  if (!store.open(options, error)) {
    std::fprintf(stderr, "cache: %s\n", error.c_str());
    return false;
  }
  for (const std::filesystem::path &path : files) {
    refresh(std::filesystem::absolute(path).generic_string());
  }
  envHash = env;
  ready = true;
  return true;
}

void ResultCache::refresh(const std::string &absPath)
{
  string ignored;
  cache::loadClosure(graph, std::string_view(absPath), fs, ignored);
}

bool ResultCache::load(const std::string &absPath,
                       std::string_view key,
                       const lint::Registry &registry,
                       lint::FileResult &out)
{
  if (!ready) {
    return false;
  }
  cache::FileCache fileCache(store, graph, envHash);
  cache::FileRecord record;
  cache::Freshness freshness;
  string error;
  if (!fileCache.lookup(std::string_view(absPath), record, freshness, error)) {
    return false;
  }
  if (freshness != cache::Freshness::Fresh) {
    return false;
  }
  string payload;
  bool found = false;
  if (!fileCache.ruleResult(record, key, payload, found, error) || !found) {
    return false;
  }
  return lint::deserializeResult(
      std::string_view(payload.c_str(), payload.size()), registry, out);
}

void ResultCache::save(const std::string &absPath,
                       std::string_view key,
                       const lint::FileResult &result)
{
  if (!ready) {
    return;
  }
  cache::FileCache fileCache(store, graph, envHash);
  cache::FileRecord record;
  cache::Freshness freshness;
  string error;
  if (!fileCache.lookup(std::string_view(absPath), record, freshness, error)) {
    return;
  }
  string payload;
  lint::serializeResult(result, payload);
  if (!store.begin(error)) {
    return;
  }
  if (!fileCache.saveRuleResult(
          record, key, std::string_view(payload.c_str(), payload.size()), error) ||
      !fileCache.commitFile(record, error))
  {
    store.rollback();
    return;
  }
  store.commit(error);
}

std::string
cacheDirFor(const char *given, bool noCache, bool fix, const std::filesystem::path &root)
{
  if (noCache || fix) {
    return std::string();
  }
  if (given) {
    return std::string(given);
  }
  std::error_code ec;
  if (std::filesystem::is_directory(root / "node_modules", ec)) {
    return (root / "node_modules/.cache/lintrix").generic_string();
  }
  return std::string();
}

bool isTypeScript(const std::filesystem::path &path)
{
  std::string ext = path.extension().generic_string();
  return ext == ".ts" || ext == ".tsx" || ext == ".mts" || ext == ".cts";
}

std::string tsconfigUpwards(const std::filesystem::path &dir)
{
  std::filesystem::path at = dir;
  for (;;) {
    std::filesystem::path candidate = at / "tsconfig.json";
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec)) {
      return candidate.generic_string();
    }
    std::filesystem::path parent = at.parent_path();
    if (parent == at || parent.empty()) {
      return std::string();
    }
    at = parent;
  }
}

uint64_t environmentHash(const string &configPath,
                         bool noConfig,
                         bool recommendedFallback,
                         const Vector<std::string> &ruleFlags,
                         const Vector<std::string> &projects,
                         const std::filesystem::path &lockDir)
{
  std::string bytes = fastlint::version();
  bytes += noConfig ? "\nno-config" : "";
  bytes += recommendedFallback ? "\nrecommended" : "";
  if (configPath.size() > 0) {
    std::string text;
    if (readFile(std::filesystem::path(configPath.c_str()), text)) {
      bytes += '\n';
      bytes += text;
    }
  }
  for (const std::string &flag : ruleFlags) {
    bytes += '\n';
    bytes += flag;
  }
  for (const std::string &project : projects) {
    std::string text;
    if (readFile(std::filesystem::path(project), text)) {
      bytes += '\n';
      bytes += text;
    }
  }
  std::string lock;
  if (readFile(lockDir / "pnpm-lock.yaml", lock)) {
    bytes += '\n';
    bytes += lock;
  }
  return cache::hashContent(std::string_view(bytes));
}

} // namespace fastlint::cli
