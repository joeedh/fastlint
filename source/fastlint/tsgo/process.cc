#include "fastlint/tsgo/process.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fastlint::tsgo {

#ifdef _WIN32

namespace {

std::wstring toWide(const string &text)
{
  if (text.size() == 0) {
    return {};
  }
  int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0);
  std::wstring out(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), n);
  return out;
}

/** Appends `arg` using the CommandLineToArgvW quoting rules. */
void appendArgument(std::wstring &cmd, const std::wstring &arg)
{
  if (!cmd.empty()) {
    cmd += L' ';
  }
  bool plain = !arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos;
  if (plain) {
    cmd += arg;
    return;
  }
  cmd += L'"';
  size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      backslashes++;
      continue;
    }
    if (c == L'"') {
      cmd.append(backslashes * 2 + 1, L'\\');
      backslashes = 0;
      cmd += c;
      continue;
    }
    cmd.append(backslashes, L'\\');
    backslashes = 0;
    cmd += c;
  }
  cmd.append(backslashes * 2, L'\\');
  cmd += L'"';
}

void lastError(string &error, const char *what)
{
  char buf[160];
  snprintf(buf, sizeof buf, "%s failed (error %lu)", what, GetLastError());
  error = string(buf);
}

bool hasDirectory(const string &exe)
{
  for (size_t i = 0; i < exe.size(); i++) {
    if (exe[intptr_t(i)] == '/' || exe[intptr_t(i)] == '\\') {
      return true;
    }
  }
  return false;
}

} // namespace

Process::~Process()
{
  closeHandles();
}

void Process::closeHandles()
{
  closeStdin();
  if (m_stdout) {
    CloseHandle(m_stdout);
    m_stdout = nullptr;
  }
  if (m_process) {
    CloseHandle(m_process);
    m_process = nullptr;
  }
}

bool Process::start(const ProcessOptions &options, string &error)
{
  if (m_started) {
    error = string("process already started");
    return false;
  }
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;

  HANDLE childStdinRead = nullptr, stdinWrite = nullptr;
  HANDLE stdoutRead = nullptr, childStdoutWrite = nullptr;
  if (!CreatePipe(&childStdinRead, &stdinWrite, &sa, 0)) {
    lastError(error, "CreatePipe(stdin)");
    return false;
  }
  if (!CreatePipe(&stdoutRead, &childStdoutWrite, &sa, 0)) {
    lastError(error, "CreatePipe(stdout)");
    CloseHandle(childStdinRead);
    CloseHandle(stdinWrite);
    return false;
  }
  SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);

  std::wstring exe = toWide(options.exe);
  std::wstring cmd;
  appendArgument(cmd, exe);
  for (const string &arg : options.args) {
    appendArgument(cmd, toWide(arg));
  }
  std::wstring cwd = toWide(options.cwd);

  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = childStdinRead;
  si.hStdOutput = childStdoutWrite;
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION pi{};

  // An explicit application name skips the PATH search, so it is only given when the
  // caller already resolved the path.
  const wchar_t *app = hasDirectory(options.exe) ? exe.c_str() : nullptr;
  BOOL ok = CreateProcessW(app,
                           cmd.data(),
                           nullptr,
                           nullptr,
                           TRUE,
                           CREATE_NO_WINDOW,
                           nullptr,
                           cwd.empty() ? nullptr : cwd.c_str(),
                           &si,
                           &pi);
  CloseHandle(childStdinRead);
  CloseHandle(childStdoutWrite);
  if (!ok) {
    lastError(error, "CreateProcess");
    error += ": ";
    error += options.exe;
    CloseHandle(stdinWrite);
    CloseHandle(stdoutRead);
    return false;
  }
  CloseHandle(pi.hThread);
  m_process = pi.hProcess;
  m_stdin = stdinWrite;
  m_stdout = stdoutRead;
  m_started = true;
  return true;
}

bool Process::running()
{
  if (!m_started || m_exited) {
    return false;
  }
  if (WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT) {
    return true;
  }
  wait();
  return false;
}

bool Process::write(span<const uint8_t> bytes)
{
  if (!m_stdin) {
    return false;
  }
  size_t at = 0;
  while (at < bytes.size()) {
    DWORD written = 0;
    DWORD chunk = DWORD(bytes.size() - at);
    if (!WriteFile(m_stdin, bytes.data() + at, chunk, &written, nullptr)) {
      return false;
    }
    at += written;
  }
  return true;
}

int Process::read(uint8_t *buffer, int size)
{
  if (!m_stdout) {
    return -1;
  }
  DWORD got = 0;
  if (!ReadFile(m_stdout, buffer, DWORD(size), &got, nullptr)) {
    DWORD err = GetLastError();
    return err == ERROR_BROKEN_PIPE ? 0 : -1;
  }
  return int(got);
}

void Process::closeStdin()
{
  if (m_stdin) {
    CloseHandle(m_stdin);
    m_stdin = nullptr;
  }
}

int Process::wait()
{
  if (!m_started) {
    return -1;
  }
  if (!m_exited) {
    WaitForSingleObject(m_process, INFINITE);
    DWORD code = 0;
    m_exitCode = GetExitCodeProcess(m_process, &code) ? int(code) : -1;
    m_exited = true;
  }
  return m_exitCode;
}

void Process::kill()
{
  if (m_started && !m_exited) {
    TerminateProcess(m_process, 1);
  }
}

#else

namespace {

void errnoError(string &error, const char *what)
{
  char buf[160];
  snprintf(buf, sizeof buf, "%s failed: %s", what, strerror(errno));
  error = string(buf);
}

} // namespace

Process::~Process()
{
  closeHandles();
}

void Process::closeHandles()
{
  closeStdin();
  if (m_stdout >= 0) {
    ::close(m_stdout);
    m_stdout = -1;
  }
}

bool Process::start(const ProcessOptions &options, string &error)
{
  if (m_started) {
    error = string("process already started");
    return false;
  }
  int inPipe[2], outPipe[2];
  if (pipe(inPipe) != 0) {
    errnoError(error, "pipe(stdin)");
    return false;
  }
  if (pipe(outPipe) != 0) {
    errnoError(error, "pipe(stdout)");
    ::close(inPipe[0]);
    ::close(inPipe[1]);
    return false;
  }
  std::vector<std::string> args;
  args.push_back(options.exe.c_str());
  for (const string &arg : options.args) {
    args.push_back(arg.c_str());
  }
  std::vector<char *> argv;
  for (std::string &arg : args) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);
  std::string cwd = options.cwd.c_str();

  pid_t pid = fork();
  if (pid < 0) {
    errnoError(error, "fork");
    ::close(inPipe[0]);
    ::close(inPipe[1]);
    ::close(outPipe[0]);
    ::close(outPipe[1]);
    return false;
  }
  if (pid == 0) {
    dup2(inPipe[0], STDIN_FILENO);
    dup2(outPipe[1], STDOUT_FILENO);
    ::close(inPipe[0]);
    ::close(inPipe[1]);
    ::close(outPipe[0]);
    ::close(outPipe[1]);
    if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
      _exit(126);
    }
    execvp(argv[0], argv.data());
    _exit(127);
  }
  ::close(inPipe[0]);
  ::close(outPipe[1]);
  m_pid = pid;
  m_stdin = inPipe[1];
  m_stdout = outPipe[0];
  m_started = true;
  return true;
}

bool Process::running()
{
  if (!m_started || m_exited) {
    return false;
  }
  int status = 0;
  pid_t r = waitpid(m_pid, &status, WNOHANG);
  if (r == 0) {
    return true;
  }
  m_exited = true;
  m_exitCode = r > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return false;
}

bool Process::write(span<const uint8_t> bytes)
{
  if (m_stdin < 0) {
    return false;
  }
  size_t at = 0;
  while (at < bytes.size()) {
    ssize_t n = ::write(m_stdin, bytes.data() + at, bytes.size() - at);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    at += size_t(n);
  }
  return true;
}

int Process::read(uint8_t *buffer, int size)
{
  if (m_stdout < 0) {
    return -1;
  }
  while (true) {
    ssize_t n = ::read(m_stdout, buffer, size_t(size));
    if (n < 0 && errno == EINTR) {
      continue;
    }
    return int(n);
  }
}

void Process::closeStdin()
{
  if (m_stdin >= 0) {
    ::close(m_stdin);
    m_stdin = -1;
  }
}

int Process::wait()
{
  if (!m_started) {
    return -1;
  }
  if (!m_exited) {
    int status = 0;
    while (waitpid(m_pid, &status, 0) < 0) {
      if (errno != EINTR) {
        m_exitCode = -1;
        m_exited = true;
        return m_exitCode;
      }
    }
    m_exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    m_exited = true;
  }
  return m_exitCode;
}

void Process::kill()
{
  if (m_started && !m_exited) {
    ::kill(m_pid, SIGKILL);
  }
}

#endif

bool runCapture(const ProcessOptions &options,
                string &output,
                int &exitCode,
                string &error)
{
  Process process;
  if (!process.start(options, error)) {
    return false;
  }
  process.closeStdin();
  uint8_t buffer[4096];
  while (true) {
    int n = process.read(buffer, int(sizeof buffer));
    if (n <= 0) {
      break;
    }
    for (int i = 0; i < n; i++) {
      output += char(buffer[i]);
    }
  }
  exitCode = process.wait();
  return true;
}

} // namespace fastlint::tsgo
