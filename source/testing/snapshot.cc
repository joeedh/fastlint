#include "testing/snapshot.h"
#include "testing/runner.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <dtl/dtl.hpp>

#ifndef FASTLINT_TESTS_DIR
#define FASTLINT_TESTS_DIR "tests"
#endif

namespace fastlint::test {

namespace {

/**
 * One `.snap` file. std containers are used throughout because this is a file
 * format adapter over std::filesystem, std::fstream and DTL, none of which
 * speak litestl types.
 */
struct SnapshotFile {
  std::string path;
  std::map<std::string, std::string> entries;
  std::set<std::string> used;
  bool dirty = false;
  bool existed = false;
  int newEntries = 0;
};

std::map<std::string, SnapshotFile> &files()
{
  static std::map<std::string, SnapshotFile> value;
  return value;
}

/** Counters for unlabelled snapshots, keyed by test and subcase path. */
std::map<std::string, int> &counters()
{
  static std::map<std::string, int> value;
  return value;
}

std::string escapeLine(const std::string &line)
{
  // A stored line that would otherwise read as a key header gets one backslash.
  size_t slashes = 0;
  while (slashes < line.size() && line[slashes] == '\\') {
    slashes++;
  }
  if (line.compare(slashes, 4, "### ") == 0) {
    return "\\" + line;
  }
  return line;
}

std::string unescapeLine(const std::string &line)
{
  if (line.empty() || line[0] != '\\') {
    return line;
  }
  size_t slashes = 1;
  while (slashes < line.size() && line[slashes] == '\\') {
    slashes++;
  }
  if (line.compare(slashes, 4, "### ") == 0) {
    return line.substr(1);
  }
  return line;
}

bool isKeyLine(const std::string &line)
{
  return line.compare(0, 4, "### ") == 0;
}

std::vector<std::string> splitLines(const std::string &text)
{
  std::vector<std::string> lines;
  std::string current;
  for (const char c : text) {
    if (c == '\n') {
      lines.push_back(current);
      current.clear();
    } else if (c != '\r') {
      current.push_back(c);
    }
  }
  if (!current.empty()) {
    lines.push_back(current);
  }
  return lines;
}

void load(SnapshotFile &file)
{
  std::ifstream in(file.path, std::ios::binary);
  if (!in) {
    return;
  }
  file.existed = true;
  std::stringstream buffer;
  buffer << in.rdbuf();

  std::string key;
  std::string value;
  bool haveKey = false;
  const auto flush = [&]() {
    if (!haveKey) {
      return;
    }
    // Writing always puts one blank line after a value; take it back off.
    while (!value.empty() && value.back() == '\n') {
      value.pop_back();
    }
    file.entries[key] = value;
  };

  for (const std::string &raw : splitLines(buffer.str())) {
    if (isKeyLine(raw)) {
      flush();
      key = raw.substr(4);
      value.clear();
      haveKey = true;
      continue;
    }
    if (!haveKey) {
      continue;
    }
    value += unescapeLine(raw);
    value += "\n";
  }
  flush();
}

void save(const SnapshotFile &file)
{
  std::filesystem::create_directories(std::filesystem::path(file.path).parent_path());
  std::ofstream out(file.path, std::ios::binary);
  for (const auto &[key, value] : file.entries) {
    out << "### " << key << "\n";
    for (const std::string &line : splitLines(value)) {
      out << escapeLine(line) << "\n";
    }
    out << "\n";
  }
}

std::string snapshotPathFor(const char *testFile)
{
  const std::string base = std::filesystem::path(testFile).filename().string();
  return (std::filesystem::path(FASTLINT_TESTS_DIR) / "__snapshots__" / (base + ".snap"))
      .string();
}

SnapshotFile &fileFor(const char *testFile)
{
  const std::string path = snapshotPathFor(testFile);
  auto it = files().find(path);
  if (it == files().end()) {
    SnapshotFile file;
    file.path = path;
    load(file);
    it = files().emplace(path, std::move(file)).first;
  }
  return it->second;
}

/** Unified diff with three lines of context, `-` stored and `+` actual. */
std::string renderDiff(const std::string &stored, const std::string &actual, bool color)
{
  const std::vector<std::string> a = splitLines(stored);
  const std::vector<std::string> b = splitLines(actual);

  dtl::Diff<std::string, std::vector<std::string>> diff(a, b);
  diff.compose();
  diff.composeUnifiedHunks();

  const char *red = color ? "\x1b[31m" : "";
  const char *green = color ? "\x1b[32m" : "";
  const char *cyan = color ? "\x1b[36m" : "";
  const char *reset = color ? "\x1b[0m" : "";

  std::ostringstream out;
  for (const auto &hunk : diff.getUniHunks()) {
    out << cyan << "@@ -" << hunk.a << "," << hunk.b << " +" << hunk.c << "," << hunk.d
        << " @@" << reset << "\n";
    const auto emit =
        [&](const std::vector<std::pair<std::string, dtl::elemInfo>> &elems) {
          for (const auto &[line, info] : elems) {
            switch (info.type) {
            case dtl::SES_DELETE:
              out << red << "-" << line << reset << "\n";
              break;
            case dtl::SES_ADD:
              out << green << "+" << line << reset << "\n";
              break;
            default:
              out << " " << line << "\n";
              break;
            }
          }
        };
    emit(hunk.common[0]);
    emit(hunk.change);
    emit(hunk.common[1]);
  }
  return out.str();
}

std::string keyFor(const char *label)
{
  const TestCase *test = currentTest();
  std::string key =
      test ? std::string(test->suite) + "." + test->name : std::string("<none>");
  const string path = currentSubcasePath();
  if (path.size() > 0) {
    key += " / ";
    key += path.c_str();
  }
  if (label) {
    key += " ";
    key += label;
    return key;
  }
  const int index = ++counters()[key];
  key += " #";
  key += std::to_string(index);
  return key;
}

bool shouldUpdate(const std::string &key)
{
  const Options &opts = options();
  if (!opts.update) {
    return false;
  }
  if (opts.updateGlob.size() == 0) {
    return true;
  }
  return globMatch(opts.updateGlob.c_str(), key.c_str());
}

} // namespace

string renderSnapshotDiff(const string &stored, const string &actual, bool color)
{
  return string(
      renderDiff(std::string(stored.c_str()), std::string(actual.c_str()), color)
          .c_str());
}

void resetSnapshotCounters()
{
  counters().clear();
}

void snapshotCheck(const string &actual, const char *label, const char *file, int line)
{
  const TestCase *test = currentTest();
  if (!test) {
    std::fprintf(stderr, "SNAPSHOT outside a test at %s:%d\n", file, line);
    return;
  }

  SnapshotFile &snapshots = fileFor(test->file);
  const std::string key = keyFor(label);
  // The file format ends a value at the next key header, so a trailing newline
  // cannot survive the round trip; snapshots compare without one.
  std::string value(actual.c_str());
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }

  snapshots.used.insert(key);

  const auto stored = snapshots.entries.find(key);
  if (stored == snapshots.entries.end()) {
    if (options().ci) {
      reportSnapshotFailure(string(key.c_str()),
                            "no stored snapshot (run with -u to write it)",
                            string(""),
                            file,
                            line);
      return;
    }
    snapshots.entries[key] = value;
    snapshots.dirty = true;
    snapshots.newEntries++;
    return;
  }

  if (stored->second == value) {
    return;
  }

  if (shouldUpdate(key)) {
    stored->second = value;
    snapshots.dirty = true;
    return;
  }

  const std::string diff = renderDiff(stored->second, value, options().color);
  reportSnapshotFailure(
      string(key.c_str()), "snapshot mismatch", string(diff.c_str()), file, line);
}

SnapshotSummary finishSnapshots()
{
  SnapshotSummary summary;
  const Options &opts = options();

  for (auto &[path, file] : files()) {
    summary.newEntries += file.newEntries;

    if (!opts.filtered()) {
      std::vector<std::string> obsolete;
      for (const auto &[key, value] : file.entries) {
        if (file.used.find(key) == file.used.end()) {
          obsolete.push_back(key);
        }
      }
      summary.obsolete += int(obsolete.size());
      if (opts.update) {
        for (const std::string &key : obsolete) {
          file.entries.erase(key);
          file.dirty = true;
        }
      } else if (!obsolete.empty()) {
        for (const std::string &key : obsolete) {
          std::fprintf(stderr, "obsolete snapshot: %s (%s)\n", key.c_str(), path.c_str());
        }
      }
    }

    if (file.dirty) {
      save(file);
      summary.written++;
    }
  }
  return summary;
}

} // namespace fastlint::test
