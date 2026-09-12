# VS Code extension

The extension under editors/vscode/ is shaped like vscode-eslint: a thin client
that starts a language server, and a server that lints open documents. Task 9
of docs/tasklists/MASTER.md tracks it. This page covers the layout and the
build; the server's protocol surface is documented as it lands.

## Layout

- editors/vscode/ is a package of its own, with its own package.json,
  pnpm-lock.yaml and node_modules. The repository has no pnpm workspace, so it
  is installed in place by `node make.ts vsix`. Do not run `npm install` there.
- `client/extension.ts` is the extension entry point. It starts the server over
  Node IPC through `vscode-languageclient` and stops it on deactivate.
- `server/server.ts` is the language server, on `vscode-languageserver`. It
  imports source/fastlint/plugin/ts by relative path, so the config loader and
  the engines have one home (docs/embedding.md) and a change there reaches the
  extension without a publish.
- `esbuild.mts` bundles each into `out/client.js` and `out/server.js`, as
  CommonJS, which is what the extension host loads, and the smoke test's
  suite into `out/test/suite.js`. `node esbuild.mts --watch`
  rebuilds on change for a debug session.
- `tsconfig.json` extends the root one but switches to `module: esnext` and
  `moduleResolution: bundler`. The root's `nodenext` would read the missing
  `"type": "module"` in this directory's package.json as CommonJS and reject
  the ESM syntax; `bundler` models what esbuild does with the sources.
  `node make.ts check` typechecks it when the directory has been installed and
  says so when it has not.
- `.vscodeignore` is an allow-list: the two bundles, `wasm/`, `schema/`,
  package.json, README.md and LICENSE. Everything else, the sources, the test
  bundle and node_modules included, stays out of the VSIX.

## The bundles and `import.meta.url`

The plugin sources locate the WASM module and the lint worker relative to
`import.meta.url`, which is empty in a CommonJS bundle. esbuild is told to
define it as the bundle's own `file:` URL (a banner computes it from
`__filename`), so `findWasmModule` in engine.ts resolves `out/../wasm/`, which
is where the build stages the engine. A bundle therefore needs no path passed
in to find its engine.

## `node make.ts vsix [--wasm] [--smoke] [--install]`

The one command that builds the extension. In order:

1. Picks the WASM engine the way `pack` does: the `wasm-release` build, built
   first under `--wasm`, or the debug build with a warning when there is no
   release one (tools/make/pack.ts `wasmPresetToBundle`).
2. `pnpm install` in editors/vscode. pnpm is fast when nothing changed, so it
   runs every time rather than guessing from node_modules.
3. Stages what the VSIX ships from outside its directory: the engine into
   `wasm/`, schema/lintrix.config.schema.json into `schema/` (the manifest's
   `jsonValidation` points at it), and the LICENSE. All three are gitignored.
4. Runs `esbuild.mts`.
5. `--smoke` runs the smoke test in a downloaded VS Code ("Tests" below)
   before anything is packaged, so a broken build never becomes a VSIX.
6. `vsce package --no-dependencies` into build/vsix/`<name>-<version>.vsix`.
   The dependencies are already inside the bundles, and vsce cannot walk
   pnpm's node_modules layout anyway.
7. `--install` runs `code --install-extension … --force`, replacing the
   installed copy.

The manifest's `publisher` is a placeholder until there is a Marketplace
publisher to release under; the VSIX installs locally regardless.

## The client

`client/extension.ts` starts the server and owns everything the user touches.

- Settings are `lintrix.*`, declared in package.json with `scope: resource`
  so a folder can override them: `enable`, `run` (`onType` or `onSave`),
  `validate` (language ids), `engine` (`auto`, `native`, `wasm`),
  `binaryPath`, `tsgoPath`, `codeActionsOnSave.mode` (`all` or `problems`)
  and `trace.server`. `shared/protocol.ts` names their shape and defaults once for
  both sides. `engine` and `binaryPath` choose between the native serve mode
  and WASM ("The native serve mode" below).
- `run`, `validate` and `enable` act on the client, in the diagnostic pull
  filter: a pull the settings exclude is never sent, so the server has one
  path and no notion of modes. `enable` is also read on the server, which
  answers an empty report for a disabled document that the client pulls
  anyway (a focus pull, say).
- The server reads the settings through `workspace/configuration` per document
  and caches them (`server/settings.ts`); the client's
  `workspace/didChangeConfiguration` drops that cache and re-pulls.
- Commands, all under the `lintrix` category: `lintrix.executeAutofix` (runs
  the server's `lintrix.applyAllFixes` on the active document),
  `lintrix.restart`, `lintrix.revalidate` (the server drops every cache and
  re-pulls) and `lintrix.showOutputChannel`. `lintrix.openRuleDoc` is
  registered too, for the "Show documentation" code action, and is not in the
  palette.
- The status bar item (`client/status.ts`) follows the active editor. The
  server sends `lintrix/status` after each lint, naming the document, the
  engine and the state, and once for itself when the engine loads. The item
  shows a document's own state when the server has reported on it, and the
  server-wide state for every editor while that is failing; it is hidden
  otherwise. Clicking it opens the output channel. A run problem (an engine or
  config that did not load) is therefore both the status and one diagnostic at
  the top of the file, so it shows in the Problems view as well.

## The server

`server/server.ts` wires the protocol; the work is in the modules beside it.

- Diagnostics are pulled, not pushed: the server advertises
  `diagnosticProvider` and answers `textDocument/diagnostic` with a full report
  each time. The client decides when to ask (on type, on save, on focus), which
  is where the `run` setting acts, so the server has one path and no timers.
- `engine.ts` loads the bundled WASM module once and lints a buffer: the
  built-in rules through `lintText` with the config's native handoff document,
  and the plugin rules through the TypeScript runtime over the same addon; the
  two lists merge through plugin/ts/report.ts as the CLI merges them. A file
  the config ignores gets an empty report without a parse. Without a config the
  recommended preset applies and no plugin rule runs.
- `configs.ts` resolves a document's config: the nearest `lintrix.config.*`
  walking up from its directory, memoized per directory, loaded and compiled
  once per config path. Module configs load with `fresh: true`
  (plugin/ts/config.ts), because the server outlives edits to them and Node's
  import cache would otherwise serve the first version forever. A config that
  fails to load is remembered as its error, and every document under it
  reports that error as its status and as one diagnostic at the top of the
  file.
- `diagnostics.ts` maps a message to an LSP `Diagnostic`. The JSON already
  counts lines from 1 and columns in UTF-16 units, so the range is an offset of
  one, clamped to the document. `ruleId` becomes `code`, the rule's `url`
  becomes `codeDescription.href` (the link on the code in the Problems view),
  and an unused disable directive carries `DiagnosticTag.Unnecessary`. Each
  diagnostic's `data` holds the index of its message in the report, and the
  report is kept per open document, so a code action request finds the
  message's fix and suggestions without linting again.
- The client watches `lintrix.config.*`, `tsconfig.json` and the source
  files. On a config or tsconfig change the server drops the whole config
  cache and asks the client to refresh, which re-pulls every open document.
  Every change is also forwarded to the native binaries, whose type server
  does not watch the disk; a source change causes no re-pull, so the next pull
  of a dependent file sees it.
- A document that is not a `file:` (an untitled buffer) has no directory to
  search, so it lints under the recommended preset as a name with the
  extension its language id implies.

## The native serve mode

The WASM engine runs no type-aware rules: they need tsgo and a tsconfig. A
native `lintrix` runs them through `lintrix serve`, a resident process the
server drives over stdio (task 9.5).

- `lintrix serve` (source/cli/serve.cc) speaks JSON-RPC 2.0 in LSP's
  `Content-Length` framing over stdin and stdout. `lint {file, text?,
  config?}` answers `{results, typed, typeError?}`, where `results` is the
  `--format json` array for the file (empty when the config ignores it),
  `typed` says the type-aware rules ran, and `typeError` says why they did
  not. `text` overlays the file on disk, so an unsaved buffer is what the
  rules and the type server see. `close {file}` drops the overlay, `changed
  {files}` reports disk changes, `configChanged {path?}` reloads the config or
  tsconfig at `path` (everything without one), and `shutdown` answers and
  exits; end of input exits too. `--no-cache` and `--cache-dir` mean what they
  do for `lint`.
- A session is one loaded config (the `config` given, or the nearest
  `lintrix.config.*` above the file, or the recommended preset) with one tsgo
  process and one result-cache store. Each file's tsconfig is resolved as
  `lint` resolves it and opened in the type server on first sight
  (`ProjectTypes::addProject`), so one process serves every project under a
  config. `ProjectTypes` already hands the type server the text it was given
  rather than the disk, which is what makes the overlay reach tsgo; `close`
  and `changed` drop that held text (`forgetFile`, `filesChanged`) so the
  server reads the disk again.
- The result cache stays on. A `text` that matches the disk is cached and
  replayed like a `lint` run; a text that differs is neither, so an edit
  never poisons the saved file's record. The tsconfigs are not known when the
  store opens, so a file's tsconfig hash goes into its result key instead of
  the environment hash; a tsconfig edit misses rather than replays.
- `plugin/ts/serve.ts` is the client: `ServeClient` spawns the binary,
  frames the requests, matches the responses by id and rejects everything in
  flight when the process exits. The npm CLI does not use it; it is the
  extension's.
- `server/native.ts` keeps one `ServeClient` per binary path, started on
  first use and again after an exit, and writes each config's handoff
  document (`.lintrix.native.json`, as the CLI does) the first time the
  config is seen, telling every running binary to reload it. A config that
  the server loads again is a new object, so an edit rewrites the handoff.
- The server picks the engine per document in `resolveRun`: `engine: wasm`
  stays on WASM; otherwise `binaryPath`, then the config's `binary`, then
  `lintrix` on PATH names the binary. `engine: native` with no binary is an
  error; `auto` with none falls back to WASM and the status bar tooltip says
  the type-aware rules are off. `tsgoPath` names the `tsc` the binary types
  with, passed as `FASTLINT_TSGO`, for a TypeScript build the binary would
  not find or would refuse by version. The plugin rules run in the server through
  the WASM runtime either way, so the module is still required.
- The status after a native lint names the engine and, when the type-aware
  rules did not run, why: a file with no tsconfig is ordinary and stays `ok`;
  a type server that failed to start is a warning, with the reason in the
  tooltip and the output channel.
- Startup costs one tsgo launch per config, a second or so; a lint after that
  is a few milliseconds, and a saved file replays from the cache in one.

## Code actions and fixes

`server/actions.ts` builds the actions; `server/diff.ts` turns a fixed text
back into edits.

- Every action carries its edit inline, versioned to the document it was
  computed for, so VS Code applies it with no round trip and refuses it once
  the document has moved on. A message's `fix` is a UTF-16 range over the
  linted source, which is the document's own coordinate space, so
  `positionAt` is the whole mapping.
- For each diagnostic VS Code asks about, the quick fixes are the message's
  own fix (marked preferred), one action per suggestion, "Fix all `<rule>`
  problems" when the rule has more than one non-overlapping fix in the file,
  "Disable `<rule>` for this line", "Disable `<rule>` for the entire file",
  and "Show documentation" when the rule has a page. A message without a rule
  (a syntax error, an unused directive) gets none. "Fix all auto-fixable
  problems" closes the list when anything in the file is fixable.
- The line directive is `// lintrix-disable-next-line <rule>` above the line
  with the line's indentation. If a `disable-next-line` directive is already
  there, in either spelling and either comment form, the rule is appended to
  it after a comma, before a ` -- justification` tail or the block comment's
  close. The file directive is `/* lintrix-disable <rule> */` at the top,
  below a shebang. `eslintDirectives` is on by default, so the `lintrix-`
  spelling is always the one written.
- "Show documentation" runs `lintrix.openRuleDoc` with the rule's `url`; the
  client registers that command, since only it can open a browser.
- Fix-all has three entry points: the `source.fixAll.lintrix` code action
  (which `editor.codeActionsOnSave` names, and a bare `source.fixAll` reaches
  too), the `lintrix.applyAllFixes` server command behind the palette's
  `lintrix.executeAutofix` and the closing quick fix, and both go through
  `computeAllFixes`. In `all` mode the text is linted and its non-overlapping
  fixes applied until a pass finds nothing fixable, at most ten passes, then
  the result is diffed against the document. In `problems` mode
  (`codeActionsOnSave.mode`) the fixes already shown are applied in one pass
  and nothing is linted again. The embedding keeps returning single-shot
  edits; no fixpoint API was added to it.
- `diff.ts` is Myers' algorithm over lines, each changed run of lines trimmed
  to the characters that differ, so the cursor and the undo stack see small
  edits rather than a whole-file replace. Past a thousand differing lines it
  gives up and replaces the changed region whole.

## The exit crash under V8's WASM tiering

V8 compiles WASM with Liftoff first and tiers hot functions up to TurboFan on
background threads. On Windows (Node 24.14), a `process.exit()` while one of
those jobs is posting back to the main thread trips libuv's
`!(handle->flags & UV_HANDLE_CLOSING)` assertion in async.c and the process
dies with 0xC0000409. A handful of lints is enough to start the jobs, and
`vscode-languageserver` ends every run with `process.exit`, so the server hit
it on every shutdown after a fix-all. A natural exit tears the platform down
in order and is fine, which is why the npm CLI (`process.exitCode`) never
sees it. `Engine.load` sets `--no-wasm-dynamic-tiering` through
`v8.setFlagsFromString` before instantiating the module; functions then
compile optimized on first call, which costs nothing measurable (first lint
~20 ms instead of ~15, later ones 0.5–1 ms either way), and V8 ignores the
flag if a later version drops it.

## Tests

`node --test` covers the mapping in `server/diagnostics.test.ts`, the actions
and directive edits in `server/actions.test.ts`, and the diff in
`server/diff.test.ts`; `node make.ts test` runs them when the extension's
dependencies are installed. None of them start VS Code. The serve mode has
two tests of its own: `cli_serve` in source/tests/cli_serve_test.cc, an
`[integration]` test that spawns the built `lintrix serve` and drives it
through a saved file, an overlay, a config change and a shutdown, and
plugin/ts/serve.test.ts, which runs `ServeClient` against a built binary and
skips when none is built.

`node make.ts vsix --smoke` does. `test/run.mts` has `@vscode/test-electron`
download a stable VS Code into editors/vscode/.vscode-test/ on first use (a
one-time download of a few hundred megabytes), writes a fixture workspace
under build/vsix-smoke/ with a config, a tsconfig and one file, and launches
that VS Code over it with the extension loaded from this directory
(`--extensionDevelopmentPath`), other extensions off and workspace trust
off. When a preset has built `lintrix`, the fixture's workspace settings
point `lintrix.binaryPath` at it, so the run goes through the native serve
mode and the expected diagnostics include a type-aware rule; the fixture
sits under the repository so that binary's type server finds `tsc` in the
repository's node_modules. Inside VS Code, `test/suite.ts` (bundled to
`out/test/suite.js` beside the client) drives the real API: activate the
extension, open the file, wait for `lintrix` diagnostics with the expected
codes, ask for code actions at one of them and check the quick fix is
offered, run `lintrix.executeAutofix`, then wait for the text to change and
the diagnostics to clear. `run()` rejects on the first failed assertion,
which test-electron turns into a non-zero exit, and the fixture is removed
afterwards. It is the only check that exercises the extension host rather
than the protocol, so it is what to run after a change to the client or the
manifest.

## Checking the server without VS Code

The server takes its transport from argv, so `node editors/vscode/out/server.js
--stdio` speaks LSP over stdin and stdout. An `initialize` request answers with
the capabilities, `initialized` logs which engine the server found, and a
`textDocument/didOpen` followed by `textDocument/diagnostic` returns the
diagnostics for the buffer, which is enough to tell a broken bundle from a
broken extension.
