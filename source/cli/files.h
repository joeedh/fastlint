#pragma once

#include "fastlint/syntax/parser.h"
#include "util/vector.h"

#include <filesystem>
#include <string>

namespace fastlint::cli {

/** Reads a whole file; std::string is the I/O boundary so NUL bytes survive. */
bool readFile(const std::filesystem::path &path, std::string &out);

/** Writes bytes to a file, creating parent directories. */
bool writeFile(const std::filesystem::path &path, const std::string &bytes);

/** Parser options implied by a file's extension (`.tsx` is JSX, `.js` is JS mode). */
syntax::Parser::Options optionsFor(const std::filesystem::path &path);

bool isSourceFile(const std::filesystem::path &path);

/** Expands directories recursively; files are taken as given. */
void collectFiles(const char *arg, litestl::util::Vector<std::filesystem::path> &files);

int fuzzCommand(int argc, char **argv);
int benchCommand(int argc, char **argv);

} // namespace fastlint::cli
