#pragma once

#include "testing/test.h"
#include "util/string.h"
#include "util/vector.h"

namespace fastlint::test {

enum class Status { Passed, Failed, Skipped };

/** One recorded failure, with everything needed to print it in isolation. */
struct Failure {
  string file;
  int line = 0;
  string expression;
  string detail;
  string subcasePath;
  Vector<string> infos;
  /** Rendered diff for a snapshot mismatch; empty otherwise. */
  string diff;
};

struct TestResult {
  string fullName;
  string file;
  int line = 0;
  Vector<string> tags;
  Status status = Status::Passed;
  string skipReason;
  double durationMs = 0.0;
  Vector<Failure> failures;
  int64_t leakedBytes = 0;
};

struct Options {
  Vector<string> filters;
  Vector<string> tags;
  Vector<string> skipTags;
  bool all = false;
  bool list = false;
  bool verbose = false;
  bool color = true;
  bool ci = false;
  bool breakOnFailure = false;
  bool isolate = false;
  /** Suppresses the trailing summary line, for callers that print their own. */
  bool noSummary = false;
  bool checkLeaks = false;
  bool printLeaks = false;
  bool update = false;
  string updateGlob;
  string jsonPath;
  int repeat = 1;
  bool shuffle = false;
  uint64_t seed = 0;
  int timeoutMs = 0;
  /** True when a name filter is active, which suppresses the obsolete check. */
  bool filtered() const
  {
    return filters.size() > 0 || tags.size() > 0;
  }
};

const Options &options();
void setOptions(const Options &opts);

/** Registry access, in registration order. */
Vector<TestCase> &registry();

/** Full `suite.name` for a registered test. */
string fullName(const TestCase &test);

/** Matches a name against a comma-free glob supporting `*` and `?`. */
bool globMatch(const char *pattern, const char *text);

/** True when the filters, tags and skip-tags in effect select this test. */
bool selected(const TestCase &test);

/** Runs one test, replaying it until every subcase has been entered. */
TestResult runTest(const TestCase &test);

/** The test currently running, for the crash handler and the snapshot store. */
const TestCase *currentTest();
string currentSubcasePath();

/** Installs the crash handler that prints the running test before exiting. */
void installCrashHandler();

/** True when a debugger is attached, which turns --break on by default. */
bool debuggerAttached();

} // namespace fastlint::test
