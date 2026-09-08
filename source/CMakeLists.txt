add_subdirectory(fastlint)

# The embedding builds want the core library and their own entry point, nothing
# else: the CLI is a native executable and the test tree needs a native runner.
if(EMSCRIPTEN)
  add_subdirectory(wasm)
  return()
endif()

if(DEFINED CMAKE_JS_VERSION)
  add_subdirectory(napi)
  return()
endif()

add_subdirectory(cli)

if(FASTLINT_HAVE_DTL)
  add_subdirectory(testing)
  add_subdirectory(tests)
endif()
