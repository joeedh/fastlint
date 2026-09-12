add_executable(fastlint main.cc bench.cc cache.cc cache_bench.cc files.cc fuzz.cc lint.cc run.cc serve.cc)
# The target keeps the tree's name; the executable a user types is `lintrix`.
set_target_properties(fastlint PROPERTIES OUTPUT_NAME "lintrix")
target_link_libraries(fastlint PRIVATE fastlint_lib fastlint_warnings)
if(WIN32)
  # Peak working set for cache-bench.
  target_link_libraries(fastlint PRIVATE psapi)
endif()
