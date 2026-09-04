#include "testing/runner.h"

#include "platform/platform.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fastlint::test {

namespace {

void printContext(const char *what)
{
  const TestCase *test = currentTest();
  std::fprintf(stderr, "\n%s\n", what);
  if (test) {
    std::fprintf(stderr,
                 "  in test: %s.%s (%s:%d)\n",
                 test->suite,
                 test->name,
                 test->file,
                 test->line);
    const string path = currentSubcasePath();
    if (path.size() > 0) {
      std::fprintf(stderr, "  subcase: %s\n", path.c_str());
    }
  }
  const std::string trace = litestl::platform::getStackTrace();
  std::fprintf(stderr, "%s\n", trace.c_str());
  std::fflush(stderr);
}

#ifdef _WIN32
LONG WINAPI onUnhandledException(EXCEPTION_POINTERS *info)
{
  char what[64];
  std::snprintf(what,
                sizeof(what),
                "crash: exception 0x%08lx",
                info ? info->ExceptionRecord->ExceptionCode : 0ul);
  printContext(what);
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

extern "C" void onSignal(int signal)
{
  char what[64];
  std::snprintf(what, sizeof(what), "crash: signal %d", signal);
  printContext(what);
  std::_Exit(2);
}

} // namespace

void installCrashHandler()
{
#ifdef _WIN32
  SetUnhandledExceptionFilter(&onUnhandledException);
#endif
  std::signal(SIGSEGV, &onSignal);
  std::signal(SIGABRT, &onSignal);
  std::signal(SIGILL, &onSignal);
  std::signal(SIGFPE, &onSignal);
}

bool debuggerAttached()
{
#ifdef _WIN32
  return IsDebuggerPresent() != 0;
#else
  return false;
#endif
}

} // namespace fastlint::test
