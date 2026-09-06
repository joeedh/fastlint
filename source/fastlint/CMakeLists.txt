set(SRC
  ast/binder.cc
  ast/comments.cc
  ast/dispatch.cc
  ast/dump.cc
  ast/file.cc
  ast/fixer.cc
  ast/generated/tables.cc
  ast/lower.cc
  ast/precedence.cc
  ast/printer.cc
  ast/template.cc
  syntax/parser/declarations.cc
  syntax/parser/dump.cc
  syntax/parser/expressions.cc
  syntax/parser/jsx.cc
  syntax/parser/lists.cc
  syntax/parser/parser.cc
  syntax/parser/statements.cc
  syntax/parser/types.cc
  syntax/scanner.cc
  syntax/tokens.cc
  syntax/tree.cc
  syntax/unicode.cc
  version.cc

  ast/access.h
  ast/binder.h
  ast/comments.h
  ast/dispatch.h
  ast/dump.h
  ast/file.h
  ast/fixer.h
  ast/generated/kinds.h
  ast/generated/views.h
  ast/kind_info.h
  ast/lower.h
  ast/precedence.h
  ast/printer.h
  ast/template.h
  ast/node.h
  ast/nodes.def
  syntax/diagnostics.h
  syntax/node_kind.def
  syntax/parser.h
  syntax/parser/internal.h
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
