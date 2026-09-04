#include "testing/runner.h"
#include "testing/snapshot.h"

#include "platform/platform.h"
#include "util/alloc.h"
#include "util/set.h"

#include <chrono>
#include <cstdio>
#include <string>

namespace fastlint::test {

using litestl::util::Set;

namespace {

struct RunState {
  const TestCase *test = nullptr;
  TestResult *result = nullptr;
  bool aborted = false;

  Vector<string> infoStack;

  // Subcase replay. `done` holds fully-executed paths; `enteredAtDepth` marks
  // the level a subcase was already entered at during the current pass, so
  // siblings defer to a later pass.
  Vector<string> path;
  Set<string> done;
  Vector<bool> enteredAtDepth;
  int deferrals = 0;

  /** Bytes the framework itself retained in `result`, excluded from the leak check. */
  int64_t frameworkBytes = 0;
};

RunState &state()
{
  static RunState value;
  return value;
}

Options &mutableOptions()
{
  static Options value;
  return value;
}

string joinPath(const Vector<string> &parts)
{
  string out;
  for (size_t i = 0; i < parts.size(); i++) {
    if (i > 0) {
      out += " / ";
    }
    out += parts[int(i)];
  }
  return out;
}

} // namespace

const Options &options()
{
  return mutableOptions();
}

void setOptions(const Options &opts)
{
  mutableOptions() = opts;
}

Vector<TestCase> &registry()
{
  static Vector<TestCase> tests;
  return tests;
}

void registerTest(const TestCase &test)
{
  litestl::alloc::PermanentGuard guard;
  registry().append(test);
}

string fullName(const TestCase &test)
{
  string out = test.suite;
  out += ".";
  out += test.name;
  return out;
}

bool globMatch(const char *pattern, const char *text)
{
  if (*pattern == '\0') {
    return *text == '\0';
  }
  if (*pattern == '*') {
    // Either the star consumes this character or it matches nothing more.
    return globMatch(pattern + 1, text) ||
           (*text != '\0' && globMatch(pattern, text + 1));
  }
  if (*text == '\0') {
    return false;
  }
  if (*pattern == '?' || *pattern == *text) {
    return globMatch(pattern + 1, text + 1);
  }
  return false;
}

const TestCase *currentTest()
{
  return state().test;
}

string currentSubcasePath()
{
  return joinPath(state().path);
}

// ------------------------------------------------------------ reporting

namespace {

/** Charges everything allocated in its scope to the framework, not the test. */
struct FrameworkAlloc {
  size_t before = litestl::alloc::getMemorySize();

  ~FrameworkAlloc()
  {
    state().frameworkBytes += int64_t(litestl::alloc::getMemorySize()) - int64_t(before);
  }
};

Failure makeFailure(const char *file, int line)
{
  Failure failure;
  failure.file = file;
  failure.line = line;
  failure.subcasePath = joinPath(state().path);
  for (const string &info : state().infoStack) {
    failure.infos.append(info);
  }
  return failure;
}

void recordFailure(Failure &&failure)
{
  RunState &st = state();
  if (!st.result) {
    std::fprintf(stderr,
                 "assertion outside a test at %s:%d\n",
                 failure.file.c_str(),
                 failure.line);
    return;
  }
  st.result->status = Status::Failed;
  st.result->failures.append(std::move(failure));
  if (options().breakOnFailure) {
    litestl::platform::debugBreak();
  }
}

} // namespace

bool reportAssert(const ExprResult &result,
                  const char *expression,
                  const char *file,
                  int line,
                  bool fatal)
{
  const FrameworkAlloc fastlintCharge;
  if (result.passed) {
    return true;
  }
  Failure failure = makeFailure(file, line);
  failure.expression = expression;
  if (result.op.size() > 0) {
    failure.detail = result.lhs;
    failure.detail += " ";
    failure.detail += result.op;
    failure.detail += " ";
    failure.detail += result.rhs;
  } else {
    failure.detail = result.lhs;
  }
  recordFailure(std::move(failure));

  if (fatal) {
    state().aborted = true;
    return false;
  }
  return true;
}

void reportFailure(const string &message, const char *file, int line)
{
  const FrameworkAlloc fastlintCharge;
  Failure failure = makeFailure(file, line);
  failure.detail = message;
  recordFailure(std::move(failure));
  state().aborted = true;
}

void reportSnapshotFailure(const string &key,
                           const string &detail,
                           const string &diff,
                           const char *file,
                           int line)
{
  const FrameworkAlloc fastlintCharge;
  Failure failure = makeFailure(file, line);
  failure.expression = key;
  failure.detail = detail;
  failure.diff = diff;
  recordFailure(std::move(failure));
}

void markSkipped(const string &reason)
{
  const FrameworkAlloc fastlintCharge;
  RunState &st = state();
  if (!st.result) {
    return;
  }
  if (st.result->status != Status::Failed) {
    st.result->status = Status::Skipped;
    st.result->skipReason = reason;
  }
  st.aborted = true;
}

InfoScope::InfoScope(const string &text)
{
  state().infoStack.append(text);
}

InfoScope::~InfoScope()
{
  Vector<string> &stack = state().infoStack;
  if (stack.size() > 0) {
    stack.pop_back();
  }
}

// ------------------------------------------------------------ subcases

Subcase::Subcase(const char *label, const char * /*file*/, int /*line*/)
{
  RunState &st = state();
  const int depth = int(st.path.size());
  st.path.append(string(label));
  const string path = joinPath(st.path);

  if (st.done.contains(path)) {
    entered_ = false;
  } else if (depth < int(st.enteredAtDepth.size()) && st.enteredAtDepth[depth]) {
    // A sibling already ran this pass; this one waits for the next replay.
    st.deferrals++;
    entered_ = false;
  } else {
    while (int(st.enteredAtDepth.size()) <= depth) {
      st.enteredAtDepth.append(false);
    }
    st.enteredAtDepth[depth] = true;
    entered_ = true;
  }
  deferralsAtEntry_ = st.deferrals;
}

Subcase::~Subcase()
{
  RunState &st = state();
  if (entered_ && st.deferrals == deferralsAtEntry_) {
    // Nothing nested was deferred, so this path is finished.
    st.done.add(joinPath(st.path));
  }
  if (st.path.size() > 0) {
    st.path.pop_back();
  }
}

// ------------------------------------------------------------ run loop

namespace {

bool tagged(const TestCase &test, const string &tag)
{
  for (int i = 0; i < test.tagCount; i++) {
    if (tag == string(test.tags[i])) {
      return true;
    }
  }
  return false;
}

} // namespace

bool selected(const TestCase &test)
{
  const Options &opts = options();
  const string name = fullName(test);

  if (opts.filters.size() > 0) {
    bool any = false;
    for (const string &filter : opts.filters) {
      if (globMatch(filter.c_str(), name.c_str())) {
        any = true;
        break;
      }
    }
    if (!any) {
      return false;
    }
  }

  if (opts.tags.size() > 0) {
    bool any = false;
    for (const string &tag : opts.tags) {
      if (tagged(test, tag)) {
        any = true;
        break;
      }
    }
    if (!any) {
      return false;
    }
  }

  if (!opts.all) {
    for (const string &tag : opts.skipTags) {
      // An explicitly requested tag outranks the default skip list, so
      // `--tag slow` needs no `--all` beside it.
      if (tagged(test, tag) && !opts.tags.contains(tag)) {
        return false;
      }
    }
  }
  return true;
}

TestResult runTest(const TestCase &test)
{
  RunState &st = state();
  TestResult result;
  result.fullName = fullName(test);
  result.file = test.file;
  result.line = test.line;
  for (int i = 0; i < test.tagCount; i++) {
    result.tags.append(string(test.tags[i]));
  }

  st.test = &test;
  st.result = &result;
  st.done.clear();
  st.path.clear();
  st.infoStack.clear();
  st.frameworkBytes = 0;

  const size_t memoryBefore = litestl::alloc::getMemorySize();
  const auto start = std::chrono::steady_clock::now();

  // Replay the body until no subcase has been deferred.
  for (;;) {
    st.aborted = false;
    st.deferrals = 0;
    st.enteredAtDepth.clear();
    st.path.clear();

    test.fn();

    if (st.aborted || st.deferrals == 0) {
      break;
    }
  }

  const auto end = std::chrono::steady_clock::now();
  result.durationMs = std::chrono::duration<double, std::milli>(end - start).count();

  const size_t memoryAfter = litestl::alloc::getMemorySize();
  result.leakedBytes = int64_t(memoryAfter) - int64_t(memoryBefore) - st.frameworkBytes;
  if (options().checkLeaks && result.leakedBytes > 0 && result.status != Status::Skipped)
  {
    Failure failure;
    failure.file = test.file;
    failure.line = test.line;
    failure.expression = "leak check";
    failure.detail = "test leaked ";
    failure.detail += std::to_string(result.leakedBytes);
    failure.detail += " bytes";
    if (options().printLeaks) {
      failure.detail += "\n";
      failure.detail += litestl::alloc::formatBlocks(false);
    }
    result.status = Status::Failed;
    result.failures.append(std::move(failure));
  }

  st.test = nullptr;
  st.result = nullptr;
  st.infoStack.clear();
  return result;
}

} // namespace fastlint::test
