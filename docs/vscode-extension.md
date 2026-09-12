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

## Checking the server without VS Code

The server takes its transport from argv, so `node editors/vscode/out/server.js
--stdio` speaks LSP over stdin and stdout. An `initialize` request answers with
the capabilities, and `initialized` logs which engine the server found, which is
enough to tell a broken bundle from a broken extension.
