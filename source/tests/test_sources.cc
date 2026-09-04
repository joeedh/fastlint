#include "util/vector.h"
#include "util/string.h"
#include <cstdio>
#include <cstring>
#include "test_sources.h"

namespace fastlint::test {
    using litestl::util::string;
    using litestl::util::Vector;

    Vector<TestSource> tsTestSources;

    void loadTestSource(string path) {
        path = string("source/tests/ts_sources/" + path);

        FILE *file = fopen(path.c_str(), "r");
        string text;
        if (file) {
            char buffer[1024];
            while (!feof(file)) {
                int read = fread(buffer, 1, sizeof(buffer) - 1, file);
                // paranoia check
                if (read < 0) {
                    break;
                }
                buffer[read] = 0;
                text += string(buffer);
            }
            fclose(file);
        } else {
            fprintf(stderr, "Failed to open file: %s\n", path.c_str());
        }
        tsTestSources.append({path, text});
    }

    void loadTestSources() {
        loadTestSource("ts_test_struct_intern2.ts");
        loadTestSource("ts_test_base.ts");
        loadTestSource("ts_test_nodes.ts");
    }
}