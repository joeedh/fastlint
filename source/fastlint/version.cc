#include "fastlint/version.h"

namespace fastlint {

const char *version()
{
  return "0.1.2";
}

string buildBanner()
{
  string banner("lintrix ");
  banner += version();
#if defined(_MSC_VER) && !defined(__clang__)
  banner += " (msvc)";
#elif defined(__clang__)
  banner += " (clang)";
#elif defined(__GNUC__)
  banner += " (gcc)";
#endif
#ifndef NDEBUG
  banner += " debug";
#endif
  return banner;
}

} // namespace fastlint
