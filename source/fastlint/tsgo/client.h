#pragma once

#include "fastlint/tsgo/json.h"
#include "fastlint/tsgo/msgpack.h"
#include "fastlint/tsgo/process.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

namespace fastlint::tsgo {

/** Serves file contents to the server over `--callbacks=readFile,fileExists`, so it
 * checks the same bytes our parser saw. */
class FileProvider {
public:
  virtual ~FileProvider() = default;
  /** Fills `content` and returns true to serve `path`; false defers to the disk. */
  virtual bool readFile(std::string_view path, string &content) = 0;
  /** 1 when `path` exists, 0 when it is known to be missing, -1 to defer to the disk. */
  virtual int fileExists(std::string_view path) = 0;
};

struct ClientOptions {
  /** Path of the native `tsc`; empty resolves it with `resolveTsgoExe`. */
  string exe;
  /** Server working directory, normally the project root. */
  string cwd;
  /** Answers FS callbacks when set; the server reads the disk otherwise. */
  FileProvider *files = nullptr;
  /** Accepts any `tsc --version`; for probing new releases, never for linting. */
  bool skipVersionCheck = false;
};

struct RpcStats {
  int calls = 0;
  int callbacks = 0;
  size_t bytesSent = 0;
  size_t bytesReceived = 0;
};

/** One project inside a snapshot. */
struct ProjectInfo {
  /** Canonical tsconfig path; the `project` parameter of every query. */
  string id;
  string configFileName;
};

/** The result of `updateSnapshot`. */
struct SnapshotInfo {
  int id = 0;
  Vector<ProjectInfo, 1> projects;
  /** Files the server found changed since the previous snapshot, across all projects. */
  Vector<string> changedFiles;
};

/** Edits to describe to `updateSnapshot`; all fields optional. */
struct SnapshotUpdate {
  Vector<string, 1> openProjects;
  Vector<string, 1> closeProjects;
  Vector<string, 1> openFiles;
  Vector<string, 1> closeFiles;
  Vector<string, 1> changed;
  Vector<string, 1> created;
  Vector<string, 1> deleted;
  bool invalidateAll = false;
};

/** Which native `tsc` releases the protocol tables were probed against. */
bool versionSupported(std::string_view version);
/** Extracts "7.0.2" from `tsc --version` output; false when the text is not a version
 * line. */
bool parseVersionOutput(std::string_view output, string &version);
/** Finds the native `tsc`: `FASTLINT_TSGO`, then the platform package under a
 * `node_modules` at or above `startDir`, then PATH. */
bool resolveTsgoExe(std::string_view startDir, string &exe);

/** One `tsc --api` server over the msgpack envelope on stdio. Calls are synchronous and
 * strictly sequential; FS callbacks are answered while waiting for a response. */
class Client {
public:
  Client() = default;
  ~Client();
  Client(const Client &) = delete;
  Client &operator=(const Client &) = delete;

  /** Resolves and version-gates the binary, spawns it and runs `initialize`. */
  bool start(const ClientOptions &options, string &error);
  /** Ends the session: closes stdin and waits briefly, killing the server if it lingers.
   */
  void stop();
  /** True while the server process is alive and the connection has not failed. */
  bool alive();

  /** Issues `method` with `params` (JSON text, or empty for null) and parses the JSON
   * result into `result`. False with `error` on a server error or a broken connection. */
  bool call(std::string_view method,
            std::string_view params,
            JsonDocument &result,
            string &error);
  /** Like `call`, for methods that answer with raw bytes (`getSourceFile`). */
  bool callRaw(std::string_view method,
               std::string_view params,
               Vector<uint8_t, 4> &payload,
               string &error);

  bool updateSnapshot(const SnapshotUpdate &update, SnapshotInfo &info, string &error);
  /** Opens `tsconfig` in a new snapshot. */
  bool openProject(std::string_view tsconfig, SnapshotInfo &info, string &error);
  bool release(int snapshot, string &error);
  /** The project owning `file` in `snapshot`; empty `projectId` when none does. */
  bool defaultProjectForFile(int snapshot,
                             std::string_view file,
                             string &projectId,
                             string &error);

  const string &version() const
  {
    return m_version;
  }
  const string &exe() const
  {
    return m_exe;
  }
  bool caseSensitiveFileNames() const
  {
    return m_caseSensitive;
  }
  const RpcStats &stats() const
  {
    return m_stats;
  }

private:
  bool send(MessageType type, std::string_view method, std::string_view payload);
  /** Answers FS callbacks as they arrive and returns the first response or error frame.
   */
  bool receive(Frame &frame, string &error);
  void answerCallback(const Frame &frame);
  bool fail(string &error, const char *message);

  Process m_process;
  ClientOptions m_options;
  string m_exe;
  string m_version;
  bool m_caseSensitive = false;
  bool m_broken = false;
  Vector<uint8_t, 4> m_inbox;
  RpcStats m_stats;
};

} // namespace fastlint::tsgo
