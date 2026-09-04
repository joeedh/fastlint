# fastlint

A fast TypeScript/JavaScript linter written in C++. It has its own scanner and
recursive-descent parser, a flat arena AST with full trivia fidelity, type
information from `tsc --api`, and a SQLite cache of type facts and rule results.

Nothing lints yet — see docs/tasklists/MASTER.md for where the work stands.

## Getting started

Requires Node 24, pnpm, and Visual Studio 18 (Community is enough). cmake,
ninja, clang-format and `vcvarsall.bat` ship with VS and are found with
`vswhere`; none of them need to be on PATH.

```sh
pnpm install
node make.ts deps              # submodules, plus DTL into vendor/
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
| `deps` | Updates submodules; `deps fetch dtl` clones a pinned external into `vendor/`. |
| `run` | Builds `fastlint` and runs it with the remaining arguments. |
| `clean` | Removes `build/<preset>`, or everything under `--all`. |

## Layout

- source/fastlint — the library.
- source/cli — the `fastlint` executable.
- source/testing — the in-house test framework; see docs/tests.md.
- source/tests — tests for the library and the framework itself.
- tests/\_\_snapshots\_\_ — committed snapshot files, reviewed like code.
- vendor/litestl — the container and platform library, as a submodule.

## Documents

- docs/STRATEGY.md — architecture and the reasoning behind each choice.
- docs/tasklists/MASTER.md — the master task list.
- docs/tests.md — the test framework, snapshot format, and test strategy.
- docs/debugging.md — debugging aids per subsystem.
- docs/tsgo-api.md — what the `tsc --api` protocol actually serves.
