# Testing

How fastlint is tested: the in-house C++ framework, the snapshot system, and
the per-component test strategy. Debugging aids live in `debugging.md`.

## Principles

- One framework, ours: `source/testing/` (`fastlint::test`). No gtest/Catch/
  doctest. Depends only on litestl + DTL (Joe's Myers-diff library, already
  used by litestl's tests).
- The framework is built on litestl containers (`util::Vector`, `util::Map`,
  `util::string`, …) like the rest of the codebase — no `std::` containers
  in its own implementation, and its `describe()` overloads cover the litestl
  types first. Tests are our code too; the same rule applies inside test
  bodies.
- Tests run in-process by default: fast, and a failure drops you straight
  into the debugger. Isolation is opt-in, not the default.
- Snapshots are the primary assertion for anything tree-shaped (tokens,
  grammar trees, ASTs, diagnostics, fixer output). Hand-written expectations
  for scalars and invariants.
- Every snapshot mismatch prints a real diff. Never "expected X, got Y" dumps
  of two multi-KB blobs.
- Leaks are test failures. litestl's allocation tracker is checkpointed
  around every test.
- Fast by default: `node make.ts test` runs `[fast]` tests only.
  `[integration]` (needs `tsc` on PATH), `[slow]`, `[bench]` are opt-in.

## Framework — `fastlint::test`

Header `testing/test.h` + `testing/test.cc` + `testing/main.cc` (runner
entry, linked into each test executable). Target ~1.5k lines.

### Registration

```cpp
#include "testing/test.h"

TEST(scanner, template_literal_modes) {
  Scanner s("`a${b}c`");
  CHECK(s.next().kind == Tok::TemplateHead);
  ...
}
```

- `TEST(suite, name)` — static registration; full name `suite.name`.
- `TEST_TAGGED(suite, name, "integration", "slow")` — tags for filtering.
- `SUBCASE("label") { ... }` — named sub-scope inside a test; failures
  report the subcase path. Used for data-driven loops:
  `for (auto &f : fixtures) SUBCASE(f.name) { ... }`.
- `SKIP("reason")` — marks skipped, reported separately.
- Fixtures: plain RAII structs constructed at the top of the test. No
  `SetUp`/`TearDown` inheritance.

### Assertions

- `CHECK(expr)` — non-fatal; test continues, failure recorded.
- `REQUIRE(expr)` — fatal; returns from the test body.
- Both decompose binary expressions and print both operands:
  `CHECK(tok.kind == Tok::Ident)` fails as
  `tok.kind == Tok::Ident  →  Tok::Number == Tok::Ident`.
  Implemented with the usual `Decomposer <= lhs op rhs` capture; operands
  rendered through `describe(const T&)` (customization point; overloads for
  litestl containers, spans, enums via the generated kind tables).
- `CHECK_EQ/NE/LT/…` exist for cases the decomposer can't handle (comma-
  containing template args).
- `CHECK_THROWS`-style helpers are absent on purpose: we don't use exceptions.
- `FAIL("msg")`, `INFO("context {}", x)` — `INFO` is scoped, attached to any
  failure inside the scope.
- Every failure prints `file:line`, the expression, decomposed values, active
  `INFO`s and the subcase path.

### Snapshots

```cpp
TEST(parser, arrow_vs_paren) {
  auto tree = parse("(a, b) => a + b");
  SNAPSHOT(dumpSexpr(tree));                 // key: parser.arrow_vs_paren #1
  SNAPSHOT_NAMED("with-types", dumpSexpr(parse("(a: T) => a")));
}
```

- Storage: `tests/__snapshots__/<test_file>.snap`, one per test source file
  (jest layout). Plain text, git-diffable:

  ```
  ### parser.arrow_vs_paren #1
  (ArrowFunction (Parameters (Parameter a) (Parameter b)) (Binary + a b))

  ### parser.arrow_vs_paren with-types
  ...
  ```
  A content line beginning with `### ` is escaped with a leading `\`.
  Entries are sorted by key on write, so updates produce minimal diffs.
  A value ends at the next key header, so trailing newlines cannot survive
  the round trip and are stripped before the comparison.
- Anything with a `describe()` overload can be snapshotted; multi-line
  strings are stored verbatim.
- Mismatch: unified diff (DTL) with 3 lines of context, colored, `-`
  stored / `+` actual. Long identical runs are elided.
- `--update` / `-u` rewrites failing/new snapshots. `--update=<glob>` limits
  which. Updates are always deliberate — never run `-u` to make CI green
  without reading the diff.
- New snapshot (no stored entry): written and reported as *new* in local
  runs; a failure under `--ci`.
- Obsolete entries (stored key never hit this run): reported as warning;
  `--ci` fails; `--update` prunes them. Filtering (`--filter`) suppresses the
  obsolete check since not everything ran.
- Inline snapshots (`SNAPSHOT_INLINE(x, R"(...)")` rewriting the source) are
  not in v1; the layout is designed so they could be added.

### Data-driven fixture tests

- `test::forEachFile("tests/fixtures/parser", ".ts", [&](Fixture &f) {...})`
  opens a `SUBCASE(f.relpath)` per file; typical body is
  `SNAPSHOT_NAMED(f.relpath, dumpSexpr(parse(f.text)))`.
- Fixture directories are the unit for adding parser/AST/rule cases: drop a
  file, run with `-u`, review the snapshot.

### Runner

Each test executable (`fastlint_tests.exe`, later `plugin_tests.exe`, …)
links `testing/main.cc` and accepts:

- `--filter <glob>[,<glob>]` on `suite.name` (`parser.*`, `*.arrow*`).
- `--tag <t>` / `--skip-tag <t>`; default `--skip-tag integration,slow,bench`.
  `--all` clears skips.
- `--list` — prints tests (with tags), for tooling and CMake discovery.
- `--json <path>` — machine-readable results (per test: status, duration,
  failures with file/line/message, snapshot diffs). Consumed by `make.ts
  test` for its summary and by CI.
- `--break` — `__debugbreak()` on first failure (default **on** when a
  debugger is attached, via `IsDebuggerPresent`).
- `--isolate` — run each test in a child process; a crash reports the test
  name and the run continues. Off by default.
- `--repeat N`, `--shuffle [seed]` — flakiness and order-dependence hunting.
- `--timeout <ms>` per test (isolate mode only, or via a watchdog thread).
- `--leaks` — print leaked blocks with stack traces (litestl tracker).
- `-u/--update[=glob]`, `--ci`, `--no-color`, `--verbose`.
- `--no-summary` — omit the trailing summary line, for `make.ts test` and
  the children `--isolate` spawns, which print their own.
- Exit code: 0 pass, 1 failures, 2 usage/crash.

Output: one line per test in verbose mode; otherwise a progress line and
then all failures in full, then a summary (`412 passed, 2 failed, 3 skipped,
1 new snapshot, 0.84s`).

### Crash handling

- `SetUnhandledExceptionFilter` (Windows) / signal handlers print the running
  test name, subcase path and `platform::getStackTrace()` before exiting.
- Under `--isolate`, the parent records the crash as a failure and moves on.

### CMake / `make.ts` integration

- `add_fastlint_test(<name> <sources…> <libs…>)` macro creates the exe,
  links `testing`, and registers one ctest entry per *suite* by running
  `--list` at configure time (cheap; re-run on rebuild via a dependency).
- `node make.ts test [--preset] [--filter] [--all] [--tag] [-u] [--asan]
  [--isolate]` builds first, runs every test exe with `--json`, aggregates,
  prints failures once. `--asan` selects the ASAN preset (see below).
- TS code (`make.ts` tooling, spikes, later the plugin runtime) is tested with
  Node's built-in runner: `node --test tools/**/*.test.ts`, wired as
  `node make.ts test --ts`.

## Sanitizers and hardened builds

- Preset `asan` (and `asan-release`): MSVC `/fsanitize=address` + `/Zi`,
  Ninja. `make.ts vcvars` puts `clang_rt.asan_dynamic-x86_64.dll` on PATH
  via the VS environment; the runner sets `ASAN_OPTIONS=
  halt_on_error=1:detect_leaks=0` (leaks are the litestl tracker's job).
- Preset `clang-asan`: clang-cl from the VS LLVM toolset, adds UBSan
  (`-fsanitize=address,undefined`) which MSVC lacks. Use when hunting UB.
- Iterator/container debugging: litestl containers have bounds `assert`s in
  debug; keep debug builds honest by not silencing them.
- `node make.ts check --asan` is the pre-merge gate for parser/fixer work;
  fuzz runs (below) always run under ASAN.

## Strategy per component

### Scanner
- Token-stream snapshots over `tests/fixtures/scanner/`.
- Invariant: concatenating every token's leading trivia + text (+ final
  trailing trivia) reproduces the source byte-for-byte, for every fixture and
  every corpus file. This is the guarantee the fixer/printer relies on.
- Rescan tests: regex vs divide, template continuation, `>>=` split inside
  type args, JSX text.
- Differential against tsgo's scanner on the conformance corpus (via the Go
  dump helper — see Parser) for token kinds and spans.

### Parser / grammar tree
- Fixture snapshots (S-expression dump with spans off; a second snapshot set
  with spans on for a small subset).
- Differential harness (`node make.ts parse-diff`): a tiny Go program in
  `tools/tsgo-dump/` links `C:/dev/TypeScript/tsc/internal/parser` and dumps
  S-exprs for a directory; our exe dumps the same; a normalizer maps known
  intentional differences; pass-rate tracked in `docs/parser-conformance.md`.
  Corpus: TS conformance/compiler cases, the TS repo's own `packages/`, and a
  vendored set of real-world files.
- Error recovery: fixtures of broken code snapshot both the tree (with
  `Error`/`Missing`) and diagnostics. Invariant: parser always terminates and
  consumes all tokens.
- Fuzz (`[slow]`, ASAN): mutate corpus files (delete/duplicate/swap random
  tokens, truncate) N times; assert no crash, no hang (watchdog), round-trip
  still holds. Seeds recorded; failures minimized and promoted to fixtures.
- Bench (`[bench]`): MB/s over the real-world corpus; `make.ts bench` stores
  results and flags regressions > 5%.

### AST layer, fixers, printer
- View accessors: one test per node kind family against fixtures — exercised
  mostly via generated tests from the kind `.def` (every declared child slot
  is reachable and returns the expected kind).
- Traversal: `children/ancestors/descendants` on snapshot fixtures.
- Comment attachment: fixtures with comments in every position; snapshot
  `leadingComments()/trailingComments()` per node.
- Printer round-trip: `print(parse(src)) == src` for every corpus file,
  unmodified tree. Run in the fast suite over fixtures, `[slow]` over the
  whole corpus.
- Fixer tests: apply a mutation, snapshot the printed output; a dedicated
  `tests/fixtures/fixer/comments/` set covers each `CommentPolicy` and each
  removal position (first/middle/last child, same-line trailing).
- Fixpoint driver: overlapping fixes from two rules converge; bounded passes.

### Type cache
- tsgo client (`[integration]`, requires `tsc`): fixture projects under
  `tests/fixtures/projects/*` with `tsconfig.json`; tests query known
  positions and check flags/structure; snapshot the interned rows.
- Protocol codec: msgpack encode/decode unit tests with captured frames
  (fixtures recorded once from a real session).
- SQLite store: in-memory DB (`:memory:`) unit tests for schema, interning,
  working-set load/flush, migration on version bump.
- Invalidation: build a small import graph, hash, edit a file, assert exactly
  which `node_types`/`rule_results` rows are dropped. Same for v2
  provenance once it exists.
- Bounded memory: `[slow]` test linting a large fixture project asserts the
  in-memory type working set never exceeds a configured high-water mark.

### Rules
- `RuleTester`-shaped harness in `testing/rule_tester.h`: `valid` cases and
  `invalid` cases with expected `(rule, line, col, messageId)` and, for
  fixable rules, expected output. Plus a snapshot of the full formatted
  diagnostics for each invalid case, so message text changes are reviewed.
- Type-aware rules use the fixture projects, tagged `[integration]`.
- Every rule ships with tests in the same commit; the rule registry test
  asserts each registered rule has a test file and a docs page.
- Dogfood (`[slow]`): lint the TS repo's `packages/typescript/src` and our
  own `tools/`; snapshot counts per rule (not positions) to catch drift.

### CLI
- End-to-end: run `fastlint.exe` on fixture projects, snapshot stdout/stderr
  with paths normalized and timings stripped; exit codes checked. Covers
  config loading, overrides, disable directives, `--fix` (compare resulting
  tree to expected), `--format json`.

### Plugins (task 7)
- N-API: `node --test` suite loads the addon, registers a TS rule, lints a
  fixture, checks diagnostics. Same test source runs against the WASM build.
- Binding surface: generated `.d.ts` snapshot — any accidental API change
  shows as a diff.

## Snapshot hygiene

- Read every snapshot diff before accepting it. If a diff is large and
  unexpected, that's a bug or a design change — not a `-u`.
- Keep snapshot inputs small and focused; one behavior per fixture file.
  Big corpus files belong in the differential/round-trip tests, not
  snapshots.
- Normalize anything nondeterministic before snapshotting (paths, timings,
  hash values, pointer ids).
- Snapshot files are reviewed in PRs like code.

## What `make.ts check` runs

1. `format --check` (clang-format, prettier).
2. Debug build + `[fast]` tests.
3. TS tests (`node --test`).
4. With `--asan`: ASAN build + `[fast]` tests. With `--all`: integration and
   slow tiers too.
