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

## The server

`server/server.ts` wires the protocol; the work is in three modules beside it.

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
  fails to load is remembered as its error, and every document under it shows
  that error as one diagnostic at the top of the file.
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
