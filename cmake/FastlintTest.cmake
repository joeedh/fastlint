set(FASTLINT_DISCOVER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/DiscoverTests.cmake")

# Creates `<name>_tests` from the given sources, links the framework and the
# named libraries, and registers one ctest entry per suite after the link.
function(add_fastlint_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  if(NOT ARG_SOURCES)
    set(ARG_SOURCES ${ARG_UNPARSED_ARGUMENTS})
  endif()

  set(target "${name}_tests")
  add_executable(${target} ${ARG_SOURCES})
  target_link_libraries(${target} PRIVATE fastlint_testing fastlint_warnings ${ARG_LIBS})

  set(ctest_file "${CMAKE_CURRENT_BINARY_DIR}/${target}_ctest.cmake")
  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -D "TEST_EXECUTABLE=$<TARGET_FILE:${target}>" -D "TEST_NAME=${name}"
      -D "TEST_WORKING_DIR=${CMAKE_SOURCE_DIR}" -D "CTEST_FILE=${ctest_file}" -P
      "${FASTLINT_DISCOVER_SCRIPT}"
    VERBATIM
  )

  # TEST_INCLUDE_FILES emits a hard `include()`, so a stub has to exist before
  # the first build writes the real one.
  if(NOT EXISTS "${ctest_file}")
    file(WRITE "${ctest_file}" "")
  endif()
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${ctest_file}")
endfunction()
