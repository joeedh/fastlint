cmake_minimum_required(VERSION 3.21)
project(fastlint C CXX)

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
elseif(EMSCRIPTEN)
  # emcc tracks a different clang than the native builds, so its warning set
  # differs from the one the sources are kept clean against. The warnings are
  # still printed; only promoting them to errors is dropped, so a toolchain
  # bump cannot break the WASM build over a warning the gate never saw.
  target_compile_options(fastlint_warnings INTERFACE -Wall -Wextra)
else()
  target_compile_options(fastlint_warnings INTERFACE -Wall -Wextra -Werror)
endif()

# The addon and the WASM module are both loadable modules, so everything they
# link has to be position independent.
if(DEFINED CMAKE_JS_VERSION OR EMSCRIPTEN)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# litestl gates its 32-bit fixups on WASM rather than on __EMSCRIPTEN__, so the
# allocator's block header is only padded back to an 8-byte multiple when this
# is defined. It goes on every target, litestl's own included.
if(EMSCRIPTEN)
  add_compile_definitions(WASM)
endif()

# backtrace() only names a function whose symbol is in the dynamic table, so
# executables are linked with -rdynamic for readable crash traces.
if(NOT MSVC AND NOT EMSCRIPTEN)
  set(CMAKE_ENABLE_EXPORTS ON)
endif()

# Release keeps debug info so a crash in a shipped build is still readable, and
# folds identical code the debugger would otherwise duplicate.
if(MSVC)
  set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Zi /DNDEBUG")
  string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
endif()

# FASTLINT_SANITIZER is a semicolon list matching -fsanitize, set by the asan
# presets. The flags go on every target, vendored ones included, because a
# partially instrumented binary reports false positives.
set(FASTLINT_SANITIZER "" CACHE STRING "sanitizers to build with, e.g. address;undefined")
if(FASTLINT_SANITIZER)
  string(REPLACE ";" "," FASTLINT_SANITIZER_ARG "${FASTLINT_SANITIZER}")
  if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(/fsanitize=${FASTLINT_SANITIZER_ARG} /Zi)
    string(APPEND CMAKE_EXE_LINKER_FLAGS " /DEBUG /INCREMENTAL:NO")
  elseif(MSVC)
    # clang-cl takes -fsanitize but only MSVC spellings for the rest.
    add_compile_options(-fsanitize=${FASTLINT_SANITIZER_ARG} /Oy- /Zi)
  else()
    add_compile_options(-fsanitize=${FASTLINT_SANITIZER_ARG} -fno-omit-frame-pointer -g)
    if(NOT MSVC)
      add_link_options(-fsanitize=${FASTLINT_SANITIZER_ARG})
    endif()
  endif()
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

# SQLite backs the type cache. `node make.ts deps` downloads the pinned
# amalgamation; without it there is no cache, so configuring stops here.
set(SQLITE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite")
if(NOT EXISTS "${SQLITE_DIR}/sqlite3.c")
  message(FATAL_ERROR "vendor/sqlite missing; run `node make.ts deps fetch sqlite`")
endif()
add_library(sqlite3 STATIC "${SQLITE_DIR}/sqlite3.c")
target_include_directories(sqlite3 SYSTEM PUBLIC "${SQLITE_DIR}")
# One connection per thread, no extension loading, no double-quoted strings, and the
# safe WAL sync level; the cache never needs more of the library than this.
target_compile_definitions(sqlite3 PRIVATE
  SQLITE_THREADSAFE=2
  SQLITE_OMIT_LOAD_EXTENSION
  SQLITE_OMIT_DEPRECATED
  SQLITE_DQS=0
  SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
  SQLITE_LIKE_DOESNT_MATCH_BLOBS
  SQLITE_USE_ALLOCA)
if(NOT MSVC)
  target_compile_options(sqlite3 PRIVATE -w)
endif()

add_subdirectory(source)
