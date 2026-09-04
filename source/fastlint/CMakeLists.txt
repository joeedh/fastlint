set(SRC
  version.cc

  version.h
)

add_library(fastlint_lib STATIC ${SRC})
target_include_directories(fastlint_lib PUBLIC "${CMAKE_SOURCE_DIR}/source")
target_link_libraries(fastlint_lib PUBLIC litestl PRIVATE fastlint_warnings)
