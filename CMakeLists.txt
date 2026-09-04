cmake_minimum_required(VERSION 3.21)
project(fastlint CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SOURCE_FILE_EXTENSIONS .cc)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_OPTIMIZE_DEPENDENCIES ON)

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

enable_testing()
include(cmake/FastlintTest.cmake)

# Warnings are errors for our code only; fastlint_warnings carries the flags so
# vendored targets never see them.
add_library(fastlint_warnings INTERFACE)
if(MSVC)
  # /external:W0 only silences headers cl was told are external, so the vendor
  # include directories below are re-tagged as system ones.
  set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/external:I ")
  target_compile_options(fastlint_warnings INTERFACE /W4 /WX /permissive- /EHsc /external:W0)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # clang-cl's preprocessor is already conforming and rejects the flag as
    # unused, which /WX turns into an error.
    target_compile_options(fastlint_warnings INTERFACE /Zc:preprocessor)
  endif()
  target_compile_definitions(fastlint_warnings INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(fastlint_warnings INTERFACE -Wall -Wextra -Werror)
endif()

# litestl's own CMakeLists pulls in math, io, binding and the vendored eigen and
# lz4 trees. Its subdirectories are added individually instead so only the parts
# we use are configured. eigen comes along because util names it as a
# dependency; its shim is header-only and costs nothing.
set(LITESTL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/litestl")
include("${LITESTL_DIR}/build_files/macros.cmake")
add_subdirectory("${LITESTL_DIR}/extern/eigen" litestl/eigen)
add_subdirectory("${LITESTL_DIR}/platform" litestl/platform)
add_subdirectory("${LITESTL_DIR}/util" litestl/util)
add_subdirectory("${LITESTL_DIR}/path" litestl/path)

# litestl's headers predate /W4 and warn under it. Re-tagging what they export
# as system includes keeps /WX meaningful for our own code.
foreach(vendored util platform litestl_path eigen)
  get_target_property(dirs ${vendored} INTERFACE_INCLUDE_DIRECTORIES)
  if(dirs)
    set_target_properties(${vendored} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${dirs}")
  endif()
endforeach()

add_library(litestl INTERFACE)
target_link_libraries(litestl INTERFACE util litestl_path)
# platform is an OBJECT library, and CMake does not carry an object library's
# object files across a second hop of usage requirements, so they are named
# directly instead of linking the target.
target_sources(litestl INTERFACE $<TARGET_OBJECTS:platform>)
target_include_directories(litestl SYSTEM INTERFACE "${LITESTL_DIR}/platform/..")
target_include_directories(litestl SYSTEM INTERFACE "${LITESTL_DIR}")

# DTL supplies the unified diff the snapshot assertions print. `node make.ts
# deps` clones it; the test framework is skipped rather than failing the build
# when it is absent, so a fresh clone can still build the CLI.
set(DTL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dtl")
if(EXISTS "${DTL_DIR}/dtl/dtl.hpp")
  add_library(dtl INTERFACE)
  target_include_directories(dtl SYSTEM INTERFACE "${DTL_DIR}")
  set(FASTLINT_HAVE_DTL ON)
else()
  set(FASTLINT_HAVE_DTL OFF)
  message(STATUS "vendor/dtl missing; run `node make.ts deps fetch dtl` for snapshot diffs")
endif()

add_subdirectory(source)
