# fastlint

A fast TypeScript/JavaScript linter written in C++. It has its own scanner and
recursive-descent parser, a flat arena AST with full trivia fidelity, type
information from `tsc --api`, and a SQLite cache of type facts and rule results.

The built-in rules run in C++. Rules written in TypeScript run over the same
parse through an N-API addon or a WASM build, so a project mixes both in one
config. See docs/tasklists/MASTER.md for where the work stands.

## Install

```sh
npm i -D fastlint
npx fastlint --init        # writes a starter fastlint.config.json
npx fastlint src
```

The package bundles a WASM build of the linter, so it works with no native
binary installed. Point the config's `binary` item at a native `fastlint`, or
put one on PATH, and the same run goes through that instead:

```jsonc
{
  "$schema": "./node_modules/fastlint/schema/fastlint.config.schema.json",
  "extends": "fastlint:recommended",
  "plugins": { "acme": "./rules/index.ts" },
  "rules": { "curly": "error", "acme/no-foo": "warn" },
  "ignores": ["**/dist/**"]
}
```

`fastlint.config.ts` is read too, with `defineConfig` from the package for the
types. docs/rules.md covers the schema and the CLI; docs/plugins.md is the guide
for writing rules.

## Building from source

Requires Node 24, pnpm, and Visual Studio 18 (Community is enough). cmake,
ninja, clang-format and `vcvarsall.bat` ship with VS and are found with
`vswhere`; none of them need to be on PATH.

```sh
pnpm install
node make.ts deps              # submodules, plus DTL and sqlite into vendor/
node make.ts configure
node make.ts build
node make.ts test
node make.ts run -- --version
```

## Commands

`node make.ts <command>` is the single entry point. Every command takes
`--preset <debug|release|relwithdebinfo|asan|clang-asan>`, defaulting to
`debug`.

| Command | What it does |
| --- | --- |
| `env` | Locates the toolchain through `vswhere` and caches it in `.cache/env.json`. `--refresh` re-probes. |
| `vcvars` | Captures the `vcvarsall.bat` environment into `.cache/vcvars-x64.json`, which every compile-adjacent command imports. |
| `configure` | CMake configure with the Ninja generator into `build/<preset>/`. |
| `build` | `cmake --build`; `--target` builds one target. |
| `test` | Builds, runs every `*_tests.exe` with `--json`, and aggregates. `--filter`, `--tag`, `--all`, `-u`, `--isolate`, `--ctest`, `--ts`. |
| `check` | `format --check` + build + C++ tests + TS tests. What CI runs. |
| `format` | clang-format over `source/`, the `@pathtx/prettier` fork over the TypeScript. `--check` only reports. |
| `deps` | Updates submodules and fetches the externals: `deps fetch dtl` clones a pinned git external, `deps fetch sqlite` downloads the checksummed amalgamation zip into `vendor/`. |
| `run` | Builds `fastlint` and runs it with the remaining arguments. |
| `config` | Prints what a config compiles to; `--native` emits the handoff document, `--out` writes it. |
| `pack` | Builds the publishable npm package into `dist/`; `--wasm` builds the engine it bundles, `--smoke` lints through the result, `--install` checks the tarball the way a user installs it. |
| `release` | Bumps the version, builds and checks the package, tags and pushes, and creates the GitHub release. `--dry-run` stops before git. |
| `publish` | Sends the tarball `release` built to the npm registry. |
| `clean` | Removes `build/<preset>`, or everything under `--all`. |

## Layout

- source/fastlint — the library; source/fastlint/rules holds the built-in rules.
- source/cli — the `fastlint` executable.
- source/testing — the in-house test framework; see docs/tests.md.
- source/tests — tests for the library and the framework itself.
- tests/\_\_snapshots\_\_ — committed snapshot files, reviewed like code.
- vendor/litestl — the container and platform library, as a submodule.

## Documents

- docs/STRATEGY.md — architecture and the reasoning behind each choice.
- docs/tasklists/MASTER.md — the master task list.
- docs/rules.md — the rule framework, config, disable directives and output.
- docs/tests.md — the test framework, snapshot format, and test strategy.
- docs/debugging.md — debugging aids per subsystem.
- docs/tsgo-api.md — what the `tsc --api` protocol actually serves.
- docs/embedding.md — the N-API addon, the WASM module and the npm package.
- docs/plugins.md — writing rules in TypeScript.
