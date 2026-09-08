# Embedding builds

Two builds put the linter inside a JavaScript host: an N-API addon for Node and
Electron, and an Emscripten module for browsers and editors without native
addons. Both are cross-compiles of the same `fastlint_lib`, both live outside
the native presets, and both are reached through `node make.ts` rather than
cmake directly.

## The shared entry point

- `fastlint/embed/lint_text.h` is the whole surface: `lintText(source,
  filename, out)` fills `out` with the ESLint-shaped JSON that `--format json`
  prints.
- The recommended preset applies. An embedding has no directory to search for
  `fastlint.config.json`, so nothing is loaded from disk.
- Type-aware rules do not run. Typing a file needs a tsgo process and a
  resolved tsconfig, and neither host offers one.
- `LintOptions::fixEdits` is on, so every fixable problem carries its edit. The
  host applies it; nothing here writes files.

Both entry points are thin wrappers over that call, so a rule change reaches
both without touching either.

## N-API addon

- `node make.ts build --napi [--smoke]` builds `build/napi/fastlint.node`.
  `--runtime electron` targets the Electron ABI instead of Node's, and
  `--runtime-version` overrides the version, which otherwise comes from the
  running Node or the pinned `electron` devDependency.
- cmake-js runs the configure step only. It downloads the runtime's headers and
  its Windows import library and injects `CMAKE_JS_INC`, `CMAKE_JS_LIB` and
  `CMAKE_JS_SRC`; the build is an ordinary `cmake --build`, so the addon
  compiles with the same generator and flags as the rest of the tree.
- cmake-js is not preset-aware, so `build/napi` has no entry in
  CMakePresets.json. What the presets would carry is passed as `--CD` flags in
  `tools/make/lib/napi.ts`.
- On Windows the addon is forced onto the dynamic CRT. cmake-js defaults to the
  static one, and a mismatch with the host surfaces as unresolved `dllimport`
  CRT symbols at link.
- `source/napi/addon.cc` calls the C N-API directly rather than through
  node-addon-api: the plugin ABI is already a C surface, and exceptions are off
  in this tree.
- Exports are `version()`, `lintText(source, filename?)` (the JSON as a string),
  and the node accessors below.

## The TypeScript rule runtime

`lintText` runs the built-in rules; the rule runtime runs rules written in
TypeScript over the same tree. It has two halves.

- `source/fastlint/embed/ast_session.h` parses a buffer once and owns the source,
  the grammar tree and the AST file as a unit, so every node it hands out stays
  valid until the session is dropped. Both embeddings wrap it.
- The addon exposes `parse(source, filename?)` (returns a session handle whose
  finalizer frees it), `root(session)`, and the accessors `kind`, `flags`,
  `parent`, `childCount`, `child`, `text`, `dataByte`, `start`, `end` and
  `descendants(session, node, kind)`. Node handles are N-API externals over a
  `const ast::Node *`; only the session external carries a finalizer.
- The generated `Host` interface (plugin/generated/ts/views.ts) names exactly
  those accessors, and `wrap(host, handle)` turns a handle into the typed view
  for its kind. So a rule reads `node.callee`, `member.isComputed` and
  `node.descendants(kind)` without ever seeing the layout.
- `source/fastlint/plugin/ts/runtime.ts` is the driver: `lint(addon, source,
  filename, rules)` builds a `Host` for the session, walks the tree once and
  dispatches each node to the visitors a rule's `create(context)` returns
  (keyed by node-kind name), collecting each `context.report` into a flat
  message list. `plugin/ts/rules/no-debugger.ts` are two example rules;
  `runtime.smoke.ts` runs them through the built addon under `--smoke`.
- Type-aware rules do not run here either, for the same reason `lintText` skips
  them: no tsgo process. `context` carries the filename and source text, not a
  type facts handle.

## WASM module

- `node make.ts deps fetch emsdk` installs the pinned SDK into `vendor/emsdk`.
  It is not part of `deps`, because the download runs to about 700MB.
- `node make.ts build --wasm [--release] [--smoke]` builds
  `build/wasm/bin/fastlint.js` beside its `.wasm`.
- The module is an ES module with a factory default export, so
  `import createFastlint from "./fastlint.js"` works in a bundler, a browser
  and Node alike.
- Exports are `_fl_wasm_version`, `_fl_wasm_lint` and `_fl_wasm_free`, plus
  `_malloc` and `_free`. `fl_wasm_lint` returns a `malloc`ed buffer the caller
  hands back to `fl_wasm_free`.
- `-sFILESYSTEM=1` stays on because SQLite is linked in with the core and opens
  files through the libc layer.
- Warnings are not errors under Emscripten. emcc tracks a different clang than
  the native builds, so a toolchain bump would otherwise break the WASM build
  over a warning the native gate never saw.
- litestl gates its 32-bit fixups on `WASM` rather than on `__EMSCRIPTEN__`, so
  the top-level CMakeLists defines it for every target under Emscripten. Its
  allocator's block header is a byte multiple short without it, and the
  `static_assert` in `util/alloc.cc` catches that at compile time.

## The emsdk environment

`tools/make/lib/emsdk.ts` captures the SDK's environment the way
`captureVcvars` captures MSVC's, and for the same reason: a delta in
`.cache/emsdk.json` cannot pin the PATH or TEMP that happened to be live when it
was written.

- `emsdk activate` runs without `--permanent`, so nothing is written to the
  user's registry or shell profile.
- `emsdk.py construct_env` emits PATH whole (its own directories followed by
  the one it inherited), so only the leading entries are kept.
- The SDK's bundled Node is dropped from those entries. emcc finds it through
  `EMSDK_NODE`, and leaving it on PATH would shadow the Node the tooling runs
  under.
- `FASTLINT_EMSDK_DIR` points at another checkout's install, so a worktree does
  not repeat the download.
- The toolchain file is passed on the `cmake` command line rather than named in
  the preset, so opening the repository in an IDE that reads CMakePresets does
  not fail on an unset `EMSDK`.
