// The `serve` command (task 9.5): a resident linter an editor drives over
// stdio, so type-aware rules see the open buffer and no lint pays tsgo startup
// (docs/vscode-extension.md "The native serve mode").

#include "cli/files.h"
#include "cli/run.h"
#include "fastlint/cache/closure.h"
#include "fastlint/lint/config.h"
#include "fastlint/lint/format.h"
#include "fastlint/lint/linter.h"
#include "fastlint/lint/registry.h"
#include "fastlint/tsgo/json.h"
#include "fastlint/tsgo/source_file.h"
#include "fastlint/types/type_source.h"
#include "util/alloc.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace fastlint::cli {

namespace {

using litestl::util::span;
using litestl::util::string;
using litestl::util::Vector;
using std::string_view;
using tsgo::JsonDocument;
using tsgo::JsonValue;
using tsgo::JsonWriter;

namespace fs = std::filesystem;

void usage(std::FILE *out = stderr)
{
  std::fprintf(out, "usage: lintrix serve [--no-cache] [--cache-dir <dir>]\n");
}

void help()
{
  usage(stdout);
  std::fprintf(
      stdout,
      "\n"
      "Lints on request over stdio, JSON-RPC 2.0 with Content-Length framing,\n"
      "keeping the type server and the result cache open between requests.\n"
      "\n"
      "requests:\n"
      "  lint {file, text?, config?}   lint a file; `text` overlays the disk\n"
      "  close {file}                  drop a file's overlay\n"
      "  changed {files}               files changed on disk\n"
      "  configChanged {path?}         reload a config or tsconfig, or all\n"
      "  shutdown                      answer and exit\n"
      "\n"
      "options:\n"
      "  --no-cache             do not read or write the result cache\n"
      "  --cache-dir <dir>      result-cache directory (default: node_modules/.cache)\n"
      "  -h, --help             show this help\n");
}

std::string_view view(const string &s)
{
  return std::string_view(s.c_str(), s.size());
}

string toString(std::string_view text)
{
  string out;
  out += std::string(text);
  return out;
}

/** `path` made absolute with forward slashes, folded to lower case where the file
 * system ignores case, so two spellings of one file compare equal. */
std::string canonical(std::string_view path)
{
  std::error_code ec;
  std::string out = fs::absolute(fs::path(std::string(path)), ec).generic_string();
#ifdef _WIN32
  for (char &c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = char(c - 'A' + 'a');
    }
  }
#endif
  return out;
}

/**
 * One loaded config and everything a lint under it keeps alive between
 * requests: the type server with every tsconfig its files have needed, and
 * the result cache under the config's directory.
 */
struct Session {
  /** Canonical, or empty for the recommended preset. */
  std::string key;
  std::string configPath;
  lint::Config config;
  fs::path baseDir;
  types::ProjectTypes types;
  /** Set once `open` was attempted; a failure is not retried until the session is
   * dropped. */
  bool typesTried = false;
  std::string typeError;
  /** Canonical paths of the tsconfigs opened in the type server. */
  Vector<std::string> projects;
  ResultCache cache;
  bool cacheTried = false;
};

struct LintOutcome {
  lint::FileResult result;
  bool typed = false;
  std::string typeError;
};

class Server {
public:
  Server(const lint::Registry &registry, bool noCache, const char *cacheDir)
      : m_registry(registry), m_noCache(noCache), m_cacheDir(cacheDir)
  {
  }
  ~Server()
  {
    dropAll();
  }

  /** Lints `file` (`text` in place of the disk when given) under `configPath`, or
   * the config found upward from the file when empty. False with `error` when the
   * config or the file cannot be read. */
  bool lint(std::string_view file,
            const std::string *text,
            std::string_view configPath,
            LintOutcome &out,
            std::string &error);
  void closeFile(std::string_view file);
  void filesChanged(const Vector<string> &files);
  /** Drops every session whose config or tsconfig is `path`; every session when
   * `path` is empty. */
  void configChanged(std::string_view path);

private:
  Session *
  sessionFor(std::string_view file, std::string_view configPath, std::string &error);
  /** The tsconfig `file` is typed with under `session`, or empty. */
  std::string projectFor(Session &session, const fs::path &file);
  void openCache(Session &session);
  void drop(int index);
  void dropAll();

  const lint::Registry &m_registry;
  bool m_noCache;
  const char *m_cacheDir;
  Vector<Session *> m_sessions;
};

Session *
Server::sessionFor(std::string_view file, std::string_view configPath, std::string &error)
{
  std::string path(configPath);
  if (path.empty()) {
    std::string dir = fs::path(std::string(file)).parent_path().generic_string();
    string found = lint::Config::find(std::string_view(dir));
    path = std::string(view(found));
  }
  std::string key = path.empty() ? std::string() : canonical(path);
  for (Session *session : m_sessions) {
    if (session->key == key) {
      return session;
    }
  }
  Session *session = litestl::alloc::New<Session>("serve session");
  session->key = key;
  session->configPath = path;
  string loadError;
  if (path.empty()) {
    session->config.parse(
        "{\"extends\": \"lintrix:recommended\"}", "", m_registry, loadError);
    session->baseDir = fs::path(std::string(file)).parent_path();
  } else if (session->config.load(std::string_view(path), m_registry, loadError)) {
    session->baseDir = fs::path(path).parent_path();
  } else {
    error = path + ": " + std::string(view(loadError));
    litestl::alloc::Delete(session);
    return nullptr;
  }
  m_sessions.append(session);
  return session;
}

std::string Server::projectFor(Session &session, const fs::path &file)
{
  string configProj = session.config.projectFor(std::string_view(file.generic_string()));
  if (configProj.size() > 0) {
    return std::string(view(configProj));
  }
  std::string proj = tsconfigUpwards(file.parent_path());
  if (proj.empty() && !session.configPath.empty()) {
    fs::path sibling = session.baseDir / "tsconfig.json";
    std::error_code ec;
    if (fs::is_regular_file(sibling, ec)) {
      proj = sibling.generic_string();
    }
  }
  return proj;
}

void Server::openCache(Session &session)
{
  session.cacheTried = true;
  std::string dir = cacheDirFor(m_cacheDir, m_noCache, false, session.baseDir);
  if (dir.empty()) {
    return;
  }
  std::error_code ec;
  fs::create_directories(dir, ec);
  // The tsconfigs are not known up front, so each file's own goes into its cache
  // key rather than into this hash.
  uint64_t env = environmentHash(toString(session.configPath),
                                 false,
                                 session.configPath.empty(),
                                 Vector<std::string>(),
                                 Vector<std::string>(),
                                 session.baseDir);
  session.cache.open(dir + "/lint.db", env, Vector<fs::path>());
}

bool Server::lint(std::string_view file,
                  const std::string *text,
                  std::string_view configPath,
                  LintOutcome &out,
                  std::string &error)
{
  std::error_code ec;
  fs::path path = fs::absolute(fs::path(std::string(file)), ec);
  std::string name = path.generic_string();
  Session *session = sessionFor(name, configPath, error);
  if (!session) {
    return false;
  }

  std::string disk;
  bool onDisk = readFile(path, disk);
  if (!text && !onDisk) {
    error = name + ": cannot read";
    return false;
  }
  const std::string &source = text ? *text : disk;

  std::string proj = projectFor(*session, path);
  if (!proj.empty() && !session->typesTried) {
    session->typesTried = true;
    Vector<string> list;
    list.append(toString(proj));
    string typeError;
    if (session->types.open(list, typeError)) {
      session->projects.append(canonical(proj));
    } else {
      session->typeError = std::string(view(typeError));
      std::fprintf(stderr, "type-aware rules disabled (%s)\n", typeError.c_str());
    }
  }
  if (!proj.empty() && session->types.isOpen()) {
    if (!session->types.hasProject(proj)) {
      string addError;
      if (session->types.addProject(proj, addError)) {
        session->projects.append(canonical(proj));
      } else {
        std::fprintf(stderr, "%s: cannot open: %s\n", proj.c_str(), addError.c_str());
      }
    }
    if (session->types.hasProject(proj)) {
      session->types.setFileProject(name, proj);
    }
  }

  // An unsaved buffer matches no file on disk, so it is neither replayed nor
  // stored; the saved file is, so a reopen replays.
  bool cacheable = onDisk && (!text || *text == disk);
  if (cacheable && !session->cacheTried) {
    openCache(*session);
  }
  std::string key = "@lint+fix";
  if (!proj.empty()) {
    std::string tsconfig;
    readFile(fs::path(proj), tsconfig);
    char hex[32];
    std::snprintf(hex,
                  sizeof hex,
                  "#%016llx",
                  static_cast<unsigned long long>(cache::hashContent(tsconfig)));
    key += hex;
  }

  lint::FileResult &result = out.result;
  bool hit = false;
  if (cacheable && session->cache.ready) {
    session->cache.refresh(name);
    hit = session->cache.load(name, key, m_registry, result);
  }
  if (hit) {
    result.filename = toString(name);
  } else {
    lint::Linter linter(m_registry, session->config);
    lint::LintOptions options;
    options.fixEdits = true;
    if (!proj.empty() && session->types.isOpen()) {
      options.types = &session->types;
    }
    linter.lintSource(source, name, options, result);
    // A degraded run (the server could not type the file) is not cached.
    if (cacheable && session->cache.ready && !result.ignored &&
        result.typeError.size() == 0)
    {
      session->cache.save(name, key, result);
    }
  }

  // A failed query mid-walk leaves the file typed; the message says what was lost.
  out.typed = !proj.empty() && session->types.isOpen() && (hit || result.typed);
  if (!session->typeError.empty()) {
    out.typeError = session->typeError;
  } else if (result.typeError.size() > 0) {
    out.typeError = std::string(view(result.typeError));
  } else if (proj.empty() && isTypeScript(path)) {
    out.typeError = "no tsconfig resolved";
  }
  return true;
}

void Server::closeFile(std::string_view file)
{
  for (Session *session : m_sessions) {
    string error;
    if (!session->types.forgetFile(file, error)) {
      std::fprintf(stderr, "type server: %s\n", error.c_str());
    }
  }
}

void Server::filesChanged(const Vector<string> &files)
{
  for (const string &file : files) {
    configChanged(view(file));
  }
  for (Session *session : m_sessions) {
    string error;
    if (!session->types.filesChanged(files, error)) {
      std::fprintf(stderr, "type server: %s\n", error.c_str());
    }
  }
}

void Server::configChanged(std::string_view path)
{
  if (path.empty()) {
    dropAll();
    return;
  }
  std::string key = canonical(path);
  for (int i = int(m_sessions.size()); i-- > 0;) {
    Session *session = m_sessions[i];
    bool matches = session->key == key;
    for (const std::string &project : session->projects) {
      matches = matches || project == key;
    }
    if (matches) {
      drop(i);
    }
  }
}

void Server::drop(int index)
{
  litestl::alloc::Delete(m_sessions[index]);
  m_sessions.remove_at(index);
}

void Server::dropAll()
{
  while (!m_sessions.isEmpty()) {
    drop(int(m_sessions.size()) - 1);
  }
}

// The transport: LSP's base protocol, `Content-Length: N` headers, a blank
// line, then N bytes of JSON.

/** Reads one message body; false at end of input or on a malformed header. */
bool readMessage(std::string &body)
{
  size_t length = std::string::npos;
  for (;;) {
    std::string line;
    for (;;) {
      int c = std::fgetc(stdin);
      if (c == EOF) {
        return false;
      }
      if (c == '\n') {
        break;
      }
      line += char(c);
    }
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      break;
    }
    const char *header = "Content-Length:";
    if (line.compare(0, std::strlen(header), header) == 0) {
      length = size_t(std::atoll(line.c_str() + std::strlen(header)));
    }
  }
  if (length == std::string::npos) {
    std::fprintf(stderr, "serve: a message without Content-Length\n");
    return false;
  }
  body.resize(length);
  size_t got = length > 0 ? std::fread(body.data(), 1, length, stdin) : 0;
  return got == length;
}

void writeMessage(std::string_view body)
{
  std::fprintf(stdout, "Content-Length: %zu\r\n\r\n", body.size());
  std::fwrite(body.data(), 1, body.size(), stdout);
  std::fflush(stdout);
}

/** Copies a request's `id` into the response; JSON-RPC allows a number or a string.
 */
void writeId(JsonWriter &w, const JsonValue *id)
{
  w.key("id");
  if (!id || id->isNull()) {
    w.null();
  } else if (id->isString()) {
    w.value(id->asString());
  } else {
    w.value(id->asDouble());
  }
}

void respondError(const JsonValue *id, int code, std::string_view message)
{
  JsonWriter w;
  w.beginObject();
  w.member("jsonrpc", "2.0");
  writeId(w, id);
  w.key("error");
  w.beginObject();
  w.member("code", code);
  w.member("message", message);
  w.endObject();
  w.endObject();
  writeMessage(w.text());
}

/** Starts a success response; the caller writes the result value, then `endResult`.
 */
void beginResult(JsonWriter &w, const JsonValue *id)
{
  w.beginObject();
  w.member("jsonrpc", "2.0");
  writeId(w, id);
  w.key("result");
}

void endResult(JsonWriter &w)
{
  w.endObject();
  writeMessage(w.text());
}

constexpr int invalidRequest = -32600;
constexpr int methodNotFound = -32601;
constexpr int invalidParams = -32602;
constexpr int lintFailed = -32000;

/** Handles one message. Returns false once `shutdown` has been answered. */
bool dispatch(Server &server, const JsonValue &message)
{
  const JsonValue *id = message.get("id");
  bool isRequest = id && !id->isNull();
  std::string_view method = message.getString("method");
  const JsonValue *params = message.get("params");
  if (method.empty()) {
    if (isRequest) {
      respondError(id, invalidRequest, "a request without a method");
    }
    return true;
  }

  if (method == "lint") {
    std::string_view file = params ? params->getString("file") : std::string_view();
    if (file.empty()) {
      respondError(id, invalidParams, "lint needs a file");
      return true;
    }
    const JsonValue *textValue = params->get("text");
    std::string text;
    bool hasText = textValue && textValue->isString();
    if (hasText) {
      text = std::string(textValue->asString());
    }
    LintOutcome outcome;
    std::string error;
    if (!server.lint(
            file, hasText ? &text : nullptr, params->getString("config"), outcome, error))
    {
      respondError(id, lintFailed, error);
      return true;
    }
    JsonWriter w;
    beginResult(w, id);
    w.beginObject();
    w.key("results");
    if (outcome.result.ignored) {
      w.beginArray();
      w.endArray();
    } else {
      string json;
      lint::formatJson(span<const lint::FileResult>(&outcome.result, 1), json);
      w.raw(view(json));
    }
    w.member("typed", outcome.typed);
    if (!outcome.typeError.empty()) {
      w.member("typeError", outcome.typeError);
    }
    w.endObject();
    endResult(w);
    return true;
  }

  if (method == "close") {
    std::string_view file = params ? params->getString("file") : std::string_view();
    if (!file.empty()) {
      server.closeFile(file);
    }
  } else if (method == "changed") {
    const JsonValue *files = params ? params->get("files") : nullptr;
    Vector<string> list;
    for (int i = 0; files && i < files->size(); i++) {
      const JsonValue *entry = files->at(i);
      if (entry && entry->isString()) {
        list.append(toString(entry->asString()));
      }
    }
    server.filesChanged(list);
  } else if (method == "configChanged") {
    server.configChanged(params ? params->getString("path") : std::string_view());
  } else if (method == "shutdown") {
    if (isRequest) {
      JsonWriter w;
      beginResult(w, id);
      w.null();
      endResult(w);
    }
    return false;
  } else if (method == "exit") {
    return false;
  } else {
    if (isRequest) {
      std::string text = "unknown method '" + std::string(method) + "'";
      respondError(id, methodNotFound, text);
    }
    return true;
  }
  if (isRequest) {
    JsonWriter w;
    beginResult(w, id);
    w.null();
    endResult(w);
  }
  return true;
}

} // namespace

int serveCommand(int argc, char **argv)
{
  bool noCache = false;
  const char *cacheDir = nullptr;
  for (int i = 2; i < argc; i++) {
    const char *arg = argv[i];
    if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
      help();
      return 0;
    } else if (std::strcmp(arg, "--no-cache") == 0) {
      noCache = true;
    } else if (std::strcmp(arg, "--cache-dir") == 0 && i + 1 < argc) {
      cacheDir = argv[++i];
    } else {
      std::fprintf(stderr, "unknown option '%s'\n", arg);
      usage();
      return 2;
    }
  }
#ifdef _WIN32
  // Text mode would turn the framing's CRLF into LF and stop at a ^Z byte.
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  Server server(lint::builtinRegistry(), noCache, cacheDir);
  std::string body;
  while (readMessage(body)) {
    JsonDocument doc;
    if (!doc.parse(body) || !doc.root() || !doc.root()->isObject()) {
      respondError(nullptr, -32700, "the message is not a JSON object");
      continue;
    }
    if (!dispatch(server, *doc.root())) {
      break;
    }
  }
  return 0;
}

} // namespace fastlint::cli
