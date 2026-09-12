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
  CommonJS, which is what the extension host loads. `node esbuild.mts --watch`
  rebuilds on change for a debug session.
- `tsconfig.json` extends the root one but switches to `module: esnext` and
  `moduleResolution: bundler`. The root's `nodenext` would read the missing
  `"type": "module"` in this directory's package.json as CommonJS and reject
  the ESM syntax; `bundler` models what esbuild does with the sources.
  `node make.ts check` typechecks it when the directory has been installed and
  says so when it has not.
- `.vscodeignore` is an allow-list: the two bundles, `wasm/`, `schema/`,
  package.json, README.md and LICENSE. Everything else, the sources and
  node_modules included, stays out of the VSIX.

## The bundles and `import.meta.url`

The plugin sources locate the WASM module and the lint worker relative to
`import.meta.url`, which is empty in a CommonJS bundle. esbuild is told to
define it as the bundle's own `file:` URL (a banner computes it from
`__filename`), so `findWasmModule` in engine.ts resolves `out/../wasm/`, which
is where the build stages the engine. A bundle therefore needs no path passed
in to find its engine.

## `node make.ts vsix [--wasm] [--install]`

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
5. `vsce package --no-dependencies` into build/vsix/`<name>-<version>.vsix`.
   The dependencies are already inside the bundles, and vsce cannot walk
   pnpm's node_modules layout anyway.
6. `--install` runs `code --install-extension … --force`, replacing the
   installed copy.

The manifest's `publisher` is a placeholder until there is a Marketplace
publisher to release under; the VSIX installs locally regardless.

## The client

`client/extension.ts` starts the server and owns everything the user touches.

- Settings are `lintrix.*`, declared in package.json with `scope: resource`
  so a folder can override them: `enable`, `run` (`onType` or `onSave`),
  `validate` (language ids), `engine` (`auto`, `native`, `wasm`),
  `binaryPath`, `codeActionsOnSave.mode` (`all` or `problems`) and
  `trace.server`. `shared/protocol.ts` names their shape and defaults once for
  both sides. `engine: native` and `binaryPath` are declared ahead of the serve
  mode (task 9.5); until then `native` reports an error and `auto` lints
  through WASM.
- `run`, `validate` and `enable` act on the client, in the diagnostic pull
  filter: a pull the settings exclude is never sent, so the server has one
  path and no notion of modes. `enable` is also read on the server, which
  answers an empty report for a disabled document that the client pulls
  anyway (a focus pull, say).
- The server reads the settings through `workspace/configuration` per document
  and caches them (`server/settings.ts`); the client's
  `workspace/didChangeConfiguration` drops that cache and re-pulls.
- Commands, all under the `lintrix` category: `lintrix.executeAutofix` (runs
  the server's `lintrix.applyAllFixes` on the active document, which task 9.4
  provides), `lintrix.restart`, `lintrix.revalidate` (the server drops every
  cache and re-pulls) and `lintrix.showOutputChannel`.
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
- The client watches `lintrix.config.*` and `tsconfig.json`. On a change the
  server drops the whole config cache and asks the client to refresh, which
  re-pulls every open document.
- A document that is not a `file:` (an untitled buffer) has no directory to
  search, so it lints under the recommended preset as a name with the
  extension its language id implies.

The mapping has `node --test` coverage in `server/diagnostics.test.ts`, run by
`node make.ts test` when the extension's dependencies are installed.

## Checking the server without VS Code

The server takes its transport from argv, so `node editors/vscode/out/server.js
--stdio` speaks LSP over stdin and stdout. An `initialize` request answers with
the capabilities, `initialized` logs which engine the server found, and a
`textDocument/didOpen` followed by `textDocument/diagnostic` returns the
diagnostics for the buffer, which is enough to tell a broken bundle from a
broken extension.
