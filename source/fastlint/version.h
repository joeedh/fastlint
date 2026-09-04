#pragma once

#include "util/string.h"

namespace fastlint {

using litestl::util::string;

/** Semantic version of this build, as `major.minor.patch`. */
const char *version();

/** One line naming the version, the compiler and the build type. */
string buildBanner();

} // namespace fastlint
