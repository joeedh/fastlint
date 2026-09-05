#include "cli/files.h"

#include <cstdio>

namespace fastlint::cli {

using litestl::util::Vector;

namespace {

/** Rewrites a UTF-16 file (detected by its BOM) as UTF-8 in place. */
void transcodeUtf16(std::string &bytes)
{
  if (bytes.size() < 2) {
    return;
  }
  unsigned char b0 = static_cast<unsigned char>(bytes[0]);
  unsigned char b1 = static_cast<unsigned char>(bytes[1]);
  bool little = b0 == 0xFF && b1 == 0xFE;
  bool big = b0 == 0xFE && b1 == 0xFF;
  if (!little && !big) {
    return;
  }
  std::string out;
  out.reserve(bytes.size());
  auto unit = [&](size_t i) {
    unsigned char lo = static_cast<unsigned char>(bytes[i + (little ? 0 : 1)]);
    unsigned char hi = static_cast<unsigned char>(bytes[i + (little ? 1 : 0)]);
    return uint32_t(hi) << 8 | lo;
  };
  auto put = [&](uint32_t cp) {
    if (cp < 0x80) {
      out.push_back(char(cp));
    } else if (cp < 0x800) {
      out.push_back(char(0xC0 | (cp >> 6)));
      out.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(char(0xE0 | (cp >> 12)));
      out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(char(0xF0 | (cp >> 18)));
      out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(char(0x80 | (cp & 0x3F)));
    }
  };
  for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
    uint32_t cp = unit(i);
    if (cp >= 0xD800 && cp < 0xDC00 && i + 3 < bytes.size()) {
      uint32_t low = unit(i + 2);
      if (low >= 0xDC00 && low < 0xE000) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        i += 2;
      }
    }
    put(cp);
  }
  bytes.swap(out);
}

} // namespace

bool readFile(const std::filesystem::path &path, std::string &out)
{
  FILE *file = fopen(path.string().c_str(), "rb");
  if (!file) {
    return false;
  }
  char buffer[65536];
  for (;;) {
    size_t read = fread(buffer, 1, sizeof(buffer), file);
    if (read == 0) {
      break;
    }
    out.append(buffer, read);
  }
  fclose(file);
  transcodeUtf16(out);
  return true;
}

bool writeFile(const std::filesystem::path &path, const std::string &bytes)
{
  std::error_code error;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), error);
  }
  FILE *file = fopen(path.string().c_str(), "wb");
  if (!file) {
    return false;
  }
  size_t written = fwrite(bytes.data(), 1, bytes.size(), file);
  fclose(file);
  return written == bytes.size();
}

syntax::Parser::Options optionsFor(const std::filesystem::path &path)
{
  syntax::Parser::Options options;
  std::string ext = path.extension().string();
  options.jsx = ext == ".tsx" || ext == ".jsx";
  options.javaScript = ext == ".js" || ext == ".jsx" || ext == ".mjs" || ext == ".cjs";
  return options;
}

bool isSourceFile(const std::filesystem::path &path)
{
  std::string ext = path.extension().string();
  return ext == ".ts" || ext == ".tsx" || ext == ".mts" || ext == ".cts";
}

void collectFiles(const char *arg, Vector<std::filesystem::path> &files)
{
  std::error_code error;
  std::filesystem::path path(arg);
  if (!std::filesystem::is_directory(path, error)) {
    files.append(path);
    return;
  }
  std::filesystem::recursive_directory_iterator it(
      path, std::filesystem::directory_options::skip_permission_denied, error);
  for (; it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
    if (error) {
      break;
    }
    if (it->is_regular_file(error) && isSourceFile(it->path())) {
      files.append(it->path());
    }
  }
}

} // namespace fastlint::cli
