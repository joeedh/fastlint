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
- `rules` is the tuples a config resolved to, `{id, rule, severity, options}`, so
  a rule set to `off` does not run, a rule reads `context.options`, and each
  message carries its `ruleId` and `severity`. `resolveRule(rule)` builds one for
  a caller running a rule without a config.
- Type-aware rules do not run here either, for the same reason `lintText` skips
  them: no tsgo process. `context` carries the filename, the source text and the
  rule's options, not a type facts handle.

## Config and the file driver

The runtime runs a rule list; a project supplies that list from a config, and
the driver runs it over many files.

- `source/fastlint/plugin/ts/config.ts` loads a config in either form: a `.json`
  one is parsed and validated, and a `.ts`, `.js` or `.mjs` one is imported and
  its default export validated the same way. `loadCompiledConfig` then hands it
  to `compile.ts`, which imports the modules `plugins` names and binds each
  `prefix/rule` to the rule it stands for. docs/rules.md "Config" is the schema.
- `compile.ts` also resolves a file: `resolveFile` applies the base layer and
  every override whose globs match, mirroring lint/config.cc, and `nativeConfig`
  emits the document the native binary is handed. `glob.ts` is the matcher both
  sides share, ported from lint/glob.cc.
- `source/fastlint/plugin/ts/driver.ts` is the host: `lintFiles(files,
  {configPath, addonPath, concurrency})` shards the files across a
  `worker_threads` pool. Each worker loads the addon and the config once, and
  the driver hands it the next file as soon as it returns the last, so a slow
  file never idles the rest. The TypeScript rules run on the workers' threads;
  the native parse runs inside each worker. One file, or `concurrency: 1`, stays
  in the calling thread with no worker. A file an `ignores` glob claims is
  answered without a worker and without being read.
- The decision this settles (task 7.2): the node side hosts the native core.
  `plugin/ts/index.ts` is the package surface a `fastlint` npm package would
  re-export, wrapping the built `.node` addon.

## Writing a rule

- `node make.ts new-rule <name> [--selector <NodeKind>] [--out <file>]` scaffolds
  a rule module: a working `Rule` that reports on every `selector` node, for the
  author to narrow. It imports the `fastlint` package surface.
- A rule is the `Rule` shape from `plugin/ts/runtime.ts`: a `name`, optional
  `messages` keyed by `messageId`, and `create(context)` returning visitors
  keyed by node-kind name. `context.report({node, messageId, data})` records a
  problem; `{{placeholder}}` in a message is filled from `data`.
- `plugin/ts/rules/` holds ported examples: `no-debugger` and `no-console`
  (a plain visitor and an `is`-narrowed member read), `no-var` (a `kind` enum),
  `eqeqeq` (an `op` enum with `{{data}}`), and `no-empty` (a list child and a
  source-slice check). They read the generated view surface and nothing else, so
  they run under either embedding.
- Reports only: the runtime has no fixer or type information yet. A rule that
  needs a fix, or a type, stays a native rule for now.

## Performance

`node source/fastlint/plugin/ts/bench.ts <addon-or-module path> [repeats]`
compares the shared C++ front end against the TypeScript runtime over an 87 KB
synthetic source (21.6k nodes), best of the repeats.

- The bare walk (parse subtracted from a one-rule run) is about 3 microseconds
  per node on both embeddings. That is the cost of wrapping a node and reading
  its kind and children back across the boundary, and it dominates: a native
  rule pays a dispatch-map lookup per node instead, in nanoseconds.
- Five inspecting rules take roughly 0.4 s where the front end takes 10 ms under
  N-API, because each visitor's own accessor reads cross the boundary too. Rule
  count barely matters; how much each rule inspects does.
- The N-API front end parses far faster than the debug WASM one (about 10 ms
  against 60 ms), but the per-node walk is close, since both pay a boundary
  crossing per accessor. The standing cost to cut is the per-node handle: an
  integer index or a batched node record would remove most of the crossings.

## WASM module

- `node make.ts deps fetch emsdk` installs the pinned SDK into `vendor/emsdk`.
  It is not part of `deps`, because the download runs to about 700MB.
- `node make.ts build --wasm [--release] [--smoke]` builds
  `build/wasm/bin/fastlint.js` beside its `.wasm`.
- The module is an ES module with a factory default export, so
  `import createFastlint from "./fastlint.js"` works in a bundler, a browser
  and Node alike.
- Exports are `_fl_wasm_version`, `_fl_wasm_lint` and `_fl_wasm_free`, the node
  accessors (`_fl_wasm_parse`, `_fl_wasm_root`, `_fl_wasm_kind`, `_fl_wasm_child`,
  `_fl_wasm_descendants`, ...), plus `_malloc` and `_free`. `fl_wasm_lint`
  returns a `malloc`ed buffer the caller hands back to `fl_wasm_free`.
- The rule runtime runs over WASM too: `plugin/ts/wasm_addon.ts` wraps the
  module as the same `Addon` the N-API path uses, so `lint` is unchanged. Node
  handles are heap pointers passed as numbers; the shim re-reads `HEAPU8`/
  `HEAPU32` on every use because `ALLOW_MEMORY_GROWTH` can swap the buffer, and
  frees each session through `Addon.freeSession`, since the heap has no
  finalizer. The worker-pool driver is N-API-only; a browser or playground runs
  one file at a time on its single thread.
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
