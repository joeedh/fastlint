#include "fastlint/version.h"
#include "testing/test.h"

using namespace fastlint;

TEST(version, reports_a_semver_string)
{
  const string text(version());
  CHECK(text.size() > 0);
  CHECK(text.contains(string(".")));
}

TEST(version, banner_names_the_version)
{
  const string banner = buildBanner();
  CHECK(banner.contains(string(version())));
}
