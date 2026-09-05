# Debugging

Tools and habits for finding out why something is wrong. Testing strategy
and the test framework itself are in `tests.md`.

## Getting into a debugger

- Run one test: `build/debug/fastlint_tests.exe --filter parser.arrow*`.
  `--break` is implied when a debugger is attached, so a failing `CHECK`
  stops on the failing line with everything in scope.
- Visual Studio: open the folder; `CMakePresets.json` is picked up. Set the
  startup item to a test exe and its args in `launch.vs.json`
  (`node make.ts configure` writes a template if missing).
- VS Code: `.vscode/launch.json` templates for `fastlint_tests.exe` and
  `fastlint.exe` (cppvsdbg), generated the same way.
- Narrow before debugging: `--filter`, then `SUBCASE` path from the failure
  output, then a fixture. Fixture-driven tests mean the repro is usually
  already a file.
- `--isolate` when a crash takes the whole run down and you need to know
  which test it was; then rerun that test in-process under the debugger.

## Crashes

- Unhandled exceptions print the current test, subcase path and a symbolized
  stack (`platform::getStackTrace()`). If symbols are missing, the build
  lacks `/Zi` or the PDB isn't next to the exe — the `debug`/`asan` presets
  always emit PDBs.
- `platform::debugBreak()` is the fast way to stop at an arbitrary point; it
  compiles to `__debugbreak()` and is a no-op-free trap on other platforms.

## ASAN

- Build: `node make.ts configure --preset asan && node make.ts build
  --preset asan`; or `node make.ts test --asan`.
- The VS environment (`make.ts vcvars`) supplies
  `clang_rt.asan_dynamic-x86_64.dll`. If a binary dies immediately with a
  missing-DLL error, the vcvars cache is stale: `node make.ts vcvars
  --refresh`.
- Default `ASAN_OPTIONS`: `halt_on_error=1:detect_leaks=0` (leaks are
  tracked by litestl, not ASAN). Useful extras:
  `print_stacktrace=1`, `malloc_context_size=30`,
  `allocator_may_return_null=1` for OOM behavior tests.
- Reports symbolize via the PDB; if frames are bare addresses, check the
  preset kept `/Zi /DEBUG` and that the exe wasn't copied away from its PDB.
- UBSan (MSVC has none): preset `clang-asan` uses clang-cl with
  `-fsanitize=address,undefined`. Slower to build; use for suspected UB
  (signed overflow, misaligned reads in the arena, bad enum values).
  `node make.ts fuzz --preset clang-asan` runs the fuzzer under it.
- Fuzz: `node make.ts fuzz [--iterations N] [--seed S] [--filter x]
  [--limit N] [--corpus dir...]` (asan preset by default). A failure is
  pinned to a seed, replayed to `build/<preset>/fuzz-failures/<n>-<name>`
  and minimized to `<n>-<name>.min.<ext>`; `<n>.txt` holds the report.
  Reproduce by hand: `fastlint fuzz --check <input>` (parse + invariants),
  `fastlint fuzz --replay <seed> --out <path> <file>` (regenerate a
  mutant). Promote the minimized case to a fixture.

## Leaks

- Every test is bracketed by a litestl allocation checkpoint; leaked blocks
  fail the test. `--leaks` prints each block with its allocation stack.
- Intentional permanent allocations (kind tables, binding descriptors) go
  under `alloc::PermanentGuard` so they're excluded.
- Allocate through `litestl::alloc` in our code — raw `new`/`malloc` is
  invisible to the tracker.

## Scanner / parser

- `fastlint dump-tokens <file> [--trivia]` — token stream with kinds, spans,
  flags (`precedingLineBreak`), optional trivia. First stop for "wrong token
  kind" bugs; check regex/divide and template mode transitions here.
- `fastlint dump-tree [--spans] [--errors] (<file> | --batch <list>)` —
  grammar-tree S-expression, the same dump the snapshots use. `--errors`
  lists diagnostics after the tree. `--spans` prints `Kind@start-end` and
  no token text, the form the differential harness diffs. `--batch` reads
  one path per line and prints `#file <path>` before each dump.
- `node make.ts parse-diff --jsx --filter .tsx` — the `.tsx` corpus (JSX).
- `node make.ts parse-diff --filter <name> --show 5` — diff one or a few
  files against tsgo; `--raw` skips the normalizer so both raw shapes show.
  The tsgo dumper alone: `echo <path> | .cache/parse-diff/tsgo-dump.exe`.
- `fastlint parse [--summary] [--limit N] <file|dir>...` — parses every
  `.ts`/`.tsx`/`.mts`/`.cts` file under the given paths and prints each
  diagnostic as `path:line:col: TSnnnn message`, then a summary line with
  counts, bytes and time. Exit code 1 when any file had a diagnostic. This
  is the corpus sweep: run it over a big `node_modules` tree to find grammar
  gaps. Run it under `timeout` and `ulimit -v` (the debug build, not ASAN,
  which needs an unlimited address space) so a parser hang cannot exhaust
  the machine; a hang means a token or recovery path that does not advance.
- `--trace-parser` — logs production enter/exit with the current token, and
  `tryParse` speculation start/commit/rollback. Noisy; pair with a minimal
  input.
- Differential: `node make.ts parse-diff --file <path>` prints ours vs tsgo's
  tree side by side with the first divergence highlighted. `--raw` skips the
  normalizer to see exactly what tsgo produced.
- Round-trip failures (`print(parse(src)) != src`): the byte offset of the
  first difference is reported; it is nearly always a trivia-ownership bug —
  compare `dump-tokens --trivia` around that offset.
- Hangs: every parser loop must consume a token or break; the watchdog under
  `--timeout` catches the ones that don't. Look for a recovery path that
  returns without advancing.

## AST, fixers, printer

- `fastlint dump-ast <file>` — the ergonomic layer's view (kinds, slots,
  attached comments), vs `dump-tree` for the grammar tree beneath it.
- `--fix --dry-run` prints a unified diff instead of writing; `--fix
  --verify-print` re-parses the printed output and asserts the tree matches
  the mutated one, catching printer bugs at the point of use.
- Comment went missing after a fix: dump the `CommentPolicy` chosen
  (`--trace-fixes` logs each mutation with policy and the trivia it moved).
- Dirty-flag problems (a clean node reprinted, or a dirty one emitted
  verbatim) show up as spurious whitespace changes in `--dry-run`; `dump-ast
  --dirty` marks the flagged nodes.

## tsgo / type cache

- Talk to tsgo by hand: `tsc --api --async` speaks JSON-RPC over stdio —
  readable in a terminal. The spike in `tools/spikes/tsgo-api/` has a REPL-
  ish script for issuing requests against a project.
- `--trace-tsgo` logs every request/response (decoded from msgpack) with
  timings, to stderr or `--trace-tsgo=<file>`. Look for: handles not
  released (memory growth in tsgo), serial requests that should be batched,
  `createProgram` happening more than once per project.
- tsgo crash or hang: its stderr is captured to
  `build/<preset>/tsgo-<pid>.log`. Reproduce with the reference TS client to
  separate our client bugs from tsgo bugs before filing anything.
- Cache: `fastlint cache inspect [--file <path>]` prints the `files` row,
  closure hash inputs, `node_types` count and rule-result hits for a file.
  `fastlint cache verify` recomputes a sample against live tsgo and reports
  mismatches — run this when a type-aware rule disagrees with tsc.
- The SQLite file is at `<cache-dir>/fastlint.db`; open it with the `sqlite3`
  CLI (`.schema`, `select … from node_types where file_hash = …`).
- Stale results after an edit: check the import graph (`fastlint deps
  <file>`) — an unresolved import means the closure hash didn't include the
  dependency. `--no-cache` confirms whether the cache is the culprit.
- Type-flag confusion: flag bit names come from the generated
  `enum_values` table; `fastlint cache inspect --decode-flags <n>` spells
  them out.

## Rules

- `fastlint --rule <name> --explain <file>` — for one rule, prints each
  node it visited, what it asked `TypeFacts`, and why it did or didn't
  report. Rules get this for free by using `ctx.explain(...)` instead of ad
  hoc logging.
- `--print-ast` alongside a lint run shows the tree the rule saw (post-
  parse, pre-fix).
- Rule tester failures print the expected vs actual diagnostic table and,
  for fixable rules, a diff of the fixer output.
- Disable-directive issues: `--report-unused-disable-directives` and
  `--trace-directives` (which comment matched which node range).

## Performance

- `fastlint --timing` — per-phase (scan, parse, bind, rules by name, type
  queries, cache I/O, print) totals and per-file worst offenders.
- `--trace-json <file>` writes Chrome trace-event format; open in
  `chrome://tracing` or Perfetto. Spans per file/phase/rule plus tsgo
  request spans, so cross-process waits are visible.
- `node make.ts bench [--save <name>] [--compare <name>] [--corpus dir...]`
  for parse throughput; results under .cache/bench/. Run on a quiet
  machine; `--repeat 5` (default) keeps the best pass.
- Native profiling: Visual Studio's CPU Usage tool on `fastlint.exe` with the
  `relwithdebinfo` preset; or Windows Performance Recorder + WPA for
  wall-clock/blocking analysis (useful for the tsgo pipe waits).
- Allocation churn: `--alloc-stats` prints litestl tracker totals per phase
  (count, bytes, peak). A hot path allocating per node means an SBO
  container that should be a range into the arena.

## `make.ts` itself

- `--verbose` on any command prints every spawned argv and cwd.
- `node --inspect-brk make.ts <cmd>` to debug the tooling in Chrome DevTools
  or VS Code.
- Environment problems are almost always the cache: `node make.ts env
  --refresh` and `node make.ts vcvars --refresh` re-probe VS. `node make.ts
  env` prints what it found and where.
