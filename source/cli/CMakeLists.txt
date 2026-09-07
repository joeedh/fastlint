add_executable(fastlint main.cc bench.cc cache_bench.cc files.cc fuzz.cc)
target_link_libraries(fastlint PRIVATE fastlint_lib fastlint_warnings)
if(WIN32)
  # Peak working set for cache-bench.
  target_link_libraries(fastlint PRIVATE psapi)
endif()
