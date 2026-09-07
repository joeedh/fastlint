set(SRC
  crash.cc
  describe.cc
  fixture.cc
  main.cc
  snapshot.cc
  test.cc

  describe.h
  fixture.h
  runner.h
  snapshot.h
  test.h
)

add_library(fastlint_testing STATIC ${SRC})
target_include_directories(fastlint_testing PUBLIC "${CMAKE_SOURCE_DIR}/source")
target_link_libraries(fastlint_testing PUBLIC litestl dtl PRIVATE fastlint_warnings)
target_compile_definitions(fastlint_testing PUBLIC FASTLINT_TESTS_DIR="${CMAKE_SOURCE_DIR}/tests")

# The rule tester needs the linter, so it is its own library; test executables
# for rules link it alongside fastlint_lib.
add_library(fastlint_rule_tester STATIC rule_tester.cc rule_tester.h)
target_link_libraries(fastlint_rule_tester PUBLIC fastlint_testing fastlint_lib PRIVATE fastlint_warnings)
