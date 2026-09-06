#pragma once

#include "util/span.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdint>

namespace fastlint::tsgo {

using litestl::util::span;
using litestl::util::Vector;
using string = litestl::util::string;

/** What to run and where. Arguments are passed unquoted; the launcher quotes them for the
 * platform. */
struct ProcessOptions {
  string exe;
  Vector<string, 4> args;
  /** Working directory; empty inherits ours. */
  string cwd;
};

/** A child process with piped stdin and stdout; stderr is inherited. Reads block, so
 * callers drive the request/response cycle themselves. */
class Process {
public:
  Process() = default;
  ~Process();
  Process(const Process &) = delete;
  Process &operator=(const Process &) = delete;

  bool start(const ProcessOptions &options, string &error);
  bool started() const
  {
    return m_started;
  }
  /** True until the child has been observed to exit. */
  bool running();

  /** Writes every byte to the child's stdin; false once the pipe is broken. */
  bool write(span<const uint8_t> bytes);
  /** Blocks for stdout data. Returns the count read, 0 at end of stream, -1 on error. */
  int read(uint8_t *buffer, int size);
  void closeStdin();
  /** Waits for exit and returns the exit code, or -1 when it cannot be determined. */
  int wait();
  void kill();

private:
  void closeHandles();

  bool m_started = false;
  bool m_exited = false;
  int m_exitCode = -1;
#ifdef _WIN32
  void *m_process = nullptr;
  void *m_stdin = nullptr;
  void *m_stdout = nullptr;
#else
  int m_pid = -1;
  int m_stdin = -1;
  int m_stdout = -1;
#endif
};

/** Runs a command to completion and captures its stdout. False when it could not be
 * spawned; `exitCode` carries the child's status otherwise. */
bool runCapture(const ProcessOptions &options,
                string &output,
                int &exitCode,
                string &error);

} // namespace fastlint::tsgo
