set(SRC
  syntax/parser.cc
  syntax/scanner.cc
  syntax/tokens.cc
  syntax/tree.cc
  syntax/unicode.cc
  version.cc

  syntax/diagnostics.h
  syntax/node_kind.def
  syntax/parser.h
  syntax/scanner.h
  syntax/tokens.def
  syntax/tokens.h
  syntax/tree.h
  syntax/unicode.h
  version.h
)

add_library(fastlint_lib STATIC ${SRC})
target_include_directories(fastlint_lib PUBLIC "${CMAKE_SOURCE_DIR}/source")
target_link_libraries(fastlint_lib PUBLIC litestl PRIVATE fastlint_warnings)
