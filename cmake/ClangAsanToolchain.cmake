# CMake drives clang-cl links through lld-link directly, so the compiler driver
# never gets to add the sanitizer runtime itself. This runs before the compiler
# test, which links an executable and would otherwise fail on __asan_init.

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)

find_program(FASTLINT_CLANG_CL clang-cl REQUIRED)
get_filename_component(FASTLINT_LLVM_BIN "${FASTLINT_CLANG_CL}" DIRECTORY)

# The clang resource directory carries the major version in its name.
file(GLOB FASTLINT_CLANG_RT_DIRS "${FASTLINT_LLVM_BIN}/../lib/clang/*/lib/windows")
if(NOT FASTLINT_CLANG_RT_DIRS)
  message(FATAL_ERROR "clang sanitizer runtime not found near ${FASTLINT_CLANG_CL}")
endif()
list(GET FASTLINT_CLANG_RT_DIRS 0 FASTLINT_CLANG_RT_DIR)

set(CMAKE_EXE_LINKER_FLAGS_INIT
    "/DEBUG /INCREMENTAL:NO /libpath:\"${FASTLINT_CLANG_RT_DIR}\" clang_rt.asan_dynamic-x86_64.lib clang_rt.asan_dynamic_runtime_thunk-x86_64.lib"
)
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT}")
