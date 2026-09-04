#include "fastlint/version.h"

#include <cstdio>
#include <cstring>

int main(int argc, char **argv)
{
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--version") == 0) {
      std::printf("%s\n", fastlint::version());
      return 0;
    }
  }
  std::printf("%s\n", fastlint::buildBanner().c_str());
  std::printf("no rules yet; see docs/tasklists/MASTER.md\n");
  return 0;
}
