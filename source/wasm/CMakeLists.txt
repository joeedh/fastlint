# The WASM module. Reached only under the Emscripten toolchain, which
# `node make.ts build --wasm` selects; source/CMakeLists.txt adds this directory
# in place of the CLI and the test tree.

add_executable(fastlint_wasm module.cc)
target_link_libraries(fastlint_wasm PRIVATE fastlint_lib fastlint_warnings)
set_target_properties(fastlint_wasm PROPERTIES
  OUTPUT_NAME "fastlint"
  SUFFIX ".js"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

target_link_options(fastlint_wasm PRIVATE
  # An ES module with a factory export, so `import createFastlint from
  # "./fastlint.js"` works in a bundler, a browser and Node alike.
  "-sMODULARIZE=1"
  "-sEXPORT_ES6=1"
  "-sEXPORT_NAME=createFastlint"
  "-sENVIRONMENT=web,worker,node"
  # A large file's arena outgrows any fixed heap, and the parser has no bound to
  # size one from.
  "-sALLOW_MEMORY_GROWTH=1"
  # SQLite is linked in with the core and opens files through the libc layer,
  # so the in-memory filesystem stays on.
  "-sFILESYSTEM=1"
  "-sEXPORTED_FUNCTIONS=_fl_wasm_version,_fl_wasm_lint,_fl_wasm_lint_config,_fl_wasm_free,_fl_wasm_parse,_fl_wasm_session_free,_fl_wasm_root,_fl_wasm_kind,_fl_wasm_flags,_fl_wasm_parent,_fl_wasm_child_count,_fl_wasm_child,_fl_wasm_text_ptr,_fl_wasm_text_len,_fl_wasm_data_byte,_fl_wasm_start,_fl_wasm_end,_fl_wasm_descendants,_malloc,_free"
  "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,stringToNewUTF8,HEAPU8,HEAPU32,HEAP32")

# Debug builds keep the names and the DWARF a browser's debugger reads; release
# drops both and lets the optimizer close over the whole module.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(fastlint_wasm PRIVATE -g)
  target_link_options(fastlint_wasm PRIVATE -g -gsource-map "-sASSERTIONS=1")
else()
  target_link_options(fastlint_wasm PRIVATE -O3 --closure=0)
endif()
