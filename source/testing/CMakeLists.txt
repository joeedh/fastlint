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
