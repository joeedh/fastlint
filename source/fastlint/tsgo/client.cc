#include "fastlint/tsgo/client.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

namespace fastlint::tsgo {

namespace fs = std::filesystem;

namespace {

constexpr const char *kSupportedVersions[] = {"7.0.2"};

#ifdef _WIN32
constexpr const char *kPlatform = "win32";
constexpr const char *kExeName = "tsc.exe";
#elif defined(__APPLE__)
constexpr const char *kPlatform = "darwin";
constexpr const char *kExeName = "tsc";
#else
constexpr const char *kPlatform = "linux";
constexpr const char *kExeName = "tsc";
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
constexpr const char *kArch = "arm64";
#else
constexpr const char *kArch = "x64";
#endif

string fromStd(const std::string &text)
{
  return string(text.c_str());
}

std::string toStd(std::string_view text)
{
  return std::string(text);
}

bool isExecutableFile(const fs::path &path)
{
  std::error_code ec;
  return fs::is_regular_file(path, ec);
}

/** The native binary inside one `node_modules` directory, if the platform package is
 * there. */
bool findInNodeModules(const fs::path &nodeModules, string &exe)
{
  std::string package = std::string("typescript-") + kPlatform + "-" + kArch;
  fs::path tail = fs::path("@typescript") / package / "lib" / kExeName;
  fs::path candidates[] = {
      nodeModules / tail,
      nodeModules / "typescript" / "node_modules" / tail,
  };
  for (const fs::path &c : candidates) {
    if (isExecutableFile(c)) {
      exe = fromStd(c.generic_string());
      return true;
    }
  }
  // pnpm keeps the real package under .pnpm/<name>@<version>/node_modules/.
  std::error_code ec;
  fs::path store = nodeModules / ".pnpm";
  if (!fs::is_directory(store, ec)) {
    return false;
  }
  std::string prefix = "@typescript+" + package + "@";
  for (const fs::directory_entry &entry : fs::directory_iterator(store, ec)) {
    std::string name = entry.path().filename().string();
    if (name.rfind(prefix, 0) != 0) {
      continue;
    }
    fs::path c = entry.path() / "node_modules" / tail;
    if (isExecutableFile(c)) {
      exe = fromStd(c.generic_string());
      return true;
    }
  }
  return false;
}

bool findOnPath(string &exe)
{
  const char *env = getenv("PATH");
  if (!env) {
    return false;
  }
#ifdef _WIN32
  const char separator = ';';
#else
  const char separator = ':';
#endif
  std::string path(env);
  size_t start = 0;
  while (start <= path.size()) {
    size_t end = path.find(separator, start);
    if (end == std::string::npos) {
      end = path.size();
    }
    std::string dir = path.substr(start, end - start);
    if (!dir.empty()) {
      fs::path c = fs::path(dir) / kExeName;
      if (isExecutableFile(c)) {
        exe = fromStd(c.generic_string());
        return true;
      }
    }
    start = end + 1;
  }
  return false;
}

} // namespace

bool versionSupported(std::string_view version)
{
  for (const char *v : kSupportedVersions) {
    if (version == v) {
      return true;
    }
  }
  return false;
}

bool parseVersionOutput(std::string_view output, string &version)
{
  size_t i = 0;
  while (i < output.size() && !(output[i] >= '0' && output[i] <= '9')) {
    i++;
  }
  size_t start = i;
  int dots = 0;
  while (i < output.size() &&
         ((output[i] >= '0' && output[i] <= '9') || output[i] == '.' ||
          output[i] == '-' || (output[i] >= 'a' && output[i] <= 'z')))
  {
    if (output[i] == '.') {
      dots++;
    }
    i++;
  }
  if (i == start || dots == 0) {
    return false;
  }
  version = string();
  for (size_t k = start; k < i; k++) {
    version += output[k];
  }
  return true;
}

bool resolveTsgoExe(std::string_view startDir, string &exe)
{
  if (const char *env = getenv("FASTLINT_TSGO")) {
    if (*env && isExecutableFile(fs::path(env))) {
      exe = string(env);
      return true;
    }
  }
  std::error_code ec;
  fs::path dir = startDir.empty() ? fs::current_path(ec) : fs::path(toStd(startDir));
  dir = fs::absolute(dir, ec);
  while (!dir.empty()) {
    if (findInNodeModules(dir / "node_modules", exe)) {
      return true;
    }
    fs::path parent = dir.parent_path();
    if (parent == dir) {
      break;
    }
    dir = parent;
  }
  return findOnPath(exe);
}

// ---------------------------------------------------------------- Client

Client::~Client()
{
  stop();
}

bool Client::fail(string &error, const char *message)
{
  error = string(message);
  return false;
}

bool Client::start(const ClientOptions &options, string &error)
{
  if (m_process.started()) {
    return fail(error, "client already started");
  }
  m_options = options;
  m_exe = options.exe;
  if (m_exe.size() == 0 && !resolveTsgoExe(std::string_view(options.cwd.c_str()), m_exe))
  {
    return fail(error, "no native tsc found: set FASTLINT_TSGO or install typescript");
  }

  ProcessOptions versionProbe;
  versionProbe.exe = m_exe;
  versionProbe.args.append(string("--version"));
  string output;
  int exitCode = 0;
  if (!runCapture(versionProbe, output, exitCode, error)) {
    return false;
  }
  if (!parseVersionOutput(std::string_view(output.c_str(), output.size()), m_version)) {
    error = string("could not read a version from `tsc --version`: ");
    error += output;
    return false;
  }
  // A `tsc` named by `FASTLINT_TSGO` is a deliberate choice (a master build,
  // say), so an unknown version there is noted rather than refused.
  const char *named = getenv("FASTLINT_TSGO");
  bool explicitExe = named && std::string_view(named) == std::string_view(m_exe.c_str());
  if (explicitExe && !versionSupported(std::string_view(m_version.c_str()))) {
    std::fprintf(
        stderr,
        "tsc %s from FASTLINT_TSGO is not a version lintrix was probed against\n",
        m_version.c_str());
  } else if (!options.skipVersionCheck &&
             !versionSupported(std::string_view(m_version.c_str())))
  {
    error = string("unsupported tsc version ");
    error += m_version;
    error += " (supported: ";
    for (size_t i = 0; i < sizeof kSupportedVersions / sizeof kSupportedVersions[0]; i++)
    {
      if (i) {
        error += ", ";
      }
      error += kSupportedVersions[i];
    }
    error += ")";
    return false;
  }

  ProcessOptions server;
  server.exe = m_exe;
  server.args.append(string("--api"));
  string cwdArg("--cwd=");
  cwdArg += options.cwd;
  server.args.append(cwdArg);
  if (options.files) {
    server.args.append(string("--callbacks=readFile,fileExists"));
  }
  server.cwd = options.cwd;
  if (!m_process.start(server, error)) {
    return false;
  }

  JsonDocument result;
  if (!call("initialize", "", result, error)) {
    stop();
    return false;
  }
  m_caseSensitive = result.root() && result.root()->getBool("useCaseSensitiveFileNames");
  return true;
}

void Client::stop()
{
  if (!m_process.started()) {
    return;
  }
  m_process.closeStdin();
  for (int i = 0; i < 50 && m_process.running(); i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (m_process.running()) {
    m_process.kill();
  }
  m_process.wait();
  m_broken = true;
}

bool Client::alive()
{
  return m_process.started() && !m_broken && m_process.running();
}

bool Client::send(MessageType type, std::string_view method, std::string_view payload)
{
  Vector<uint8_t, 4> frame;
  encodeFrame(frame, type, method, payload);
  m_stats.bytesSent += frame.size();
  if (!m_process.write(span<const uint8_t>(frame.data(), frame.size()))) {
    m_broken = true;
    return false;
  }
  return true;
}

bool Client::receive(Frame &frame, string &error)
{
  uint8_t buffer[65536];
  while (true) {
    size_t consumed = 0;
    string decodeError;
    DecodeStatus status = decodeFrame(span<const uint8_t>(m_inbox.data(), m_inbox.size()),
                                      frame,
                                      consumed,
                                      &decodeError);
    if (status == DecodeStatus::Malformed) {
      m_broken = true;
      error = string("malformed frame from tsc: ");
      error += decodeError;
      return false;
    }
    if (status == DecodeStatus::Complete) {
      m_inbox = m_inbox.slice(int(consumed));
      if (frame.type == MessageType::Call) {
        answerCallback(frame);
        continue;
      }
      return true;
    }
    auto readStart = std::chrono::steady_clock::now();
    int n = m_process.read(buffer, int(sizeof buffer));
    m_stats.readSeconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - readStart)
            .count();
    if (n <= 0) {
      m_broken = true;
      return fail(error, "tsc closed the connection");
    }
    m_stats.bytesReceived += size_t(n);
    m_inbox.concat(span<const uint8_t>(buffer, size_t(n)));
  }
}

void Client::answerCallback(const Frame &frame)
{
  m_stats.callbacks++;
  JsonDocument params;
  std::string_view path;
  if (params.parse(frame.payloadText()) && params.root()) {
    path = params.root()->asString();
  }
  string reply("null");
  if (m_options.files && !path.empty()) {
    std::string_view method(frame.method.c_str(), frame.method.size());
    if (method == "readFile") {
      string content;
      if (m_options.files->readFile(path, content)) {
        JsonWriter w;
        w.beginObject();
        w.member("content", std::string_view(content.c_str(), content.size()));
        w.endObject();
        reply = string();
        reply += w.str();
      }
    } else if (method == "fileExists") {
      int exists = m_options.files->fileExists(path);
      if (exists >= 0) {
        reply = string(exists ? "true" : "false");
      }
    }
  }
  send(MessageType::CallResponse,
       std::string_view(frame.method.c_str(), frame.method.size()),
       std::string_view(reply.c_str(), reply.size()));
}

bool Client::callRaw(std::string_view method,
                     std::string_view params,
                     Vector<uint8_t, 4> &payload,
                     string &error)
{
  if (!m_process.started() || m_broken) {
    return fail(error, "tsc client is not connected");
  }
  m_stats.calls++;
  if (!send(MessageType::Request, method, params.empty() ? "null" : params)) {
    return fail(error, "could not write to tsc");
  }
  Frame frame;
  if (!receive(frame, error)) {
    return false;
  }
  if (frame.type == MessageType::Error || frame.type == MessageType::CallError) {
    error = string();
    for (char c : frame.payloadText()) {
      error += c;
    }
    return false;
  }
  if (frame.type != MessageType::Response) {
    m_broken = true;
    return fail(error, "unexpected message type from tsc");
  }
  payload = std::move(frame.payload);
  return true;
}

bool Client::call(std::string_view method,
                  std::string_view params,
                  JsonDocument &result,
                  string &error)
{
  Vector<uint8_t, 4> payload;
  if (!callRaw(method, params, payload, error)) {
    return false;
  }
  std::string_view text(reinterpret_cast<const char *>(payload.data()), payload.size());
  if (text.empty()) {
    text = "null";
  }
  if (!result.parse(text)) {
    error = string("unparsable response from tsc ");
    for (char c : method) {
      error += c;
    }
    error += ": ";
    error += result.error();
    return false;
  }
  return true;
}

// ---------------------------------------------------------------- snapshots

namespace {

void writeList(JsonWriter &w, const char *name, const Vector<string, 1> &items)
{
  if (items.isEmpty()) {
    return;
  }
  w.key(name);
  w.beginArray();
  for (const string &item : items) {
    w.value(std::string_view(item.c_str(), item.size()));
  }
  w.endArray();
}

} // namespace

bool Client::updateSnapshot(const SnapshotUpdate &update,
                            SnapshotInfo &info,
                            string &error)
{
  JsonWriter w;
  w.beginObject();
  writeList(w, "openProjects", update.openProjects);
  writeList(w, "closeProjects", update.closeProjects);
  writeList(w, "openFiles", update.openFiles);
  writeList(w, "closeFiles", update.closeFiles);
  bool hasChanges = update.invalidateAll || !update.changed.isEmpty() ||
                    !update.created.isEmpty() || !update.deleted.isEmpty();
  if (hasChanges) {
    w.key("fileChanges");
    w.beginObject();
    if (update.invalidateAll) {
      w.member("invalidateAll", true);
    }
    writeList(w, "changed", update.changed);
    writeList(w, "created", update.created);
    writeList(w, "deleted", update.deleted);
    w.endObject();
  }
  w.endObject();

  JsonDocument result;
  if (!call("updateSnapshot", w.text(), result, error)) {
    return false;
  }
  const JsonValue *root = result.root();
  if (!root || !root->isObject()) {
    return fail(error, "updateSnapshot returned no snapshot");
  }
  info.id = root->getInt("snapshot");
  info.projects.clear();
  info.changedFiles.clear();
  if (const JsonValue *projects = root->get("projects")) {
    for (int i = 0; i < projects->size(); i++) {
      const JsonValue *p = projects->at(i);
      ProjectInfo project;
      for (char c : p->getString("id")) {
        project.id += c;
      }
      for (char c : p->getString("configFileName")) {
        project.configFileName += c;
      }
      info.projects.append(std::move(project));
    }
  }
  if (const JsonValue *changes = root->get("changes")) {
    if (const JsonValue *byProject = changes->get("changedProjects")) {
      for (int i = 0; i < byProject->size(); i++) {
        const JsonValue *files = byProject->at(i)->get("changedFiles");
        for (int k = 0; files && k < files->size(); k++) {
          string file;
          for (char c : files->at(k)->asString()) {
            file += c;
          }
          info.changedFiles.append(std::move(file));
        }
      }
    }
  }
  return true;
}

bool Client::openProject(std::string_view tsconfig, SnapshotInfo &info, string &error)
{
  SnapshotUpdate update;
  string path;
  for (char c : tsconfig) {
    path += c;
  }
  update.openProjects.append(std::move(path));
  if (!updateSnapshot(update, info, error)) {
    return false;
  }
  if (info.projects.isEmpty()) {
    error = string("no project loaded for ");
    for (char c : tsconfig) {
      error += c;
    }
    return false;
  }
  return true;
}

bool Client::parseConfigFile(std::string_view tsconfig,
                             JsonDocument &result,
                             string &error)
{
  JsonWriter w;
  w.beginObject();
  // `file` is a DocumentIdentifier, whose decoder takes a plain string as the file name.
  w.member("file", tsconfig);
  w.endObject();
  if (!call("parseConfigFile", w.text(), result, error)) {
    return false;
  }
  if (!result.root() || !result.root()->get("options")) {
    error = string("parseConfigFile answered without options");
    return false;
  }
  return true;
}

bool Client::release(int snapshot, string &error)
{
  JsonWriter w;
  w.beginObject();
  w.member("snapshot", snapshot);
  w.endObject();
  JsonDocument result;
  return call("release", w.text(), result, error);
}

bool Client::defaultProjectForFile(int snapshot,
                                   std::string_view file,
                                   string &projectId,
                                   string &error)
{
  JsonWriter w;
  w.beginObject();
  w.member("snapshot", snapshot);
  w.member("file", file);
  w.endObject();
  JsonDocument result;
  if (!call("getDefaultProjectForFile", w.text(), result, error)) {
    return false;
  }
  projectId = string();
  const JsonValue *root = result.root();
  std::string_view id =
      root && root->isObject() ? root->getString("id") : root->asString();
  for (char c : id) {
    projectId += c;
  }
  return true;
}

} // namespace fastlint::tsgo
