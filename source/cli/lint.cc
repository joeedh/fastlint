// The `lint` command: config discovery, rule overrides, `--fix` and the
// output formats (docs/rules.md "Command line").

#include "cli/files.h"
#include "fastlint/cache/closure.h"
#include "fastlint/cache/store.h"
#include "fastlint/lint/config.h"
#include "fastlint/lint/format.h"
#include "fastlint/lint/linter.h"
#include "fastlint/lint/registry.h"
#include "fastlint/lint/result_cache.h"
#include "fastlint/types/type_source.h"
#include "fastlint/version.h"
#include "util/string.h"
#include "util/vector.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <io.h>
#define FASTLINT_ISATTY _isatty
#define FASTLINT_FILENO _fileno
#else
#include <unistd.h>
#define FASTLINT_ISATTY isatty
#define FASTLINT_FILENO fileno
#endif

namespace fastlint::cli {

namespace {

using litestl::util::Map;
using litestl::util::span;
using litestl::util::string;
using litestl::util::Vector;
using std::string_view;

void usage(std::FILE *out = stderr)
{
  std::fprintf(out, "usage: lintrix lint [options] <file|dir>...\n");
}

/** The full `lint` help: the usage line followed by every option. Printed to
 * stdout on `--help`. */
void help()
{
  usage(stdout);
  std::fprintf(
      stdout,
      "\n"
      "options:\n"
      "  --config <file>        config to load (default: lintrix.config.json upwards)\n"
      "  --no-config            ignore any config file; use the recommended preset\n"
      "  --rule <name:severity> override one rule (severity: off, warn, error, or 0-2)\n"
      "  --fix                  rewrite files with the available fixes\n"
      "  --format <fmt>         output format: pretty (default), json, or sarif\n"
      "  --color | --no-color   force or disable color (default: on when a TTY)\n"
      "  --quiet                report errors only, dropping warnings\n"
      "  --project <tsconfig>   type every file with this tsconfig (enables type rules)\n"
      "  --type-stats           print type-server fetch counts to stderr\n"
      "  --no-cache             do not read or write the result cache\n"
      "  --cache-dir <dir>      result-cache directory (default: node_modules/.cache)\n"
      "  --max-warnings N       exit non-zero when warnings exceed N\n"
      "  -h, --help             show this help\n"
      "\n"
      "Run `lintrix --init` to write a starter config.\n");
}

/** `name:severity` or `name=severity`. */
bool parseRuleFlag(std::string_view text, std::string &name, lint::Severity &severity)
{
  size_t split = text.find_first_of(":=");
  if (split == std::string_view::npos) {
    return false;
  }
  name = std::string(text.substr(0, split));
  std::string_view value = text.substr(split + 1);
  while (!name.empty() && name.back() == ' ') {
    name.pop_back();
  }
  while (!value.empty() && value.front() == ' ') {
    value = value.substr(1);
  }
  if (value == "off" || value == "0") {
    severity = lint::Severity::Off;
  } else if (value == "warn" || value == "1") {
    severity = lint::Severity::Warn;
  } else if (value == "error" || value == "2") {
    severity = lint::Severity::Error;
  } else {
    return false;
  }
  return true;
}

/**
 * The rule-result cache for one `lint` run: a SQLite store plus the import
 * graph of the linted files. A file whose content, closure and environment
 * hashes match a stored record replays its diagnostics instead of running the
 * rules (docs/rules.md "Result cache").
 */
struct ResultCache {
  cache::Store store;
  cache::ImportGraph graph;
  cache::DiskFileSystem fs;
  uint64_t envHash = 0;
  bool ready = false;

  bool open(const std::string &dbPath,
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
      std::string abs = std::filesystem::absolute(path).generic_string();
      string ignored;
      cache::loadClosure(graph, std::string_view(abs), fs, ignored);
    }
    envHash = env;
    ready = true;
    return true;
  }

  /** Replays a stored result for `absPath`; false on a miss or any error. */
  bool load(const std::string &absPath,
            string_view key,
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
        string_view(payload.c_str(), payload.size()), registry, out);
  }

  void save(const std::string &absPath, string_view key, const lint::FileResult &result)
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
            record, key, string_view(payload.c_str(), payload.size()), error) ||
        !fileCache.commitFile(record, error))
    {
      store.rollback();
      return;
    }
    store.commit(error);
  }
};

/** The cache directory for this run, or empty when caching is off. The default
 * is the conventional gitignored JS cache dir, used only when it can exist. */
std::string cacheDirFor(const char *given, bool noCache, bool fix)
{
  if (noCache || fix) {
    return std::string();
  }
  if (given) {
    return std::string(given);
  }
  std::error_code ec;
  if (std::filesystem::is_directory("node_modules", ec)) {
    return "node_modules/.cache/lintrix";
  }
  return std::string();
}

/** Hashes everything beyond a file's own content and closure that changes its
 * diagnostics: the lintrix version, the config, the tsconfig and the lockfile. */
/** A TypeScript source, whose type-aware rules a missing tsconfig would silently skip. */
bool isTypeScript(const std::filesystem::path &path)
{
  std::string ext = path.extension().generic_string();
  return ext == ".ts" || ext == ".tsx" || ext == ".mts" || ext == ".cts";
}

/** The nearest `tsconfig.json` at `dir` or an ancestor, or empty when none exists. */
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
                         const Vector<std::string> &projects)
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
  if (readFile(std::filesystem::path("pnpm-lock.yaml"), lock)) {
    bytes += '\n';
    bytes += lock;
  }
  return cache::hashContent(std::string_view(bytes));
}

} // namespace

/** True when `path` matches one of `config`'s `ignores` globs; used to prune
 * ignored directories while `collectFiles` walks the tree. */
bool skipIgnored(const std::filesystem::path &path, void *context)
{
  const auto *config = static_cast<const lint::Config *>(context);
  return config->isIgnored(std::string_view(path.generic_string()));
}

int lintCommand(int argc, char **argv)
{
  Vector<std::string> pathArgs;
  const char *configPath = nullptr;
  bool noConfig = false;
  bool fix = false;
  bool json = false;
  bool sarif = false;
  bool quiet = false;
  const char *project = nullptr;
  bool typeStats = false;
  bool noCache = false;
  const char *cacheDir = nullptr;
  int maxWarnings = -1;
  int color = -1;
  Vector<std::string> ruleFlags;
  for (int i = 2; i < argc; i++) {
    const char *arg = argv[i];
    if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
      help();
      return 0;
    } else if (std::strcmp(arg, "--config") == 0 && i + 1 < argc) {
      configPath = argv[++i];
    } else if (std::strcmp(arg, "--no-config") == 0) {
      noConfig = true;
    } else if (std::strcmp(arg, "--rule") == 0 && i + 1 < argc) {
      ruleFlags.append(std::string(argv[++i]));
    } else if (std::strcmp(arg, "--fix") == 0) {
      fix = true;
    } else if (std::strcmp(arg, "--project") == 0 && i + 1 < argc) {
      project = argv[++i];
    } else if (std::strcmp(arg, "--type-stats") == 0) {
      typeStats = true;
    } else if (std::strcmp(arg, "--no-cache") == 0) {
      noCache = true;
    } else if (std::strcmp(arg, "--cache-dir") == 0 && i + 1 < argc) {
      cacheDir = argv[++i];
    } else if (std::strcmp(arg, "--format") == 0 && i + 1 < argc) {
      const char *format = argv[++i];
      if (std::strcmp(format, "json") == 0) {
        json = true;
      } else if (std::strcmp(format, "sarif") == 0) {
        sarif = true;
      } else if (std::strcmp(format, "pretty") != 0 &&
                 std::strcmp(format, "stylish") != 0)
      {
        std::fprintf(stderr, "unknown format '%s'\n", format);
        return 2;
      }
    } else if (std::strcmp(arg, "--color") == 0) {
      color = 1;
    } else if (std::strcmp(arg, "--no-color") == 0) {
      color = 0;
    } else if (std::strcmp(arg, "--quiet") == 0) {
      quiet = true;
    } else if (std::strcmp(arg, "--max-warnings") == 0 && i + 1 < argc) {
      maxWarnings = std::atoi(argv[++i]);
    } else if (arg[0] == '-' && arg[1] == '-') {
      std::fprintf(stderr, "unknown option '%s'\n", arg);
      usage();
      return 2;
    } else {
      pathArgs.append(std::string(arg));
    }
  }
  if (pathArgs.isEmpty()) {
    usage();
    return 2;
  }

  const lint::Registry &registry = lint::builtinRegistry();
  lint::Config config;
  string error;
  string found;
  bool recommendedFallback = false;
  if (!noConfig) {
    if (configPath) {
      found = string(configPath);
    } else {
      std::error_code ec;
      std::string cwd = std::filesystem::current_path(ec).generic_string();
      found = lint::Config::find(cwd);
    }
    if (found.size() > 0) {
      if (!config.load(std::string_view(found.c_str(), found.size()), registry, error)) {
        std::fprintf(stderr, "%s: %s\n", found.c_str(), error.c_str());
        return 2;
      }
    } else if (ruleFlags.isEmpty()) {
      // With no config file and no --rule, the recommended preset applies.
      config.parse("{\"extends\": \"lintrix:recommended\"}", "", registry, error);
      recommendedFallback = true;
    }
  }
  for (const std::string &flag : ruleFlags) {
    std::string name;
    lint::Severity severity;
    if (!parseRuleFlag(flag, name, severity)) {
      std::fprintf(stderr, "--rule expects name:severity, got '%s'\n", flag.c_str());
      return 2;
    }
    const lint::RuleDef *rule = registry.find(name);
    if (!rule) {
      std::fprintf(stderr, "unknown rule '%s'\n", name.c_str());
      return 2;
    }
    config.setRule(rule, severity);
  }

  // Walked after the config loads so an ignored directory is pruned instead
  // of expanded and filtered file by file.
  Vector<std::filesystem::path> files;
  for (const std::string &pathArg : pathArgs) {
    collectFiles(pathArg.c_str(), files, skipIgnored, &config);
  }
  if (files.isEmpty()) {
    usage();
    return 2;
  }

  // A plugin rule needs a JavaScript host to import and run it. Passing over one
  // in silence would read as a rule that found nothing.
  Vector<string> pluginRules;
  config.pluginRuleNames(pluginRules);
  if (!pluginRules.isEmpty()) {
    std::string names;
    for (const string &name : pluginRules) {
      if (!names.empty()) {
        names += ", ";
      }
      names.append(name.c_str(), name.size());
    }
    std::fprintf(stderr,
                 "%d plugin rule%s skipped (%s); lint through the lintrix npm CLI "
                 "to run them\n",
                 int(pluginRules.size()),
                 pluginRules.size() == 1 ? "" : "s",
                 names.c_str());
  }

  lint::Linter linter(registry, config);
  lint::LintOptions options;
  options.fix = fix;
  // The JSON output carries an ESLint-shaped fix range per fixable problem.
  options.fixEdits = json && !fix;
  // Type-aware rules need the tsconfig that owns each file. `--project` forces
  // one for every file; otherwise the config's `projects`/`project` decides,
  // then the nearest tsconfig.json walking up from the file, then a
  // tsconfig.json beside the config file. Discovery is best-effort, so a server
  // that will not start only disables the type-aware rules, unlike an explicit
  // `--project`, which is an error.
  bool projectExplicit = project != nullptr;
  std::string projectDefault;
  if (found.size() > 0) {
    std::filesystem::path sibling =
        std::filesystem::path(std::string(found.c_str(), found.size())).parent_path() /
        "tsconfig.json";
    std::error_code ec;
    if (std::filesystem::is_regular_file(sibling, ec)) {
      projectDefault = sibling.generic_string();
    }
  }
  Vector<std::string> perFileProject;
  Vector<std::string> uniqueProjects;
  for (const std::filesystem::path &path : files) {
    std::string name = path.generic_string();
    std::string proj;
    if (projectExplicit) {
      proj = project;
    } else {
      string configProj = config.projectFor(std::string_view(name));
      if (configProj.size() > 0) {
        proj.assign(configProj.c_str(), configProj.size());
      } else {
        std::error_code ec;
        proj = tsconfigUpwards(std::filesystem::absolute(path, ec).parent_path());
        if (proj.empty()) {
          proj = projectDefault;
        }
      }
    }
    perFileProject.append(proj);
    if (!proj.empty()) {
      bool seen = false;
      for (const std::string &u : uniqueProjects) {
        if (u == proj) {
          seen = true;
          break;
        }
      }
      if (!seen) {
        uniqueProjects.append(proj);
      }
    }
  }
  types::ProjectTypes types;
  Map<const lint::RuleDef *, types::FactsStats> ruleStats;
  if (!uniqueProjects.isEmpty()) {
    Vector<string> list;
    for (const std::string &p : uniqueProjects) {
      list.append(string(p.c_str()));
    }
    string typeError;
    if (types.open(list, typeError)) {
      options.types = &types;
      if (typeStats) {
        options.ruleStats = &ruleStats;
      }
      for (int i = 0; i < int(files.size()); i++) {
        if (!perFileProject[i].empty()) {
          types.setFileProject(files[i].generic_string(), perFileProject[i]);
        } else if (isTypeScript(files[i])) {
          // Types are engaged for the run, so a TypeScript file with no tsconfig
          // is a gap the user should see, not a silent syntactic-only pass.
          std::fprintf(stderr,
                       "%s: no tsconfig resolved; type-aware rules skipped\n",
                       files[i].generic_string().c_str());
        }
      }
    } else if (projectExplicit) {
      std::fprintf(
          stderr, "%s: cannot start the type server: %s\n", project, typeError.c_str());
      return 2;
    } else {
      std::fprintf(stderr, "type-aware rules disabled (%s)\n", typeError.c_str());
    }
  }
  // The rule-result cache replays unchanged files; `--fix` and JSON fix ranges
  // are kept in separate payloads through the key.
  ResultCache cache;
  std::string cacheDirPath = cacheDirFor(cacheDir, noCache, fix);
  if (cacheDirPath.size() > 0) {
    std::error_code ec;
    std::filesystem::create_directories(cacheDirPath, ec);
    uint64_t env =
        environmentHash(found, noConfig, recommendedFallback, ruleFlags, uniqueProjects);
    cache.open(cacheDirPath + "/lint.db", env, files);
  }
  const char *cacheKey = options.fixEdits ? "@lint+fix" : "@lint";

  Vector<lint::FileResult> results;
  int errors = 0, warnings = 0, fixed = 0, unreadable = 0;
  for (const std::filesystem::path &path : files) {
    std::string bytes;
    if (!readFile(path, bytes)) {
      std::fprintf(stderr, "%s: cannot read\n", path.string().c_str());
      unreadable++;
      continue;
    }
    std::string name = path.generic_string();
    std::string absPath = std::filesystem::absolute(path).generic_string();
    lint::FileResult result;
    bool hit = cache.load(absPath, cacheKey, registry, result);
    if (hit) {
      result.filename = string(name.c_str());
    } else {
      linter.lintSource(bytes, name, options, result);
      if (result.ignored) {
        continue;
      }
      if (result.typeError.size() > 0) {
        std::fprintf(
            stderr, "%s: no types: %s\n", name.c_str(), result.typeError.c_str());
      }
      if (fix && result.changed) {
        std::string output(result.output.c_str(), result.output.size());
        if (!writeFile(path, output)) {
          std::fprintf(stderr, "%s: cannot write\n", path.string().c_str());
          unreadable++;
        }
        fixed += result.fixesApplied;
      }
      // A degraded run (the server could not type the file) is not cached.
      if (result.typeError.size() == 0) {
        cache.save(absPath, cacheKey, result);
      }
    }
    if (quiet) {
      // Only errors survive; the counts follow.
      Vector<lint::Diagnostic> kept;
      for (lint::Diagnostic &d : result.diagnostics) {
        if (d.severity == lint::Severity::Error) {
          kept.append(std::move(d));
        }
      }
      result.diagnostics = std::move(kept);
      result.warningCount = 0;
      result.fixableWarningCount = 0;
    }
    errors += result.errorCount;
    warnings += result.warningCount;
    results.append(std::move(result));
  }

  string out;
  if (json) {
    lint::formatJson(span<const lint::FileResult>(results.data(), results.size()), out);
  } else if (sarif) {
    lint::formatSarif(span<const lint::FileResult>(results.data(), results.size()), out);
  } else {
    lint::FormatOptions format;
    format.color =
        color == 1 || (color == -1 && FASTLINT_ISATTY(FASTLINT_FILENO(stdout)));
    lint::formatPretty(
        span<const lint::FileResult>(results.data(), results.size()), format, out);
    if (fix && fixed > 0) {
      char line[64];
      std::snprintf(
          line, sizeof line, "fixed %d problem%s\n", fixed, fixed == 1 ? "" : "s");
      out += line;
    }
  }
  std::fputs(out.c_str(), stdout);
  std::fflush(stdout);
  if (typeStats && project) {
    types::FactsStats facts = types.stats();
    const tsgo::RpcStats &rpc = types.rpcStats();
    std::fprintf(stderr,
                 "types: %d node hits, %d misses, %d unmapped; %d type, %d child, %d "
                 "symbol fetches; %d rpc calls, %zu bytes out, %zu in\n",
                 facts.nodeHits,
                 facts.nodeMisses,
                 facts.unmappedNodes,
                 facts.typeFetches,
                 facts.childFetches,
                 facts.symbolFetches,
                 rpc.calls,
                 rpc.bytesSent,
                 rpc.bytesReceived);
    // Per-rule attribution, busiest first. Rules that asked nothing of the type
    // server are omitted.
    struct RuleLine {
      string_view name;
      types::FactsStats stats;
    };
    Vector<RuleLine> lines;
    for (const auto &pair : ruleStats) {
      if (pair.value.fetches() > 0) {
        lines.append({string_view(pair.key->meta.name), pair.value});
      }
    }
    std::sort(lines.data(),
              lines.data() + lines.size(),
              [](const RuleLine &a, const RuleLine &b) {
                return a.stats.fetches() > b.stats.fetches();
              });
    for (const RuleLine &line : lines) {
      std::fprintf(stderr,
                   "  %-32.*s %d fetches (%d type, %d child, %d symbol)\n",
                   int(line.name.size()),
                   line.name.data(),
                   line.stats.fetches(),
                   line.stats.typeFetches,
                   line.stats.childFetches,
                   line.stats.symbolFetches);
    }
  }

  if (unreadable > 0) {
    return 2;
  }
  if (errors > 0 || (maxWarnings >= 0 && warnings > maxWarnings)) {
    return 1;
  }
  return 0;
}

} // namespace fastlint::cli
