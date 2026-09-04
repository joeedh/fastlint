#include "util/vector.h"
#include "util/string.h"

namespace fastlint::test {
    using litestl::util::string;
    using litestl::util::Vector;

    struct TestSource {
        string path;
        string source;
    };

    extern Vector<TestSource> tsTestSources;

    void loadTestSource(string path);
    void loadTestSources();
}