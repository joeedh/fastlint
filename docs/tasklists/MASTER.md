# fastlint — Master Task List

Status legend: `[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked.
See `docs/STRATEGY.md` for the design each task implements.

Environment facts (verified 2026-09-04):
- Node v24.14.0; `tsc` 7.0.2 (native, Go) on PATH. `tsgo` binary name is not on
  PATH — TS 7 RC+ exposes it as `tsc`. API mode: `tsc --api [--pipe <path>]
  [--async]` (msgpack over stdio/pipe by default; `--async` = JSON-RPC).
- TypeScript (Go port) checkout: `C:/dev/TypeScript` (branch
  `joeedh/profiling-tests`, HEAD 253c5e2074a, 2026-08-31). API server:
  `tsc/internal/api/`; protocol: `tsc/internal/api/proto.go`; TS client:
  `packages/typescript/src/api/`.
- VS 18 Community: `C:\Program Files\Microsoft Visual Studio\18\Community`.
  Bundled: cmake, ninja (`Common7/IDE/CommonExtensions/Microsoft/CMake/`),
  clang-format (`VC/Tools/Llvm/x64/bin`), `VC/Auxiliary/Build/vcvarsall.bat`.
  None on PATH — scripts must locate via `vswhere`.
- Linux/WSL builds with gcc or clang. `make.ts` takes cmake, ninja and
  clang-format off PATH there and skips vcvars; clang-format must be 20 or
  newer, since 19 lays out a requires-expression and a wrapped `if` differently
  and would rewrite committed sources.
- `@pathtx/prettier` goes in as a local dev dependency (task 1.1); never rely
  on the global `prettier@3.2.5`. `make.ts format` invokes it via the local
  `node_modules/.bin`.
- `vendor/litestl` submodule checked out (C++20, `.cc`/`.h`, CMake).

Conventions (apply to every task):
- litestl containers over the STL in all our C++ (`util::Vector/Map/Set/
  Span/Array/string`, …). `std::` containers only inside boundary adapters
  (SQLite, msgpack, N-API, OS). `util::Array` is an alias of `std::array` —
  use the alias anyway. See docs/STRATEGY.md "litestl usage".
- Allocate via `litestl::alloc` so the leak tracker sees it.

---

## 1. Build environment

Goal: `node make.ts <cmd>` drives everything; a fresh clone + VS builds with
one command.

### 1.1 Repo scaffold
- [x] `package.json` (private, `"type": "module"`, pnpm), dev deps: `yargs`,
  `@pathtx/prettier`, `@types/node`, `@types/yargs`.
- [x] `tsconfig.json`: `strict`, `strictNullChecks`, `module: nodenext`,
  `target: es2023`, `noEmit`, `allowImportingTsExtensions`,
  `verbatimModuleSyntax`, `erasableSyntaxOnly` (enforces node-runnable TS:
  no enums, no namespaces, no parameter properties).
- [x] `.prettierrc` pointing at the `@pathtx/prettier` fork; `.clang-format`
  (base: LLVM or Google, 2-space, 100 col — match litestl's style).
- [x] `.gitignore`: `build/`, `node_modules/`, `.cache/`. Snapshot files
  (`tests/__snapshots__/`) are **committed**.
- [x] `.editorconfig`.

### 1.2 `make.ts` dispatch (yargs)
- [x] `make.ts` at repo root, run as `node make.ts …` (no build step).
  Commands live in `tools/make/<cmd>.ts`, one file each, auto-registered.
- [x] `env` — locate toolchain: `vswhere` → VS root → cmake/ninja/clang-format
  /vcvarsall paths. Cache result in `.cache/env.json`. `--refresh` to
  re-probe. Prints a table.
- [x] `vcvars` — run `vcvarsall.bat x64`, diff the environment, persist to
  `.cache/vcvars-x64.json`; every compile-adjacent command imports it into
  `process.env` before spawning. Re-run when VS version changes.
- [x] `configure [--preset debug|release|relwithdebinfo] [--wasm]` — cmake
  configure with Ninja generator into `build/<preset>/`, exports
  `compile_commands.json`. `--wasm` reports that it lands with task 7.3.
- [x] `build [--preset] [--target]` — ninja via cmake `--build`; passes
  `-j`; surfaces first error clearly.
- [x] `test [--preset] [--filter]` — ctest, or direct test binary.
- [x] `clean [--preset|--all]`.
- [x] `format [--check]` — clang-format over `source/**/*.{cc,h}`, prettier
  over `**/*.ts` (excluding `vendor/`, `build/`, `node_modules/`).
- [x] `deps` — submodule init/update; `deps fetch <name>` clones a pinned
  external into `vendor/`. DTL is registered; sqlite and msgpack are added
  with the tasks that need them.
- [x] `run [args…]` — build then run `fastlint` with args.
- [x] Shared helpers in `tools/make/lib/`: `spawn` with inherited stdio +
  exit-code propagation, `log`, path utils. No shell string concatenation —
  argv arrays only.

### 1.3 CMake
- [x] Root `CMakeLists.txt`: C++20, `.cc` extension, `add_subdirectory(
  vendor/litestl)` scoped to `platform`, `util`, `path` (avoid `math`,
  `extern/eigen`, `io`, `binding` until needed — check litestl exposes
  options; add them if not).
- [x] `CMakePresets.json` matching `make.ts` presets; Ninja generator;
  `CMAKE_EXPORT_COMPILE_COMMANDS`. Presets: `debug`, `release`,
  `relwithdebinfo`, `asan` (MSVC `/fsanitize=address /Zi`), `clang-asan`
  (clang-cl, `-fsanitize=address,undefined`). All emit PDBs.
- [x] `make.ts` flags `--asan` / `--preset clang-asan` on `configure`,
  `build`, `test`, `check`; runner exports `ASAN_OPTIONS` defaults; verify
  `clang_rt.asan_dynamic-x86_64.dll` resolves from the cached vcvars env.
- [x] `source/` layout: `fastlint/` (lib), `cli/` (exe), `tests/`.
- [x] Warnings-as-errors on our code, not on vendor.
- [x] Hello-world `fastlint.exe` builds and runs via `node make.ts run`.

### 1.4 CI-ish sanity
- [x] `node make.ts check [--asan] [--all]` = format --check + build + C++
  tests + TS tests (`node --test`). Document in `README`.

### 1.5 Test framework (`source/testing/`, see `docs/tests.md`)
- [x] Core: `TEST`, `TEST_TAGGED`, `SUBCASE`, `SKIP`, `INFO`, `FAIL`; static
  registry; `describe(const T&)` customization point with overloads for
  litestl containers/spans and generated kind enums.
- [x] Assertions: `CHECK`/`REQUIRE` with binary-expression decomposition
  printing both operands; `CHECK_EQ/NE/LT/…` fallbacks. `file:line`,
  expression, values, `INFO` scope and subcase path on every failure.
- [x] Snapshots: `SNAPSHOT`, `SNAPSHOT_NAMED`; `tests/__snapshots__/
  <file>.snap` format (`### key` headers, `\` escape, sorted keys); DTL
  unified diff with context + color on mismatch; `-u/--update[=glob]`;
  new/obsolete detection; `--ci` semantics.
- [x] `test::forEachFile(dir, ext, fn)` fixture driver.
- [x] Runner (`testing/main.cc`): `--filter`, `--tag/--skip-tag/--all`,
  `--list`, `--json`, `--break` (auto when debugger attached), `--isolate`,
  `--repeat`, `--shuffle`, `--timeout`, `--leaks`, `--no-color`,
  `--verbose`; exit codes 0/1/2.
- [x] Leak checkpoint around each test via litestl alloc tracker; leaks fail.
- [x] Crash handler: unhandled-exception filter prints test name, subcase
  path, `platform::getStackTrace()`; `--isolate` parent records and
  continues.
- [x] CMake `add_fastlint_test()` macro; per-suite ctest registration from
  `--list`.
- [x] `make.ts test` aggregates `--json` from every test exe; `--ts` runs
  `node --test`.
- [ ] `testing/rule_tester.h` (RuleTester-shaped) lands with task 6.1.
- [x] Self-tests for the framework: decomposer output, describe() rendering,
  glob matching, subcase replay, tag selection, snapshot diff rendering,
  the fixture driver. Obsolete/new snapshot handling is exercised by hand;
  automating it needs a second snapshot file the runner can dirty.
- [x] Vendor DTL via `make.ts deps` (same repo litestl uses:
  `github.com/joeedh/dtl`).

---

## 2. tsgo API surface test

Goal: know exactly what we get from `tsc --api` before designing the type
layer. Deliverable: `docs/tsgo-api.md` + a working TS spike.

Done 2026-09-04. Findings in docs/tsgo-api.md; spike at
`tools/spikes/tsgo-api/` (`node tools/spikes/tsgo-api/main.ts [phase…]`).
The results that change later tasks:
- The msgpack transport is a 6-byte envelope around JSON, so no msgpack
  library is needed (drops the dependency from 5.1).
- The released 7.0.2 binary serves 111 of the checkout's 142 methods.
  `createProgram`, `batchRequests` and `updateTemporarySnapshot` are absent,
  and several endpoints spell their type-id parameter differently.
- Positions address tokens, not expressions. Expression-level queries need a
  `NodeHandle`, which we synthesize from the encoded parse tree
  (`getSourceFile`) — added as a sub-task under 5.1.

### 2.1 Protocol study (read-only, `C:/dev/TypeScript/tsc`)
- [x] Read `internal/api/proto.go`: `Method*` list, `TypeResponse`,
  `SymbolResponse`, `SignatureResponse`, `NodeHandle`, snapshot/project
  model (`initialize`, `updateSnapshot`, `createProgram`,
  `getDefaultProjectForFile`, `release`, `batchRequests`).
- [x] Read `internal/ipc/` for framing (msgpack sync vs JSON-RPC async),
  `transport_windows.go` for named-pipe specifics.
- [x] Read `packages/typescript/src/api/{sync,async,proto.ts}` — the
  reference client; note lifecycle and handle-release discipline.
- [x] Map `TypeFlags` / `ObjectFlags` / `SymbolFlags` / `CheckFlags` /
  `ElementFlags` numeric values (`enum_values_generated.go`) — we'll need
  them in C++.
- [x] Note protocol-stability signals: any `// unstable` markers, version
  negotiation in `initialize`.

### 2.2 Spike (TS, in `tools/spikes/tsgo-api/`)
- [x] Spawn `tsc --api --async` (JSON-RPC, easiest to eyeball) against a
  small fixture project; `initialize` → snapshot → `createProgram` →
  `getTypeAtLocation` on a few node positions.
- [x] Exercise the queries rules will need: `getTypeAtLocation`,
  `getTypesOfType` (union members), `getSignaturesOfType`,
  `getReturnTypeOfSignature`, `getNonNullableType`, `isTypeAssignableTo`,
  `isArrayLikeType`, `getSymbolAtLocation` → `declarations` (for
  provenance), `typeToString` (for diagnostics text only).
- [x] Measure: startup time, `createProgram` time on a mid-size project,
  per-query latency, batch (`batchRequests`) throughput.
- [x] Measure the same over msgpack sync mode; decide which transport C++
  uses.
- [x] Test `updateSnapshot` with an edited file — does it incrementally
  re-check? Cost?
- [x] Test FS `--callbacks` — could we serve file contents from our cache?

### 2.3 Decisions to record in `docs/tsgo-api.md`
- [x] Transport: msgpack/stdio vs named pipe vs JSON-RPC.
- [x] Which `TypeResponse` fields feed the interned type row; what
  "one hop" means concretely (which follow-up methods).
- [x] Provenance: `SymbolResponse.declarations` → `NodeHandle` → file. Enough
  for v2 invalidation?
- [x] Whether we can reuse tsgo's `NodeHandle`s as our node ↔ position key,
  or must stay position-based.
- [x] Update `docs/STRATEGY.md` open questions accordingly.

---

## 3. Parser → grammar tree

Goal: scanner + recursive-descent parser producing a *grammar tree* (a
faithful parse tree over the token stream) with error recovery. The
ergonomic AST (task 4) is a separate layer over it, so the parser can start
before the AST design is reviewed.

### 3.1 Scanner

Done 2026-09-04 (hand-written unit tests; the tsgo token-stream diff below is
still open). Notes:
- Rescans rewrite the current token in place (`rescanGreaterThan()` hands out
  one `>` per call; `scanAll()` auto-rescans a substitution-closing `}` into
  the CloseBrace token *plus* the following middle/tail token, keeping both).
- Line starts record `break_offset + 1` (first byte of the terminator), one
  entry per break, CRLF counted once.
- Leading trivia of a token excludes a trailing pure-whitespace run.

- [x] Token kinds table (single source of truth, generated header from a
  `.def` or `constexpr` array).
- [x] UTF-8 source, byte offsets, line-start table as side product.
- [x] Modes: normal, template (head/middle/tail), JSX text, JSX identifier,
  regex. `rescan*` API: slash, template tail, `>`-family, JSX identifier,
  JSX text.
- [x] Numeric literals (all bases, separators, bigint), strings with
  escapes, identifiers incl. unicode escapes and `#private`.
- [x] Trivia collection: whitespace/newline/comments into the trivia array;
  `precedingLineBreak` flag on tokens for ASI.
- [x] Snapshot/rewind.
- [~] Unit tests: hand-written expectations in `scanner_test.cc` (29 tests)
  pass. Token stream diffs against tsgo's scanner on a corpus (via a small Go
  or `tsc --api` helper — decide in 2.x) still open.

### 3.2 Parser

First working slice done 2026-09-04 (public header `syntax/parser.h`; the
implementation is split under `syntax/parser/`: `parser.cc` (class plumbing,
`parseFile`), `statements.cc`, `declarations.cc`, `expressions.cc`,
`types.cc`, `dump.cc`, with shared helpers inline in `parser/internal.h`.
Tests in `syntax/parser_test.cc`: 21 tests green; suite 65 passed / 1 skipped).
Working: statements (all forms incl. for-of/for-in with `using`/`await using`
heads, ASI), declarations (functions/generators/ambient, classes with
modifiers + accessors + constructor parameter properties + index/call/construct
signatures, interfaces, type aliases, enums, namespaces, import/export forms,
import-equals), expressions (full precedence climbing, arrows via paren
speculation with diagnostic rollback, call/member chains, object/array
literals, templates driven through scanner rescans, `import.meta`/`import()`),
a large slice of the TS type grammar (predicates, unions/intersections,
operators, tuples, mapped types with `as`, function/constructor types, type
queries, `infer`, import types, template literal types, references/args), a
speculation mechanism (`Parser::Mark` = scanner state + tree build state +
diagnostic count), and error recovery with `ErrorNode`/`Missing` plus progress
guards in every repetition loop.

Parser notes:
- Node kinds gained `PropertyAccessExpression` (def had none).
- `GrammarTree` gained `buildState()/restoreBuild()` (speculation) and
  `setSourceForBuild()` (token-text dumps).
- Scanner quirk worked around in the parser: middle-mode `scanTemplate` that
  runs to the closing backtick reports TemplateMiddle; the parser detects the
  trailing backtick and calls `rescanTemplateTail(true)`.
- `dumpTree()` (S-expression with node flags and the node's unowned tokens)
  lives in `syntax/parser/dump.cc`; tests assert exact dumps.
- Recurring bug class while building this: a helper that appends children
  directly to the enclosing node must not have its kNoNode return value
  passed to addChild() (parseParameterList/parseClassLike). Keep those
  returning void.

Still open in 3.2:
- [~] Statements, declarations, expressions with precedence climbing,
  patterns/destructuring, classes (fields, accessors, `accessor`, `static`
  blocks, decorators), modules (`import`/`export` all forms, `import type`,
  attributes), `using`/`await using`.
  - [x] `using`/`await using` statements and `for` heads (`FLAG_USING`,
    `FLAG_AWAIT` on the list; `for await` flags the loop), with `using` as
    a plain identifier when no binding name follows on the line.
  - [x] Decorators on classes (either side of `export`), class members,
    parameters, and class expressions; each decorated node owns its
    `Decorator` children.
  - [x] Non-erasable syntax: enums (`const`/`declare`, members directly
    under `EnumDeclaration`, `ComputedPropertyName`), namespaces/modules,
    parameter properties incl. `override`, `import x = require()`,
    `export import`, `export =` (`ExportAssignment`), `export as namespace`
    (`NamespaceExportDeclaration`), `declare enum/interface/type`, `declare
    abstract class`. Modifier tokens belong to their declaration node.
  - [x] `new.target`, `export default interface`, `export * from` without
    an alias, `import type x = require()`, `global { }` augmentations
    inside ambient modules, call/construct signatures in type literals,
    `bigint` as a type keyword and as a name, `accessor` as a name.
  - [x] Corpus sweep (`fastlint parse <dir>`, see docs/debugging.md) over
    4477 files of a real project incl. node_modules: every non-JSX file
    parses without diagnostics. Found on 2026-09-04: the scanner looped on
    a UTF-8 BOM (any non-ASCII character that cannot start a name produced
    a zero-length token), now Unicode whitespace is trivia and other such
    characters are one-character error tokens; a `#!` shebang line is
    trivia.
  - [ ] Class `static` blocks.
- [~] Types: full TS type grammar (conditional, mapped, template literal,
  indexed access, `infer`, `satisfies`, `asserts`, predicates, `unique
  symbol`, abstract constructors, variance annotations).
  - [x] Function and constructor types (speculative `(…) =>` head, `new`,
    `abstract new`), conditional types with the `extends`-operand
    restriction, `infer X extends C`, indexed access, `readonly`/optional
    type members, computed names, index signatures, `this` parameters,
    `as const`, `void`.
  - [x] Expressions: `as`/`satisfies`, type arguments on calls and `new`
    (speculative, TS's follow-set), tagged templates, regex literals via
    `rescanSlash`, `>>` splitting via `rescanGreaterThan`, generic arrow
    heads `<T>(…) =>`, `**` right-associativity (other operators were
    right-associative by mistake), reserved words as member names.
  - [x] Mapped types: `+`/`-` `readonly` and `?` modifiers (flags
    `FLAG_READONLY`/`FLAG_OPTIONAL`, the sign kept as a token), `as`
    clauses; optional and named-rest tuple elements (`T?`, `name?: T`,
    `...name: T`); import types with qualifiers and type arguments;
    function-type return types may be conditional inside an `extends`
    operand; `TypeParameters`/`TypeArguments` list nodes.
  - [ ] `unique symbol` node, variance annotations recorded (parsed and
    dropped today), JSX.
- [~] Contexts: `await`/`yield` flags, ambient (`declare`), strict-mode
  reserved words, `in` operator disallowed in for-init.
  - [x] `await`/`yield` follow the enclosing function's `async`/`*` flags
    (functions, methods, object literal methods); `in` is banned in a `for`
    head and re-allowed inside brackets, arguments and blocks
    (`detail::FlagScope`).
  - [ ] Ambient bodies, strict-mode reserved words as names.
- [~] Speculation: arrow vs parenthesized expr (done), generic call vs
  comparison, type-assertion vs JSX in `.ts` vs `.tsx` (basic `<T>expr` done).
- [~] ASI rules (restricted productions: `return`, `throw`, `break`,
  `continue`, postfix `++/--`, arrow `=>`, `yield`, `async`) — done for the
  productions implemented.
  - [x] `do … while (x)` takes ASI after `)` without a line break.
  - [x] Statements ended by ASI carry `FLAG_ASI`, so rules can tell a
    written `;` from an inserted one from a missing one (diagnostic).
- [ ] JSX (`.jsx`/`.tsx`), JS mode (`.js` incl. JSDoc *ranges* only, no
  JSDoc parsing yet).
- [~] Error recovery: `Error`/`Missing` nodes, sync sets per production,
  no infinite loops on garbage (basic level done).
  - [x] Every repetition goes through `parseList`/`parseDelimitedList`
    (parser/internal.h); the loop owns progress, productions never unwind.
  - [x] Sync sets per `ListKind` in parser/lists.cc; a token an enclosing
    list accepts ends the inner list, anything else is skipped as an
    `ErrorNode` with TS's "X expected" code.
  - [x] Diagnostics at the same offset as the previous one are dropped, so
    an abort through several lists reports once.
  - [x] Real-world corpus (`parser.real_world_ts_code`) parses with no
    diagnostics.
- [ ] Diagnostics: TS-compatible codes where practical.

### 3.3 Grammar tree representation
- [ ] Flat per-file arena; `uint32` node ids; `{kind, flags, parent,
  first_child, child_count, first_token, token_count}`; shared child-id
  vector; token and trivia arrays (see docs/STRATEGY.md "AST").
- [ ] Debug dump (S-expression) for tests and diffing.

### 3.4 Differential harness
- [ ] `node make.ts parse-diff [--corpus <path>]`: parse every file in
  `C:/dev/TypeScript/tsc/testdata` conformance/compiler cases (locate the
  actual path in the Go repo) + our own fixtures; compare against tsgo's
  AST (via `tsc --api` `getSourceFile`, or a tiny Go program linking
  `internal/parser` that dumps S-exprs — likely faster and richer).
- [ ] Normalization layer for known, intentional shape differences.
- [ ] Track pass-rate in `docs/parser-conformance.md`; grind to ~100%.
- [ ] Perf benchmark: MB/s on a large real-world corpus (e.g. the TS repo's
  own `src/`), in CI-ish `node make.ts bench`.

---

## 4. AST design (reviewed)

Goal: the ergonomic layer over the grammar tree. **Design reviewed before
implementation.** Deliverable: `docs/ast-design.md` + header stubs.

### 4.1 Design doc
- [ ] Node kind taxonomy: which grammar-tree kinds are exposed, which
  collapse (parens? `as`/`satisfies`? type vs value identifiers).
- [ ] Generic surface: `kind()`, `parent()`, `children()`, `ancestors()`,
  `descendants(kind)`, `span()`, `tokens()`, `leadingComments()`,
  `trailingComments()`, `source()`.
- [ ] Typed views: naming scheme, child-slot tables, `Optional<Node>` for
  absent children, list accessors returning `Span<NodeId>`.
- [ ] Iteration & querying: visitor-free traversal, kind filters, small
  pattern-match helper (`match<CallExpr>(node, [&](CallExpr c){…})`).
- [ ] Identifier/scope layer: binder producing scopes, declarations,
  references — separate pass, same arena. Decide v1 scope (needed by
  `no-unused-vars`, `no-shadow`, `prefer-const`).
- [ ] Mutation & fixers: append-only arena, dirty flags, `replace/insert/
  remove(CommentPolicy)`, builders. Printer contract.
- [ ] Ownership/lifetime: arena per file, ids not pointers, rules never hold
  across files.
- [ ] Interop hooks for task 7: everything reachable by id + integer kind, so
  bindings are flat and cheap.
- [ ] Anti-goals: no per-node heap allocation, no virtual dispatch, no
  node-class hierarchy.
- [ ] **Review checkpoint with Joe.** Iterate until signed off.

### 4.2 Implementation
- [ ] Kind tables + generated view headers (single `.def`, generator in
  `tools/`).
- [ ] Generic API + views.
- [ ] Trivia/comment attachment rule + comment side table.
- [ ] Binder (scopes/refs) v1.
- [ ] Fixer API + printer (verbatim for clean, synthesized for dirty,
  indentation/style sniffing).
- [ ] Fixpoint driver with reparse between passes.
- [ ] Tests: round-trip (parse → print == source for every corpus file),
  fixer unit tests with comment-preservation cases.

---

## 5. Type cache system

Goal: type facts from tsgo, cached in SQLite, bounded memory. Depends on 2.x
decisions.

### 5.1 tsgo client (C++)
- [ ] Process management: spawn `tsc --api`, stdio or named pipe (per 2.3),
  lifecycle, crash detection/restart, one per tsconfig project, concurrency
  cap.
- [ ] Wire protocol: the msgpack 3-tuple envelope (hand-rolled, ~60 lines —
  see docs/tsgo-api.md "Transport") plus JSON payloads; request/response
  correlation. No `batchRequests` in 7.0.2: batch through the plural
  endpoints (`getTypesAtPositions`, `getTypeAtLocations`).
- [ ] Version gate: read `tsc --version`, refuse an unpinned version, record
  it in the cache `meta` table. Per-version parameter-name table beside the
  client; regenerate with `main.ts compat`.
- [ ] Snapshot/program management: `initialize`, `updateSnapshot`
  (`openProjects`/`openFiles`/`fileChanges`), `getDefaultProjectForFile`,
  `release` per snapshot. `createProgram` does not exist in 7.0.2.
- [ ] `getSourceFile` decoder (44-byte header + 28-byte flat nodes) and the
  node-id -> tsgo-index side table, rebuilt on content-hash change, so rules
  can query expressions by `NodeHandle` rather than by position.
- [ ] Serve file contents over `--callbacks=readFile,fileExists` from our
  cache, so the server and our parser see the same bytes.
- [ ] Typed request wrappers for the query set from 2.2.

### 5.2 Type facts layer
- [ ] `TypeFacts` interface rules call: `typeOf(node)`, `isNullable`,
  `isAnyLike`, `isPromiseLike`, `isArrayLike`, `unionMembers`,
  `callSignatures`, `returnType`, `assignableTo`, `symbolOf`,
  `declarationsOf`.
- [ ] Lazy fetch; per-file working set; flush on file completion.
- [ ] Interning: structural hash of `TypeResponse` + one hop; `types`,
  `type_children`, `symbols`, `strings` tables in memory with LRU.

### 5.3 SQLite store
- [ ] Vendor sqlite amalgamation via `make.ts deps`; WAL; single writer
  thread; batched commits.
- [ ] Schema: `files(path, content_hash, closure_hash, tsconfig_hash)`,
  `types`, `type_children`, `symbols`, `node_types(file_hash, offset,
  type_id)`, `rule_results(file_hash, closure_hash, rule, payload)`,
  `meta(schema_version, tsgo_version, lib_hash)`.
- [ ] Migration/versioning; drop-and-rebuild on schema or tsgo version
  change.
- [ ] `--no-cache`, `--cache-dir`, `cache verify` (recompute a sample and
  compare).

### 5.4 Invalidation
- [ ] Import graph from parser → closure hash per file.
- [ ] v1 file-closure invalidation.
- [ ] Rule-result replay for unchanged (file, closure).
- [ ] v2 per-type provenance (decl file hashes per type row) — after v1 is
  measured on a real monorepo.

### 5.5 Measurement
- [ ] Cold vs warm run timings on a real project; memory high-water mark;
  cache size on disk.

---

## 6. Lint rules

Goal: enough rules to lint a real project; rule API proven for task 7.

### 6.1 Rule framework
- [ ] Rule interface: metadata (name, docs URL, fixable, type-aware flag),
  `create(ctx)` registering kind-indexed callbacks; `ctx.report(node, msg,
  fix?)`.
- [ ] Dispatch: single tree walk, per-kind callback lists — no per-rule
  traversal.
- [ ] Config: `fastlint.config.{json,ts}`; severity; per-rule options;
  overrides by glob; `extends` presets.
- [ ] Disable directives: `// fastlint-disable[-next-line] rule`, and
  `// eslint-disable*` compatibility (decision in docs/STRATEGY.md open Qs).
- [ ] Output: pretty terminal, `--format json`, SARIF later.
- [ ] Rule test harness: `valid`/`invalid` cases with expected messages and
  fixer output (ESLint `RuleTester`-shaped for familiarity).

### 6.2 Syntactic rules (initial set)
- [ ] `no-debugger`, `no-console`, `eqeqeq`, `no-var`, `prefer-const`,
  `no-unused-vars` (needs binder), `no-shadow`, `no-empty`,
  `no-unreachable`, `no-duplicate-case`, `no-fallthrough`,
  `no-constant-condition`, `no-self-assign`, `curly`,
  `@typescript-eslint/consistent-type-imports` (fixable),
  `no-non-null-assertion`, `prefer-as-const`, `array-type` (fixable).
- [ ] Each: docs page, tests, fixer where applicable.

### 6.3 Type-aware rules (initial set)
- [ ] `no-floating-promises`, `await-thenable`, `no-misused-promises`,
  `no-unnecessary-condition`, `no-unsafe-*` family (`any` flow),
  `restrict-template-expressions`, `strict-boolean-expressions`,
  `prefer-nullish-coalescing`, `no-unnecessary-type-assertion` (fixable).
- [ ] Each rule's type queries logged so the cache working set is measured.

### 6.4 Dogfood
- [ ] Lint `C:/dev/TypeScript/packages/typescript/src` and our own `tools/`;
  compare against typescript-eslint output; triage diffs.

---

## 7. Plugin API (TS rules via litestl bindings)

Goal: custom rules in TypeScript, loaded as an N-API native module or as
WASM, over the same flat AST.

### 7.1 Binding surface
- [ ] Bind arena/node/token/trivia access (ids + kinds), `TypeFacts`
  predicates, `ctx.report`, fixer builders — all integer-handle based, no
  object graph across the boundary.
- [ ] Generate TS `.d.ts` + runtime via litestl `binding/generators/
  typescript`; kind enums shared from the same `.def` as C++.
- [ ] Batch-friendly traversal: expose `descendants(kind)` returning typed
  arrays so a TS rule does one call, not N.

### 7.2 N-API build
- [ ] `node make.ts build --napi`: cmake target producing `fastlint.node`;
  node-addon-api headers via `make.ts deps`.
- [ ] Rule loading: `fastlint.config.ts` imports TS rule modules; the CLI
  hosts node (or the node CLI hosts the native core — decide; the latter is
  simpler: `fastlint` npm package wraps the `.node` addon).
- [ ] Threading model: TS rules run on the JS thread; C++ walks files in
  parallel and queues callback batches.

### 7.3 WASM build
- [ ] `node make.ts build --wasm` via litestl's `build_files/WASM.cmake` and
  emsdk discovery in `make.ts env`.
- [ ] Same TS runtime over the WASM heap (litestl `typescriptRuntime`).
- [ ] Use case: browser/playground, and editors without native addons.

### 7.4 Ecosystem
- [ ] `create-fastlint-rule` template; docs; example rules ported from
  typescript-eslint.
- [ ] Perf budget: TS rule overhead vs native rule, documented.

---

## Cross-cutting

- [x] `docs/tests.md` — testing strategy + framework spec (written
  2026-09-04; update as the framework lands).
- [x] `docs/debugging.md` — debugging aids per subsystem (written
  2026-09-04; each listed flag/subcommand becomes a real task in its
  component: `dump-tokens`, `dump-tree`, `dump-ast`, `--trace-parser`,
  `--trace-tsgo`, `--trace-fixes`, `cache inspect/verify`, `--explain`,
  `--timing`, `--trace-json`, `--alloc-stats`).
- [ ] Fuzz harness for the parser (`[slow]`, ASAN-only): token-level
  mutations of the corpus, seed logging, failure minimization to fixtures.
- [ ] `make.ts bench` with JSON baselines and `--compare`.
- [ ] `README.md` — what/why, quickstart, `make.ts` commands.
- [ ] `CLAUDE.md` — repo conventions (build, style, layout), pointing at
  docs/STRATEGY.md and this list.
- [ ] Bench suite (`node make.ts bench`) tracking parse MB/s, lint files/s,
  warm-cache rerun time.
- [ ] Decide ESLint compatibility surface (config, rule names, directives).
