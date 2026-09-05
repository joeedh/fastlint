add_executable(fastlint main.cc bench.cc files.cc fuzz.cc)
target_link_libraries(fastlint PRIVATE fastlint_lib fastlint_warnings)
