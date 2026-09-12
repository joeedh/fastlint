// Drives `lintrix serve` (task 9.5) over its own stdio: a buffer overlay, a
// config change and a shutdown, against the built binary.

#include "fastlint/tsgo/client.h"
#include "fastlint/tsgo/json.h"
#include "fastlint/tsgo/process.h"
#include "testing/test.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace fastlint;
using litestl::util::span;
using litestl::util::string;

namespace {

namespace fs = std::filesystem;

std::string repoRoot()
{
  return fs::path(FASTLINT_TESTS_DIR).parent_path().generic_string();
}

bool haveTsgo()
{
  string exe;
  return tsgo::resolveTsgoExe(std::string_view(repoRoot().c_str()), exe);
}

void writeText(const fs::path &path, const std::string &text)
{
  std::ofstream out(path, std::ios::binary);
  out << text;
}

/** A project directory under the temp dir, removed with the guard. */
struct Project {
  fs::path dir;

  Project()
  {
    dir = fs::temp_directory_path() /
          ("fastlint-serve-test-" + std::to_string(uint64_t(this)));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "node_modules", ec);
    writeText(dir / "tsconfig.json", "{\"compilerOptions\":{\"strict\":true}}\n");
    writeText(dir / "lintrix.config.json", "{\"extends\":\"lintrix:recommended\"}\n");
    writeText(dir / "a.ts", "async function f(): Promise<void> {}\nf();\n");
  }
  ~Project()
  {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  std::string file(const char *name) const
  {
    return (dir / name).generic_string();
  }
};

/** The serve process with the framing on this side; `error` explains a false `ok`. */
struct Serve {
  tsgo::Process process;
  string error;
  bool ok = false;
  std::string inbox;
  int nextId = 1;

  Serve()
  {
    tsgo::ProcessOptions options;
    options.exe = string(LINTRIX_EXE_PATH);
    options.args.append(string("serve"));
    // tsc resolves from the working directory when the project has none.
    options.cwd = string(repoRoot().c_str());
    ok = process.start(options, error);
  }

  bool send(const std::string &body)
  {
    std::string frame =
        "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    return process.write(span<const uint8_t>(
        reinterpret_cast<const uint8_t *>(frame.data()), frame.size()));
  }

  /** Reads one framed message into `doc`; false at end of stream or on bad framing. */
  bool receive(tsgo::JsonDocument &doc)
  {
    for (;;) {
      size_t headerEnd = inbox.find("\r\n\r\n");
      if (headerEnd != std::string::npos) {
        size_t at = inbox.find("Content-Length:");
        if (at == std::string::npos || at > headerEnd) {
          return false;
        }
        size_t length = size_t(std::atoll(inbox.c_str() + at + 15));
        if (inbox.size() >= headerEnd + 4 + length) {
          std::string body = inbox.substr(headerEnd + 4, length);
          inbox.erase(0, headerEnd + 4 + length);
          return doc.parse(body);
        }
      }
      uint8_t buffer[4096];
      int got = process.read(buffer, sizeof buffer);
      if (got <= 0) {
        return false;
      }
      inbox.append(reinterpret_cast<const char *>(buffer), size_t(got));
    }
  }

  /** Issues `method` with `params` (JSON text) and parses the response. */
  bool call(const char *method, const std::string &params, tsgo::JsonDocument &doc)
  {
    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(nextId++) +
                       ",\"method\":\"" + method + "\",\"params\":" + params + "}";
    return send(body) && receive(doc);
  }
};

std::string quoted(const std::string &text)
{
  std::string out;
  tsgo::appendJsonString(out, text);
  return out;
}

/** The rule ids of the first result's messages, comma separated. */
std::string ruleIds(const tsgo::JsonDocument &doc)
{
  const tsgo::JsonValue *result = doc.root() ? doc.root()->get("result") : nullptr;
  const tsgo::JsonValue *results = result ? result->get("results") : nullptr;
  const tsgo::JsonValue *first = results ? results->at(0) : nullptr;
  const tsgo::JsonValue *messages = first ? first->get("messages") : nullptr;
  std::string out;
  for (int i = 0; messages && i < messages->size(); i++) {
    if (i > 0) {
      out += ",";
    }
    out += std::string(messages->at(i)->getString("ruleId"));
  }
  return out;
}

bool typed(const tsgo::JsonDocument &doc)
{
  const tsgo::JsonValue *result = doc.root() ? doc.root()->get("result") : nullptr;
  return result && result->getBool("typed");
}

} // namespace

TEST_TAGGED(cli_serve, lints_overlays_and_reloads_configs, "integration")
{
  if (!haveTsgo()) {
    SKIP("no native tsc found");
  }
  Project project;
  Serve serve;
  REQUIRE(serve.ok);
  std::string file = quoted(project.file("a.ts"));
  tsgo::JsonDocument doc;

  // The saved file, typed through the project's tsconfig.
  REQUIRE(serve.call("lint", "{\"file\":" + file + "}", doc));
  CHECK_EQ(ruleIds(doc), std::string("no-floating-promises"));
  CHECK(typed(doc));

  // An overlay replaces the disk for the type server as well.
  std::string fixed = quoted("async function f(): Promise<void> {}\nvoid f();\n");
  REQUIRE(serve.call("lint", "{\"file\":" + file + ",\"text\":" + fixed + "}", doc));
  CHECK_EQ(ruleIds(doc), std::string());
  CHECK(typed(doc));

  std::string broken = quoted("async function f(): Promise<void> {}\nf();\ndebugger;\n");
  REQUIRE(serve.call("lint", "{\"file\":" + file + ",\"text\":" + broken + "}", doc));
  CHECK_EQ(ruleIds(doc), std::string("no-floating-promises,no-debugger"));

  // A changed config is reloaded, and the saved file reads from disk after close.
  writeText(project.dir / "lintrix.config.json",
            "{\"extends\":\"lintrix:recommended\",\"rules\":{\"no-floating-promises\":"
            "\"off\"}}\n");
  std::string config = quoted(project.file("lintrix.config.json"));
  REQUIRE(serve.call("configChanged", "{\"path\":" + config + "}", doc));
  REQUIRE(serve.call("close", "{\"file\":" + file + "}", doc));
  REQUIRE(serve.call("lint", "{\"file\":" + file + "}", doc));
  CHECK_EQ(ruleIds(doc), std::string());
  CHECK(typed(doc));

  // A buffer with no file behind it lints under the same config.
  std::string untitled = quoted(project.file("untitled.ts"));
  REQUIRE(serve.call(
      "lint", "{\"file\":" + untitled + ",\"text\":" + quoted("debugger;\n") + "}", doc));
  CHECK_EQ(ruleIds(doc), std::string("no-debugger"));

  REQUIRE(serve.call("nope", "{}", doc));
  const tsgo::JsonValue *error = doc.root()->get("error");
  REQUIRE(error != nullptr);
  CHECK_EQ(error->getInt("code"), -32601);

  REQUIRE(serve.call("shutdown", "null", doc));
  CHECK(doc.root()->get("result") != nullptr);
  serve.process.closeStdin();
  CHECK_EQ(serve.process.wait(), 0);
}
