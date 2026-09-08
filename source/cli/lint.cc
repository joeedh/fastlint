// The `lint` command: config discovery, rule overrides, `--fix` and the
// output formats (docs/rules.md "Command line").

#include "cli/files.h"
#include "fastlint/lint/config.h"
#include "fastlint/lint/format.h"
#include "fastlint/lint/linter.h"
#include "fastlint/lint/registry.h"
#include "fastlint/types/type_source.h"
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

void usage()
{
  std::fprintf(
      stderr,
      "usage: fastlint lint [--config <file>] [--no-config] [--rule <name:severity>]... "
      "[--fix] [--format pretty|json|sarif] [--color|--no-color] [--quiet] "
      "[--max-warnings N] <file|dir>...\n");
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

} // namespace

int lintCommand(int argc, char **argv)
{
  Vector<std::filesystem::path> files;
  const char *configPath = nullptr;
  bool noConfig = false;
  bool fix = false;
  bool json = false;
  bool sarif = false;
  bool quiet = false;
  const char *project = nullptr;
  bool typeStats = false;
  int maxWarnings = -1;
  int color = -1;
  Vector<std::string> ruleFlags;
  for (int i = 2; i < argc; i++) {
    const char *arg = argv[i];
    if (std::strcmp(arg, "--config") == 0 && i + 1 < argc) {
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
      collectFiles(arg, files);
    }
  }
  if (files.isEmpty()) {
    usage();
    return 2;
  }

  const lint::Registry &registry = lint::builtinRegistry();
  lint::Config config;
  string error;
  string found;
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
      config.parse("{\"extends\": \"fastlint:recommended\"}", "", registry, error);
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

  lint::Linter linter(registry, config);
  lint::LintOptions options;
  options.fix = fix;
  // The JSON output carries an ESLint-shaped fix range per fixable problem.
  options.fixEdits = json && !fix;
  // Type-aware rules run only with a project; without one they are skipped.
  types::ProjectTypes types;
  Map<const lint::RuleDef *, types::FactsStats> ruleStats;
  if (project) {
    string typeError;
    if (!types.open(project, typeError)) {
      std::fprintf(
          stderr, "%s: cannot start the type server: %s\n", project, typeError.c_str());
      return 2;
    }
    options.types = &types;
    if (typeStats) {
      options.ruleStats = &ruleStats;
    }
  }
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
    lint::FileResult result;
    linter.lintSource(bytes, name, options, result);
    if (result.ignored) {
      continue;
    }
    if (result.typeError.size() > 0) {
      std::fprintf(stderr, "%s: no types: %s\n", name.c_str(), result.typeError.c_str());
    }
    if (fix && result.changed) {
      std::string output(result.output.c_str(), result.output.size());
      if (!writeFile(path, output)) {
        std::fprintf(stderr, "%s: cannot write\n", path.string().c_str());
        unreadable++;
      }
      fixed += result.fixesApplied;
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
