# The N-API addon. cmake-js configures this tree and injects CMAKE_JS_INC (the
# runtime's headers), CMAKE_JS_LIB (its import library, Windows only) and
# CMAKE_JS_SRC (the Windows delay-load hook); `node make.ts build --napi` drives
# it. Configuring without cmake-js never reaches here — source/CMakeLists.txt
# adds this directory only when CMAKE_JS_VERSION is set.

add_library(fastlint_node MODULE addon.cc ${CMAKE_JS_SRC})
set_target_properties(fastlint_node PROPERTIES
  PREFIX ""
  SUFFIX ".node"
  OUTPUT_NAME "fastlint"
  # Pinned so the tooling knows where the addon lands whatever cmake-js's own
  # layout would have been.
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

target_include_directories(fastlint_node SYSTEM PRIVATE ${CMAKE_JS_INC})
target_link_libraries(fastlint_node PRIVATE fastlint_lib fastlint_warnings ${CMAKE_JS_LIB})

# N-API 8 is the floor: it is what Node 16 shipped, so an addon built here loads
# in every runtime the project supports without a rebuild.
target_compile_definitions(fastlint_node PRIVATE
  NAPI_VERSION=8
  NODE_GYP_MODULE_NAME=fastlint)

# Nothing in the addon throws, and the runtime's headers are tagged system, so
# their own warnings stay out of the way of /WX on our sources.
if(MSVC)
  target_compile_options(fastlint_node PRIVATE /wd4100)
endif()

# On ARM64 Windows there is no prebuilt import library, so one is generated from
# the .def cmake-js downloads. This is cmake-js's documented boilerplate.
if(MSVC AND CMAKE_JS_NODELIB_DEF AND CMAKE_JS_NODELIB_TARGET)
  execute_process(COMMAND ${CMAKE_AR} /def:${CMAKE_JS_NODELIB_DEF}
                          /out:${CMAKE_JS_NODELIB_TARGET} ${CMAKE_STATIC_LINKER_FLAGS})
endif()
