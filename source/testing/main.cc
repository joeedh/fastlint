#include "testing/runner.h"
#include "testing/snapshot.h"

#include "util/rand.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#endif

namespace fastlint::test {

namespace {

struct Palette {
  const char *red = "";
  const char *green = "";
  const char *yellow = "";
  const char *dim = "";
  const char *bold = "";
  const char *reset = "";
};

Palette palette(bool color)
{
  if (!color) {
    return Palette{};
  }
  return Palette{"\x1b[31m", "\x1b[32m", "\x1b[33m", "\x1b[2m", "\x1b[1m", "\x1b[0m"};
}

void usage()
{
  std::printf("usage: <test exe> [options]\n"
              "  --filter <glob>[,<glob>]  run tests whose suite.name matches\n"
              "  --tag <tag>               run only tests with this tag (repeatable)\n"
              "  --skip-tag <tag>          skip tests with this tag (repeatable)\n"
              "  --all                     clear the default skips\n"
              "  --list                    print the selected tests and exit\n"
              "  --json <path>             write a machine-readable report\n"
              "  --break                   break into the debugger on the first failure\n"
              "  --isolate                 run each test in a child process\n"
              "  --repeat <n>              run the selection n times\n"
              "  --shuffle [seed]          randomize order\n"
              "  --timeout <ms>            abort the run if one test exceeds this\n"
              "  --leaks                   print leaked blocks on a leak failure\n"
              "  --no-leak-check           do not fail tests that leak\n"
              "  -u, --update[=<glob>]     rewrite failing and new snapshots\n"
              "  --ci                      new and obsolete snapshots are failures\n"
              "  --no-summary              omit the trailing summary line\n"
              "  --no-color, --verbose\n");
}

void splitInto(Vector<string> &out, const char *value)
{
  string current;
  for (const char *c = value; *c; c++) {
    if (*c == ',') {
      if (current.size() > 0) {
        out.append(current);
      }
      current = string("");
    } else {
      current += *c;
    }
  }
  if (current.size() > 0) {
    out.append(current);
  }
}

/** Returns false on a usage error. */
bool parseArgs(int argc, char **argv, Options &opts)
{
  opts.skipTags.append(string("integration"));
  opts.skipTags.append(string("slow"));
  opts.skipTags.append(string("bench"));
  opts.checkLeaks = true;
  opts.breakOnFailure = debuggerAttached();

  const auto next = [&](int &i) -> const char * {
    if (i + 1 >= argc) {
      std::fprintf(stderr, "%s needs a value\n", argv[i]);
      return nullptr;
    }
    return argv[++i];
  };

  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (std::strcmp(arg, "--filter") == 0) {
      const char *value = next(i);
      if (!value)
        return false;
      splitInto(opts.filters, value);
    } else if (std::strcmp(arg, "--tag") == 0) {
      const char *value = next(i);
      if (!value)
        return false;
      splitInto(opts.tags, value);
    } else if (std::strcmp(arg, "--skip-tag") == 0) {
      const char *value = next(i);
      if (!value)
        return false;
      splitInto(opts.skipTags, value);
    } else if (std::strcmp(arg, "--all") == 0) {
      opts.all = true;
    } else if (std::strcmp(arg, "--list") == 0) {
      opts.list = true;
    } else if (std::strcmp(arg, "--json") == 0) {
      const char *value = next(i);
      if (!value)
        return false;
      opts.jsonPath = string(value);
    } else if (std::strcmp(arg, "--break") == 0) {
      opts.breakOnFailure = true;
    } else if (std::strcmp(arg, "--no-break") == 0) {
      opts.breakOnFailure = false;
    } else if (std::strcmp(arg, "--no-summary") == 0) {
      opts.noSummary = true;
    } else if (std::strcmp(arg, "--isolate") == 0) {
      opts.isolate = true;
    } else if (std::strcmp(arg, "--repeat") == 0) {
      const char *value = next(i);
      if (!value)
        return false;
      opts.repeat = std::atoi(value);
    } else if (std::strcmp(arg, "--shuffle") == 0) {
      opts.shuffle = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        opts.seed = uint64_t(std::atoll(argv[++i]));
      }
    } else if (std::strcmp(arg, "--timeout") == 0) {
      const char *value = next(i);
      if (!value)
        return false;
      opts.timeoutMs = std::atoi(value);
    } else if (std::strcmp(arg, "--leaks") == 0) {
      opts.printLeaks = true;
    } else if (std::strcmp(arg, "--no-leak-check") == 0) {
      opts.checkLeaks = false;
    } else if (std::strcmp(arg, "-u") == 0 || std::strcmp(arg, "--update") == 0) {
      opts.update = true;
    } else if (std::strncmp(arg, "--update=", 9) == 0) {
      opts.update = true;
      opts.updateGlob = string(arg + 9);
    } else if (std::strcmp(arg, "--ci") == 0) {
      opts.ci = true;
    } else if (std::strcmp(arg, "--no-color") == 0) {
      opts.color = false;
    } else if (std::strcmp(arg, "--verbose") == 0) {
      opts.verbose = true;
    } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
      usage();
      std::exit(0);
    } else {
      std::fprintf(stderr, "unknown option %s\n", arg);
      usage();
      return false;
    }
  }
  return true;
}

void printFailure(const Failure &failure, const Palette &c)
{
  std::printf("  %s%s:%d%s\n", c.dim, failure.file.c_str(), failure.line, c.reset);
  if (failure.subcasePath.size() > 0) {
    std::printf("    subcase: %s\n", failure.subcasePath.c_str());
  }
  for (const string &info : failure.infos) {
    std::printf("    info: %s\n", info.c_str());
  }
  if (failure.expression.size() > 0) {
    std::printf("    %s\n", failure.expression.c_str());
  }
  if (failure.detail.size() > 0) {
    std::printf("      %s->%s %s\n", c.dim, c.reset, failure.detail.c_str());
  }
  if (failure.diff.size() > 0) {
    std::printf("%s", failure.diff.c_str());
  }
}

std::string jsonEscape(const char *text)
{
  std::string out;
  for (const char *c = text; *c; c++) {
    switch (*c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(*c) < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", *c);
        out += buf;
      } else {
        out += *c;
      }
    }
  }
  return out;
}

const char *statusName(Status status)
{
  switch (status) {
  case Status::Passed:
    return "passed";
  case Status::Failed:
    return "failed";
  case Status::Skipped:
    return "skipped";
  }
  return "unknown";
}

void writeJson(const string &path,
               const Vector<TestResult> &results,
               int passed,
               int failed,
               int skipped,
               const SnapshotSummary &snapshots)
{
  std::ofstream out(path.c_str(), std::ios::binary);
  if (!out) {
    std::fprintf(stderr, "could not write %s\n", path.c_str());
    return;
  }
  out << "{\n  \"summary\": {\"passed\": " << passed << ", \"failed\": " << failed
      << ", \"skipped\": " << skipped << ", \"newSnapshots\": " << snapshots.newEntries
      << ", \"obsoleteSnapshots\": " << snapshots.obsolete << "},\n  \"tests\": [\n";

  for (size_t i = 0; i < results.size(); i++) {
    const TestResult &result = results[int(i)];
    out << "    {\"name\": \"" << jsonEscape(result.fullName.c_str())
        << "\", \"status\": \"" << statusName(result.status)
        << "\", \"durationMs\": " << result.durationMs << ", \"file\": \""
        << jsonEscape(result.file.c_str()) << "\", \"line\": " << result.line
        << ", \"tags\": [";
    for (size_t t = 0; t < result.tags.size(); t++) {
      out << (t ? ", " : "") << "\"" << jsonEscape(result.tags[int(t)].c_str()) << "\"";
    }
    out << "], \"failures\": [";
    for (size_t f = 0; f < result.failures.size(); f++) {
      const Failure &failure = result.failures[int(f)];
      out << (f ? ", " : "") << "{\"file\": \"" << jsonEscape(failure.file.c_str())
          << "\", \"line\": " << failure.line << ", \"expression\": \""
          << jsonEscape(failure.expression.c_str()) << "\", \"detail\": \""
          << jsonEscape(failure.detail.c_str()) << "\", \"subcase\": \""
          << jsonEscape(failure.subcasePath.c_str()) << "\", \"diff\": \""
          << jsonEscape(failure.diff.c_str()) << "\"}";
    }
    out << "]}" << (i + 1 < results.size() ? "," : "") << "\n";
  }
  out << "  ]\n}\n";
}

#ifdef _WIN32
/** Re-runs one test in a child process so a crash does not end the run. */
int runIsolated(const char *exe, const TestCase &test, const Options &opts)
{
  const string name = fullName(test);
  std::vector<std::string> owned = {
      exe, "--filter", std::string(name.c_str()), "--no-break", "--no-summary"};
  if (opts.all)
    owned.push_back("--all");
  if (opts.update)
    owned.push_back("--update");
  if (!opts.color)
    owned.push_back("--no-color");

  std::vector<const char *> argv;
  for (const std::string &arg : owned) {
    argv.push_back(arg.c_str());
  }
  argv.push_back(nullptr);
  return int(_spawnv(_P_WAIT, exe, argv.data()));
}
#endif

} // namespace

int main(int argc, char **argv)
{
  Options opts;
  if (!parseArgs(argc, argv, opts)) {
    return 2;
  }
  setOptions(opts);
  installCrashHandler();

  const Palette c = palette(opts.color);

  Vector<TestCase> selection;
  for (const TestCase &test : registry()) {
    if (selected(test)) {
      selection.append(test);
    }
  }

  if (opts.list) {
    for (const TestCase &test : selection) {
      std::printf("%s", fullName(test).c_str());
      for (int i = 0; i < test.tagCount; i++) {
        std::printf(" [%s]", test.tags[i]);
      }
      std::printf("\n");
    }
    return 0;
  }

  if (opts.shuffle) {
    litestl::util::Random rng(uint32_t(opts.seed));
    for (int i = int(selection.size()) - 1; i > 0; i--) {
      const int j = int(rng.get_double() * double(i + 1)) % (i + 1);
      std::swap(selection[i], selection[j]);
    }
  }

  Vector<TestResult> results;
  int passed = 0;
  int failed = 0;
  int skipped = 0;

  const auto runStart = std::chrono::steady_clock::now();
  const int repeat = opts.repeat > 0 ? opts.repeat : 1;

  for (int pass = 0; pass < repeat; pass++) {
    for (const TestCase &test : selection) {
      resetSnapshotCounters();

#ifdef _WIN32
      if (opts.isolate) {
        const int code = runIsolated(argv[0], test, opts);
        TestResult result;
        result.fullName = fullName(test);
        result.file = test.file;
        result.line = test.line;
        result.status = code == 0 ? Status::Passed : Status::Failed;
        if (code != 0) {
          Failure failure;
          failure.file = test.file;
          failure.line = test.line;
          failure.expression = "isolated run";
          failure.detail = "child exited with code ";
          failure.detail += std::to_string(code);
          result.failures.append(std::move(failure));
        }
        results.append(result);
        (result.status == Status::Passed ? passed : failed)++;
        continue;
      }
#endif

      std::atomic_bool finished{false};
      std::thread watchdog;
      if (opts.timeoutMs > 0) {
        const int timeout = opts.timeoutMs;
        const string name = fullName(test);
        watchdog = std::thread([&finished, timeout, name]() {
          const auto deadline =
              std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
          while (!finished.load()) {
            if (std::chrono::steady_clock::now() > deadline) {
              std::fprintf(
                  stderr, "\ntimeout: %s exceeded %d ms\n", name.c_str(), timeout);
              std::fflush(stderr);
              std::_Exit(2);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
          }
        });
      }

      TestResult result = runTest(test);
      finished.store(true);
      if (watchdog.joinable()) {
        watchdog.join();
      }

      switch (result.status) {
      case Status::Passed:
        passed++;
        break;
      case Status::Failed:
        failed++;
        break;
      case Status::Skipped:
        skipped++;
        break;
      }

      if (opts.verbose) {
        const char *mark = result.status == Status::Failed    ? c.red
                           : result.status == Status::Skipped ? c.yellow
                                                              : c.green;
        std::printf("%s%-8s%s %s (%.2fms)\n",
                    mark,
                    statusName(result.status),
                    c.reset,
                    result.fullName.c_str(),
                    result.durationMs);
      }
      results.append(std::move(result));
    }
  }

  const SnapshotSummary snapshots = finishSnapshots();

  for (const TestResult &result : results) {
    if (result.status != Status::Failed) {
      continue;
    }
    std::printf("\n%sFAILED%s %s\n", c.red, c.reset, result.fullName.c_str());
    for (const Failure &failure : result.failures) {
      printFailure(failure, c);
    }
  }

  const auto runEnd = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(runEnd - runStart).count();

  if (!opts.noSummary) {
    std::printf("\n%d passed, %s%d failed%s, %d skipped",
                passed,
                failed ? c.red : "",
                failed,
                failed ? c.reset : "",
                skipped);
    if (snapshots.newEntries > 0) {
      std::printf(", %d new snapshot%s",
                  snapshots.newEntries,
                  snapshots.newEntries == 1 ? "" : "s");
    }
    if (snapshots.obsolete > 0) {
      std::printf(", %s%d obsolete snapshot%s%s",
                  c.yellow,
                  snapshots.obsolete,
                  snapshots.obsolete == 1 ? "" : "s",
                  c.reset);
    }
    std::printf(", %.2fs\n", seconds);
  }

  if (opts.jsonPath.size() > 0) {
    writeJson(opts.jsonPath, results, passed, failed, skipped, snapshots);
  }

  if (failed > 0) {
    return 1;
  }
  if (opts.ci && snapshots.obsolete > 0) {
    return 1;
  }
  return 0;
}

} // namespace fastlint::test

int main(int argc, char **argv)
{
  return fastlint::test::main(argc, argv);
}
