# lintrix — Master Task List

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
- [x] `configure [--preset debug|release|relwithdebinfo] [--wasm] [--napi]` —
  cmake configure with Ninja generator into `build/<preset>/`, exports
  `compile_commands.json`. `--wasm` configures `build/wasm` under the
  Emscripten toolchain and `--napi` configures `build/napi` through cmake-js
  (docs/embedding.md).
- [x] `build [--preset] [--target]` — ninja via cmake `--build`; passes
  `-j`; surfaces first error clearly. `--wasm` and `--napi` build the
  embeddings, and `--smoke` loads the result and lints one line through it.
- [x] `test [--preset] [--filter]` — ctest, or direct test binary.
- [x] `clean [--preset|--all]`.
- [x] `format [--check]` — clang-format over `source/**/*.{cc,h}`, prettier
  over `**/*.ts` (excluding `vendor/`, `build/`, `node_modules/`).
- [x] `deps` — submodule init/update; `deps fetch <name>` clones a pinned
  external into `vendor/`. DTL is registered; sqlite and msgpack are added
  with the tasks that need them.
- [x] `run [args…]` — build then run `lintrix` with args.
- [x] `parse-diff [--corpus …] [--filter x] [--limit N] [--jsx] [--top N]
  [--show N] [--no-build] [--no-report]` — tsgo differential harness
  (task 3.4).
- [x] `fuzz [--preset asan|clang-asan] [--iterations N] [--seed S]
  [--batch N] [--timeout S] [--no-minimize] [--corpus …]` — ASAN mutation
  fuzz (task 3.5).
- [x] `bench [--preset release] [--repeat N] [--save name] [--compare name]
  [--corpus …]` — parse MB/s with JSON baselines under .cache/bench/.
- [x] `lint` and `lint:prose [--json] -- <commentlint args>` — prose
  linting of comments and markdown with comment-lint
  (.commentlintrc.jsonc).
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
- [x] `source/` layout: `lintrix/` (lib), `cli/` (exe), `tests/`.
- [x] Warnings-as-errors on our code, not on vendor.
- [x] Hello-world `lintrix.exe` builds and runs via `node make.ts run`.

### 1.4 CI-ish sanity
- [x] `node make.ts check [--asan] [--all]` = format --check + build + C++
  tests + TS tests (`node --test`). Document in `README`.
  - [x] The typecheck step failed on `noUncheckedIndexedAccess` errors in
    tools/parse-diff and tools/make/parse-diff.ts (seen 2026-09-06); fixed
    2026-09-08 with index-access assertions where a length guard already
    bounds the slot. `tsc --noEmit -p tsconfig.json` is clean. Spike
    fixtures are excluded from tsconfig.

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
- [x] CMake `add_lintrix_test()` macro; per-suite ctest registration from
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
  lives in `syntax/parser/dump.cc`; tests assert exact dumps. The walk is
  iterative: a 40 KB left-associative `+` chain overflowed the recursive one.
- Status 2026-09-05: parser mostly finished. Validated on all of
  C:/dev/visualnovel including node_modules (about 4000 files) and against
  tsgo over the TypeScript test corpus (3.4). Shape changes made for the
  harness: entity names are `Identifier` or a flat `QualifiedName` over
  `Identifier` children (type references, `typeof`, import-equals); type
  keywords are `KeywordType`; `TypeParameter`, `NamedTupleMember` and
  `TypePredicate` name their binding with a child node; `LiteralType` holds
  its literal node; a bare arrow parameter gets a `Parameter`; `new X()`
  owns its arguments (`new X().y()` was mis-nested); `catch (e)` wraps the
  binding in `VariableDeclaration`; `#x` after `.` is `PrivateIdentifier`;
  `export * from` has no `NamespaceExport` node; `class implements X {}` is
  anonymous.
- Speculation cost: `isArrowHead` first rejects by the two tokens after `(`
  (tsgo's `isParenthesizedArrowFunctionExpression`) and memoizes failed
  probe positions in `m_notArrowHead`; without both, `(a = (a = (a = …`
  was exponential (a 9 KB corpus file never finished).
- The CLI transcodes UTF-16 sources (by BOM) to UTF-8 before parsing.
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
  - [x] Corpus sweep (`lintrix parse <dir>`, see docs/debugging.md) over
    4477 files of a real project incl. node_modules: every non-JSX file
    parses without diagnostics. Found on 2026-09-04: the scanner looped on
    a UTF-8 BOM (any non-ASCII character that cannot start a name produced
    a zero-length token), now Unicode whitespace is trivia and other such
    characters are one-character error tokens; a `#!` shebang line is
    trivia.
  - [x] Class `static` blocks (`ClassStaticBlockDeclaration` over a
    `Block`, await/yield context cleared).
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
    dropped today).
- [~] Contexts: `await`/`yield` flags, ambient (`declare`), strict-mode
  reserved words, `in` operator disallowed in for-init.
  - [x] `await`/`yield` follow the enclosing function's `async`/`*` flags
    (functions, methods, object literal methods); `in` is banned in a `for`
    head and re-allowed inside brackets, arguments and blocks
    (`detail::FlagScope`).
  - [~] Ambient bodies, strict-mode reserved words as names:
    `isBindingIdentifier` admits `let`, `yield`, `abstract`, `public`,
    `private`, `protected` as binding, declaration and expression names;
    `await`/`yield` outside their contexts are still expressions when a
    name, keyword or literal follows on the same line (tsgo's rule).
- [~] Speculation: arrow vs parenthesized expr (done), generic call vs
  comparison, type-assertion vs JSX in `.ts` vs `.tsx` (basic `<T>expr` done).
  - [x] In `.tsx`, `<` starts JSX unless `<T,>` or `<T extends U>` (not
    followed by `=`, `>` or `/`) makes it a generic arrow
    (`isJsxGenericArrowHead`); no type assertions in `.tsx`.
- [~] ASI rules (restricted productions: `return`, `throw`, `break`,
  `continue`, postfix `++/--`, arrow `=>`, `yield`, `async`) — done for the
  productions implemented.
  - [x] `do … while (x)` takes ASI after `)` without a line break.
  - [x] Statements ended by ASI carry `FLAG_ASI`, so rules can tell a
    written `;` from an inserted one from a missing one (diagnostic).
- [~] JSX (`.jsx`/`.tsx`), JS mode (`.js` incl. JSDoc *ranges* only, no
  JSDoc parsing yet).
  - [x] JSX (parser/jsx.cc, 2026-09-05): elements, self-closing elements,
    fragments, namespaced and member tag names, type arguments on tags,
    attributes (string, `{expr}`, element, spread), children (text,
    `{expr}`, `{...expr}`, nested). Shapes follow tsgo: `JsxAttributes`
    always present, whitespace-only `JsxText` kept, `JsxNamespacedName`,
    `JsxOpeningFragment`/`JsxClosingFragment`. The parser drives scanner
    modes itself (`scanNext`): `SingleGreaterThan` inside tags, `JsxText`
    in children; attribute strings are rescanned to allow newlines
    (`rescanJsxAttributeString`); `}`/`>` in text are text with an error.
    `parse-diff --jsx`: 335/350 `.tsx` files match, every miss is a
    deliberately invalid input.
  - [ ] JS mode.
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
- [x] Flat per-file arena; `uint32` node ids; `{kind, flags, parent,
  first_child, child_count, first_token, token_count}`; shared child-id
  vector; token and trivia arrays (see docs/STRATEGY.md "AST").
- [x] Debug dump (S-expression) for tests and diffing; `--spans` prints
  `Kind@start-end` without token text for machine diffing, `--batch <list>`
  dumps many files in one process.

### 3.4 Differential harness
- [x] `node make.ts parse-diff [--corpus <path>] [--limit N] [--filter s]
  [--top N] [--show N] [--spans] [--raw] [--jsx] [--no-build] [--no-report]`
  (tools/make/parse-diff.ts): corpus is
  `C:/dev/TypeScript/tsc/testdata/tests/cases` (12484 `.ts`, 350 `.tsx`)
  plus `source/tests/ts_sources`; `FASTLINT_TYPESCRIPT_REPO` overrides the
  checkout. Full run is about 10 s.
  - [x] tsgo side: tools/parse-diff/tsgo-dump.go, built with
    `go build -overlay` as a virtual `cmd/lintrix-dump` inside the tsc
    module so it can import `internal/parser` without touching that
    checkout; binary cached at `.cache/parse-diff/tsgo-dump.exe`. Reads
    paths on stdin, prints `(Kind start end` per node via `ForEachChild`
    with `SkipTrivia` starts.
  - [x] Our side: `lintrix dump-tree --spans --batch <list>`; both dumps
    stream through tools/parse-diff/sexp.ts.
  - [x] tools/parse-diff/compare.ts reports one first mismatch per file,
    bucketed by `Parent > Expected != Actual` signature; full list in
    `.cache/parse-diff/mismatches.txt`.
- [x] Normalization layer (tools/parse-diff/normalize.ts) for the
  intentional differences: tsgo token/keyword children and list wrappers
  dropped; our `TypeParameters`/`TypeArguments`/argument-run wrappers,
  `ExportDeclaration` around exported declarations, interface
  `TypeLiteral` bodies and `ExpressionWithTypeArguments` under calls
  spliced; flat `QualifiedName` and `module A.B.C` nested; missing bodies,
  `ErrorNode`s and `for (;;)` holes dropped; `implements` and interface
  `extends` entries turned into `TypeReference` the way tsgo does.
- [~] Track pass-rate in `docs/parser-conformance.md`; grind to ~100%.
  2026-09-05: 11766/12527 (93.9%), up from 15% before the shape fixes
  above. The tsgo dumper reports its diagnostic count per file, so the
  summary separates mismatches on files tsgo parses cleanly (279) from
  deliberately invalid inputs where only error recovery differs (482).
  Fixed in the second pass: `import("m")` types as
    `ImportType(LiteralType(StringLiteral), qualifier, TypeArguments)`;
    `TemplateLiteralTypeSpan` per `${T}`; `ImportAttribute` entries and
    attributes on `export … from`; `declare module "*.x" with { … }`;
    static blocks; interface `get`/`set` accessors; `[x?: T]` index
    signatures; `...name: T` as a rest-flagged `NamedTupleMember`; nested
    `module "x" {}`; the `import(…)` callee as an `ImportKeyword` node;
    NEL (U+0085) as whitespace; bigint enum member names; `await` in
    parameter defaults following the function's own context.
  Remaining, all small:
  - [ ] Clean-file long tail (279 files, no bucket above 30): `let` as a
    for-of binding, `declare export`, `@dec default class`, JSDoc types
    in `.ts` (`foo<?string>`), object literal members with modifiers,
    duplicate modifiers. Inspect with
    `grep -v "invalid input" .cache/parse-diff/mismatches.txt`.
  - [ ] JS-mode files (`// @filename: x.js` sections) and JSON sections:
    `export =` in JS, `require` calls, JSDoc-only constructs.
  - [ ] Error-recovery shape differences in deliberately invalid tests
    (tagged `[invalid input]` in mismatches.txt). Not a goal to match
    exactly; count them but do not chase.
- [x] Perf benchmark: `node make.ts bench` runs `lintrix bench` (release
  preset, files read up front, best of `--repeat`), stores JSON under
  .cache/bench/, `--save <name>` / `--compare <name>` for baselines.
  2026-09-05, tsgo test corpus (12874 files, 8.3 MB): 28 MB/s. The corpus
  is small files (650 bytes on average) so per-file setup dominates;
  C:/dev/visualnovel incl. node_modules (11183 files, 82 MB, larger
  `.d.ts` files) runs at 56 MB/s.
  - [ ] Profile and speed up: per-file arena allocation, keyword lookup,
    speculation rollbacks. Target 100+ MB/s.

---

## 4. AST design (reviewed)

Goal: the rule-facing tree, lowered from the grammar tree. **Design reviewed
before implementation.** Deliverable: `docs/ast-design.md` (signed off
2026-09-06) + the implementation below.

### 4.1 Design doc
- [x] Node kind taxonomy: typescript-eslint kinds with listed divergences
  (no `ChainExpression`, no `TSTypeAnnotation` wrapper, one
  `TSKeywordType`, no `ParenthesizedExpression`, one `Literal` kind,
  `Error`/`Missing` handling, shared `FunctionLike` layout).
- [x] Generic surface: `kind()`, `parent()`, `children()`, `ancestors()`,
  `descendants(kind)`, `span()`, `tokens()`, `leadingComments()`,
  `trailingComments()`, `source()`.
- [x] Typed views: value wrappers over the fixed child layout from
  `nodes.def`; optional children are `nullptr`; at most one list per node,
  always the tail, exposed as `span<Node *>`.
- [x] Iteration & querying: preorder-vector dispatch by kind, template
  `match`.
- [x] Identifier/scope layer: binder v1 producing scopes, declarations,
  references as a separate pass over the AST (needed by `no-unused-vars`,
  `no-shadow`, `prefer-const`).
- [x] Mutation & fixers: pooled mutable `Node` with a dirty flag,
  `replace/insertBefore/insertAfter/remove(CommentPolicy)/set`, builders,
  templates with `$placeholder`. Printer contract.
- [x] Ownership/lifetime: one `AstFile` per source file owning the pool,
  released as a unit; rules hold `Node *` for one rule pass only.
- [x] Interop hooks for task 7: `kind`, `flags`, `parent`, `child(i)`,
  `childCount`, `span`, `text` plus generated name tables; C ABI via
  `ast/access.h`.
- [x] Anti-goals: no per-node heap allocation outside the pool, no virtual
  dispatch, no node-class hierarchy, no formatter.
- [x] **Review checkpoint with Joe.** Signed off 2026-09-06. Decisions:
  `Identifier` keeps `typeAnnotation`; JSX kinds present, lowering deferred;
  `dirty` stays a flag.

### 4.2 Implementation
- [x] `source/fastlint/ast/nodes.def` + `tools/gen-ast.ts` generating the
  C++ views, kind names, child-name tables and the layout tables
  (2026-09-06; `node make.ts gen-ast`, `--check` runs in `make.ts check`).
  Later the C header and TS views for task 7.
  - [x] Dump format on top of the tables (`ast/dump.cc`, 2026-09-06).
    - [x] `lintrix dump-ast [--errors] [--bindings] <file>` subcommand
      (2026-09-06).
- [x] `Node`, `AstFile`, `util::Pool<Node, 256>`, `GrammarRef` (2026-09-06).
- [x] Lowering pass from the grammar tree (`ast/lower.cc`, 2026-09-06):
  every non-JSX grammar kind, `Error`/`Missing` handling, spans as the
  union of own tokens and children, `Incomplete` on parents of missing
  required slots. Fixture snapshots in tests/fixtures/ast/ and an
  invariant check over the parser corpus.
  - [x] JSX lowering (2026-09-06): elements, self-closing elements,
    fragments, member and namespaced names, attributes, spreads, expression
    containers, empty expressions, text. Fixture tests/fixtures/ast/jsx.tsx.
  - [x] An `ErrorNode` before the `=` of a declarator, parameter or property
    lowers into the type slot (2026-09-06).
- [x] Generic API + views, including the `FunctionLike` union view
  (generated; `is<T>`/`as<T>` on `Node`, 2026-09-06).
  - [x] Convenience predicates on `Node` (`isIdentifier("x")`,
    `isStringLiteral("x")`, `enclosingFunction()`, `enclosingStatement()`,
    `enclosing<T>()`, `ancestors()`, `descendants<T>()`, `firstChild<T>()`;
    2026-09-06). Backed by `category` lines in nodes.def, generated into
    `KindInfo::categories`.
- [x] Comment attachment rule + `Map<const Node *, CommentList>` side table
  (`ast/comments.cc`, 2026-09-06). Found and fixed a scanner bug on the
  way: speculation rewind did not restore the trivia cursor, so tokens
  after a rolled-back probe carried garbage trivia ranges; the fuzzer now
  checks trivia ranges.
- [x] Preorder vector + kind-to-rules dispatch (`AstFile::preorder()`,
  `ast/dispatch.h` `Dispatcher` with enter/exit listeners per kind;
  2026-09-06). The rule framework in 6.1 builds on `Dispatcher`.
- [x] Binder (scopes/refs) v1 (`ast/binder.cc`, 2026-09-06): scopes,
  declarations with value and type spaces, references with read, write and
  init flags, unresolved list, `dumpBindings` snapshot over
  tests/fixtures/binder/.
  - [x] `namespace A.B.C` nests a scope per segment (2026-09-06); the inner
    scopes are keyed by the segment identifiers.
  - [ ] `declare global` bodies declare into the module scope; give them a
    global scope once lib globals exist.
- [x] Fixer API (`ast/fixer.h`, 2026-09-06): `replace`, `insertBefore/After`,
  `append`, `remove(CommentPolicy)`, `set`, builders, dirty propagation,
  `applyFixes` skipping fixes whose target is dirty or detached.
- [x] Templates (`ast/template.h`, 2026-09-06): compiler with `Auto`,
  `Expression`, `Statement`, `Statements` and `Type` modes, per-mode-and-text
  cache, slot classification per placeholder, `$name$` splices, `$$`
  escapes, `instantiate` with slot checks, detach-and-reparent, clone for
  repeated names and precedence parens via `ast/precedence.h`, `match` with
  splice spans and structural equality for repeated names.
  - [x] `Template::compile(text, mode, jsx)` parses JSX templates
    (2026-09-06); placeholders sit in expression containers.
  - [ ] A placeholder in a shorthand property or a for-in head is typed
    loosely (`Name`, `Any`); tighten once rules need it.
- [x] Printer (`ast/printer.cc`, 2026-09-06): verbatim for clean, captured
  layout + current children for dirty, per-kind templates for synthesized,
  list separator rules, style sniffing (semicolons, quotes, indent, line
  ending), span recomputation behind `PrintOptions::updateSpans`. Round-trip
  tests over the fixtures and the corpus, clean and all-dirty.
  - [x] Kind templates cover the JSX kinds (2026-09-06); attributes separate
    with a space, children with nothing.
  - [x] Precedence-aware parenthesization lives in `ast/precedence.h`
    (`needsParens`), used by template instantiate and the `Fixer` builders;
    the printer only honours the `parenthesized` flag.
- [x] Fixpoint driver (`ast/fixpoint.h`, 2026-09-06): parse, lower, bind,
  run the pass callback, apply, print, repeat; stops on no fixes, unchanged
  text or `maxPasses`; reverts a pass whose output fails to parse; one
  fix-free pass for files with syntax errors.
- [x] Tests (2026-09-06): round-trip (parse → lower → print == source for
  every fixture and corpus file, clean and all-dirty), fixer unit tests with
  comment-preservation cases, template instantiate/match cases
  (`ast_template_test.cc`), fixpoint driver cases (`ast_fixpoint_test.cc`),
  binder snapshot tests (tests/fixtures/binder/).

---

## 5. Type cache system

Goal: type facts from tsgo, cached in SQLite, bounded memory. Depends on 2.x
decisions.

### 5.1 tsgo client (C++)

Landed 2026-09-06 under `source/fastlint/tsgo/`; docs/tsgo-client.md describes
the pieces. Tests: `tsgo_json_test`, `tsgo_msgpack_test`,
`tsgo_source_file_test` (fast) and `tsgo_client_test` (`[integration]`, against
`tests/fixtures/projects/basic`).

- [x] Process management: spawn `tsc --api --cwd=<dir>` over stdio pipes
  (`process.h`, Win32 and POSIX), one `Client` per server, connection marked
  broken on EOF or a malformed frame, `stop()` waits then kills.
  - [ ] Restart after a crash and the per-run concurrency cap belong to the
    driver that runs several projects (5.2 / the CLI).
- [x] Wire protocol: the msgpack 3-tuple envelope (`msgpack.h`, encode/decode
  with partial-frame detection) plus JSON payloads (`json.h`, own parser and
  writer). One request in flight at a time; FS callbacks are answered while
  waiting. No `batchRequests` in 7.0.2: `typesAtPositions` and
  `typesAtLocations` wrap the plural endpoints.
- [x] Version gate: `tsc --version` is parsed before the server starts and
  compared with `kSupportedVersions` (7.0.2); `skipVersionCheck` exists for
  probing. The parameter-name table is `generated/compat.h`, written by
  `node tools/spikes/tsgo-api/main.ts compat --emit`.
  - [x] Recorded as `tsgo_version` in the store's `meta` table (5.3); a
    mismatch rebuilds the cache.
- [x] Snapshot/program management: `initialize`, `updateSnapshot` with
  `SnapshotUpdate` (open/close projects and files, file changes,
  `invalidateAll`), `openProject`, `getDefaultProjectForFile`, `release`.
  `changedFiles` is flattened from the response's `changes`.
- [x] `getSourceFile` decoder (`EncodedSourceFile`, 44-byte header + 28-byte
  nodes, node-list pseudo-entries skipped) and `NodeIndexTable`, which pairs
  our nodes with tsgo indices by span: same `end`, the largest tsgo `pos` not
  past our `start`, and the k-th of a same-span run. Error and zero-width
  nodes stay unmapped.
  - [x] Measured with `lintrix cache-bench --unmapped` on visualnovel (541
    files, 378k expression nodes): 80% unmapped at first, because tsgo spans
    are UTF-16 code units over BOM-stripped text while ours are UTF-8 bytes.
    `Utf16Offsets` converts; 0.6% remain (`constructor`/`new` keyword
    identifiers, parameter identifiers whose ESTree span includes the type
    annotation), none of which tsgo has a node for.
- [x] Serve file contents over `--callbacks=readFile,fileExists` through a
  `FileProvider`; `null` defers to the disk.
- [x] Typed request wrappers (`queries.h`: `Session`, `TypeResponse`,
  `SymbolResponse`, `SignatureResponse`) for the query set from 2.2, plus
  `getTypeArguments`, `getTypeOfSymbol` and `getSymbolAtLocation`.
- [x] `node make.ts gen-tsgo-enums` writes `generated/enums.h` (`TypeFlags`,
  `ObjectFlags`, `SymbolFlags`, `ElementFlags`, `SignatureFlags`,
  `SyntaxKind`) from the installed typescript package's `dist/enums/`.

### 5.2 Type facts layer

Landed 2026-09-06 under `source/fastlint/types/`; docs/type-facts.md describes
it. Tests: `types_graph_test` (fast) and `types_facts_test` (`[integration]`).

- [x] `TypeFacts` interface rules call: `typeOf(node)`, `prefetch(nodes)`,
  `isNullable`, `isAnyLike`, `isPromiseLike`, `isArrayLike`, `unionMembers`,
  `callSignatures` (each with its interned return type and parameter
  symbols), `assignableTo`, `symbolOf`, `declarationsOf`,
  `declarationFile(handle)`.
- [x] Lazy fetch; per-file working set; flush on file completion.
  `beginFile` selects the file, the node side table is fetched on the first
  query, `typeOf` caches per node, `prefetch` issues one
  `getTypeAtLocations` for a batch, `endFile` drops the working set.
- [x] Interning: `TypeGraph` rows keyed by a 64-bit structural hash over the
  row's flags, text, symbol and alias identities and one hop of children
  (union/intersection members, type arguments). Symbol identity is name,
  flags and declaration handles. `types`, `type_children`, `symbols` and
  `strings` live in `Vector`s with a hash `Map` per table; live tsgo ids are
  tracked per row and dropped with `clearSessionIds()`.
  - [ ] LRU over type rows once 5.3 can reload them from SQLite; until then
    the graph is the whole working set and only grows.
  - [ ] Children interned as members are shallow (no hop of their own). A
    union nested in a union hashes by its flags alone; decide whether to
    deepen on demand or accept the collision risk after measuring.

### 5.3 SQLite store
- [x] Vendor sqlite amalgamation via `make.ts deps`; WAL; single writer
  thread; batched commits. (docs/type-cache.md)
  - [x] `deps` downloads `sqlite-amalgamation-3530400.zip` pinned by sha3-256
    into `vendor/sqlite` (gitignored) through tools/make/lib/zip.ts; the
    root CMake builds it as the static `sqlite3` C target and fails
    configure with the fetch command when it is missing.
  - [x] `Store` (source/fastlint/cache/store.h) is one connection in WAL
    mode; callers batch a file's writes in `begin`/`commit`. The writer
    thread itself belongs to the driver (task 6).
- [x] Schema: `files(path, content_hash, closure_hash, tsconfig_hash)`,
  `types`, `type_children`, `symbols`, `symbol_declarations`,
  `node_types(file_hash, start, end, kind, type_hash)`,
  `rule_results(file_hash, closure_hash, rule, payload)`,
  `meta(schema_version, tsgo_version, lib_hash)`.
  - [x] Graph rows are keyed by the `TypeGraph` structural hash and stored
    with a `seq`; `loadGraph` re-interns in that order and checks every hash
    reproduces, `saveGraph` writes from a `GraphCursor` onward.
- [x] Migration/versioning; drop-and-rebuild on schema or tsgo version
  change (`kSchemaVersion`, `tsgo_version`, `lib_hash`; `rebuilt()` reports
  it).
- [~] `--no-cache`, `--cache-dir`, `cache verify` (recompute a sample and
  compare).
  - [x] `Store::verify`: `integrity_check` plus dangling child, symbol and
    node-type references.
  - [ ] The CLI flags and the tsgo recompute sample land with the driver in
    task 6.

### 5.4 Invalidation
- [x] Import graph from parser → closure hash per file. (docs/type-cache.md
  "Invalidation")
  - [x] `collectImports` (cache/imports.h) over the AST: import/export
    declarations, `import =` require, dynamic `import()`, `require()` and
    `import()` types.
  - [x] `resolveImport` for relative specifiers with bundler-style extension
    probing and `.js` → `.ts` rewrites; bare specifiers stay unresolved and
    hash by name.
  - [x] `ImportGraph` with order-independent closure hashes and
    `loadClosure` to parse a file's whole closure through a `FileSystem`.
  - [ ] tsconfig `paths`/`baseUrl` aliases and triple-slash references are
    not resolved yet; they hash by name like packages.
- [x] v1 file-closure invalidation: `FileCache::lookup` compares content,
  closure and tsconfig hashes with the stored record and drops a stale
  file's node types and rule results.
- [x] Rule-result replay for unchanged (file, closure):
  `FileCache::ruleResult`/`saveRuleResult` over `rule_results`.
  - [x] Wired `FileCache` into the `lint` command (6.1); the environment hash
    folds in the lintrix version, config, every resolved tsconfig and the
    lockfile so package upgrades invalidate.
- [ ] v2 per-type provenance (decl file hashes per type row) — after v1 is
  measured on a real monorepo.

### 5.5 Measurement
- [x] Cold vs warm run timings on a real project; memory high-water mark;
  cache size on disk. `lintrix cache-bench` (docs/type-cache.md
  "Measurement"); visualnovel, 541 files, release: cold 11.5 s, warm 1.3 s,
  peak working set 97 MB, database 31 MB, 377k node types.
  - [x] Found and fixed three quadratic litestl string appends (file read,
    JSON writer, JSON parser); closure 25 s → 0.1 s, fetch 58 s → 1.2 s on
    the subsets measured.
  - [ ] Batch the per-symbol `getSymbolOfType` calls (23.7k of 33.3k rpc
    calls) through `batchRequests` or defer symbol interning.
  - [ ] Store writes: 2.8 s per 377k node types in per-file transactions;
    batch files per transaction or cache prepared statements.
  - [ ] Working-set eviction (the 5.2 LRU item) before a monorepo-sized
    graph.

---

## 6. Lint rules

Goal: enough rules to lint a real project; rule API proven for task 7.

### 6.1 Rule framework (docs/rules.md)
- [x] Rule interface: metadata (name, docs URL, fixable, type-aware flag),
  `create(ctx)` registering kind-indexed callbacks; `ctx.report(node, msg,
  fix?)`. `RuleDef`/`RuleContext` in lint/rule.h; messages by id with
  `{{placeholders}}`; per-file `state<T>()`; `option(i)` for config options.
  - [x] Option schema validation: `RuleMeta::schema` is an ESLint-shaped JSON
    Schema (array per positional option) validated at config load
    (`validateOptions`, lint/option_schema.cc); a bad option is a config error.
    Keywords: `type`, `enum`, `properties`, `additionalProperties`, `required`,
    `items`, `minItems`/`maxItems`, `oneOf`/`anyOf`. Every optioned built-in
    declares one. Data, not C++, so task-7 plugins reuse it.
  - [x] Suggestions carry an applicable edit: the JSON output gives each
    suggestion a `fix: {range, text}` in ESLint's shape (`SuggestionResult`
    fix fields), computed like a diagnostic's `fix` by applying the suggestion
    alone and diffing, and cached with the result. `--fix` still never applies
    a suggestion; an editor applies the range and text.
- [x] Dispatch: single tree walk, per-kind callback lists — no per-rule
  traversal. One `ast::Dispatcher` over every enabled rule's listeners.
- [x] Config: `lintrix.config.json`; severity; per-rule options;
  overrides by glob; `extends` presets (`lintrix:recommended`, `lintrix:all`);
  `ignores`; `--rule name:severity` on the command line.
  - [ ] `lintrix.config.ts` (needs Node to evaluate; with task 7).
  - [x] Glob matching ignores case on Windows (case-sensitive elsewhere), via a
    `caseInsensitive` flag on `globMatch` that `Config::resolve` sets from the
    platform; the path is already normalized to forward slashes before matching.
- [x] Disable directives: `// lintrix-disable[-next-line] rule`, and
  `// eslint-disable*` compatibility (decided: accepted as aliases, see
  docs/STRATEGY.md). Unused directives reported (`reportUnusedDisableDirectives`).
- [x] Output: pretty terminal (stylish-shaped), `--format json` (ESLint-shaped).
  - [x] SARIF: `--format sarif` writes a SARIF 2.1.0 log (`formatSarif`),
    driver `rules` list plus `results` with `ruleIndex`, `level` and a
    1-based `physicalLocation` region.
  - [x] JSON `fix` ranges: a fixable message carries `fix: {range, text}` in
    ESLint's shape. Each fix is applied alone to the original and the reprint is
    diffed to a UTF-16 `[start, end]` span and replacement text
    (`LintOptions::fixEdits`, computed only for `--format json`).
  - [x] Columns count UTF-16 code units, as editor protocols and ESLint's JSON
    do (one per byte for ASCII, so ASCII output is unchanged; one per basic-plane
    code point, two for an astral one).
- [x] Rule test harness: `valid`/`invalid` cases with expected messages and
  fixer output (`testing/rule_tester.h`, `runRuleTests`).
- [x] `lintrix lint` command: config discovery, `--fix`, `--format`,
  `--quiet`, `--max-warnings`; exit codes as ESLint.
  - [x] Wired `FileCache`, `--no-cache`, `--cache-dir` and `cache verify` into
    the command. A file replays its diagnostics from a SQLite store when its
    content, closure and environment hashes match; one JSON payload per file
    (lint/result_cache.cc), keyed so JSON fix ranges cache separately. On by
    default at `node_modules/.cache/lintrix/lint.db`; `--fix` and untyped
    files are not cached.
  - [x] Type server start-up for type-aware rules: one `tsgo` server through
    `types::ProjectTypes`, which serves the linter's text to the server so
    fixpoint passes are typed too (docs/type-facts.md "Type sources").
    `--type-stats` prints the query counts.
  - [x] Per-file tsconfig discovery. `ProjectTypes::open` takes the list of
    tsconfigs the run resolved and opens them in one snapshot; `setFileProject`
    routes each file to its own, and `beginFile` binds that project (a file
    with no route is syntactic-only, not degraded). The CLI resolves a file's
    tsconfig in order: `--project` for every file, else the config's
    `projects`/`project` glob mapping, else the nearest `tsconfig.json` walking
    up from the file, else a `tsconfig.json` beside the config. The environment
    hash folds in every resolved tsconfig. `--project` failure is still an
    error; discovery failure disables the type-aware rules with a stderr note.
  - [x] A file with syntax errors reports only the earliest one as a single
    fatal diagnostic and runs no rules, matching ESLint (whose parser throws on
    the first error). We recover past it to keep parsing but report just the
    first. Running rules on a recovered tree stays off until a use case asks.
- [x] First rule end to end: `no-debugger` (fixable).

### 6.2 Syntactic rules (initial set)
- [x] Batch A (syntax only): `no-debugger`, `no-console`, `eqeqeq`, `no-var`,
  `no-empty`, `no-unreachable`, `no-duplicate-case`, `no-fallthrough`,
  `no-constant-condition`, `no-self-assign`, `curly`,
  `no-non-null-assertion`, `prefer-as-const`, `array-type`. Each with a docs
  page under docs/rules/, ported upstream tests, and a fixer where upstream
  has one (`eqeqeq`, `no-var`, `curly`, `prefer-as-const`, `array-type`).
  - [x] `no-unreachable` / `no-fallthrough` decide reachability with a
    structural completion analysis (rules/flow.h `completesNormally` /
    `sequenceCompletesNormally`), replacing the old statement-list `alwaysExits`
    approximation. It sees `switch` exhaustiveness (a `default` with every path
    exiting), matches `break`/`continue` to the loop or switch they target, and
    follows labelled jumps, so a `break` aimed at an inner loop no longer keeps
    a `while (true)` from exiting.
  - [x] `no-empty`: upstream's "insert a comment" suggestion. An empty block
    statement carries a `suggestComment` suggestion that inserts `/* empty */`
    between the braces, built on a new `Fixer::addComment` primitive for
    synthesized comments (a `Comment` whose text lives in the file arena, not
    the source; printed by `bracedStatements` between bare braces). An empty
    `switch` is still reported without a suggestion, as upstream does.
  - [x] `no-fallthrough`: `reportUnusedFallthroughComment` reports a
    fallthrough comment on a non-empty case that exits (the report lands on the
    comment); reachability is the `completesNormally` analysis in rules/flow.h.
  - [x] `lint::Regex` `\uXXXX`/`\u{...}`/`\xXX` escapes above ASCII match the
    UTF-8 bytes of their code point (held in a group so a quantifier spans the
    whole character); the same escape inside a character class still fails, as
    do Unicode property classes. Fixed a range-endpoint escape that read its
    hex from the wrong position.
- [x] Batch B (binder-heavy): `prefer-const` (fixable), `no-unused-vars`,
  `no-shadow`.
  - [x] `prefer-const`: ESLint port over the binder; the fix flips the
    declaration's kind byte. No `/* exported */` directive or
    `markVariableAsUsed`, so names other rules mark as used are reported.
  - [x] `no-shadow`: typescript-eslint's extension ported. Globals come from
    a fixed ECMAScript builtin list since there is no `globals` config;
    a function expression's own name is treated as one scope out, which
    matches scope-manager's function-expression-name scope.
  - [x] `no-unused-vars`: typescript-eslint's extension ported; import
    removal is a suggestion, or a fix with `enableAutofixRemoval.imports`.
    Needed a decorated plain parameter to get a `TSParameterProperty`
    wrapper so its decorators bind (in the enclosing scope), `infer I` to
    declare in its conditional type's scope, `export =` to accept either
    space, `typeof` to resolve type-only imports, and the printer to reprint
    an import from the template when its specifier mix changes.
- [x] Batch C: `@typescript-eslint/consistent-type-imports` (fixable).
  Type-only use comes from the binder's reference spaces and the `TypeQuery`
  flag; fixes flip the import kind byte, move specifiers between imports
  and insert new imports before the reported one. The printer now keeps
  children's comments when a node reprints from its template. Not done: the
  decorator-metadata caveat (needs tsconfig) and a `jsxPragma` option.
- [x] Fixer additions the batch needed: `setData`/`setFlag` (operator and
  keyword changes in place), slot-owned parentheses on `detach`/placement,
  loop-head declarations print without `;`.
- [x] Scanner: line starts no longer duplicate across speculative rescans
  (`throw err` + newline shifted every later line number by one).

### 6.3 Type-aware rules (initial set)
- [x] `no-floating-promises`, `await-thenable`, `no-misused-promises`,
  `no-unnecessary-condition`, `no-unsafe-*` family (`any` flow),
  `restrict-template-expressions`, `strict-boolean-expressions`,
  `prefer-nullish-coalescing`, `no-unnecessary-type-assertion` (fixable). Each is
  ported to its high-value core with the deferred parts documented per rule in
  the sub-items below and in `docs/rules/<name>.md`.
  - [x] Typed rule tester: `runTypedRuleTests` over
    tests/fixtures/projects/basic (`[integration]`); each case is served
    to the server as `src/case.ts`.
  - [x] `TypeFacts` grew the questions the promise rules ask: apparent
    type, type-parameter constraint, property type, `number` index type,
    well-known symbol properties, callability, `isThenable(type, n)`,
    `isBuiltin(type, name)` with base-type walking, `isTuple` via the
    reference target (tsc 7.0.2 omits `isTupleType`), and `typeArguments`.
    Children are now interned with their own children to a depth limit,
    since shallow generic references collided (`Array<number>` and
    `Array<Promise<number>>` were one row).
  - [x] `no-floating-promises` (suggestions only, as upstream). Not done:
    `allowForKnownSafePromises`/`allowForKnownSafeCalls` (type-or-value
    specifiers).
  - [x] `await-thenable`. The `await using` cases need the `esnext`
    disposable lib, which the fixture project does not enable, so they are
    untested.
  - [x] `no-unsafe-*` family: `no-unsafe-call`, `no-unsafe-member-access`,
    `no-unsafe-argument`, `no-unsafe-assignment`, `no-unsafe-return`. Shared
    helpers live in rules/unsafe.{h,cc} (the `this` walk, union parts, the
    any-array tests, `isUnsafeAssignment` over generic arguments,
    `discriminateAny`, and a `Texts` intern pool for report placeholders).
    `TypeFacts` grew `contextualTypeOf`, `resolvedSignature`,
    `constructSignatures`, `targetOf`, `typeToString`, `awaitedType`,
    `isAny`/`isUnknown`/`isErrorType`, and `strictOption` reading the
    compiler options that `TypeSource` fills from `parseConfigFile`.
    `callSignatures` now gathers over a union's callable members. A binding
    identifier has no type of its own from the server, so the receiver's
    annotated type is read as the initializer's contextual type. A parameter
    property's assignment-pattern span was narrowed past the modifier to
    match ESTree.
  - [x] Typed rule tester gained a `project` argument; a `loose` fixture
    adds `noImplicitThis: false`. A JSX case names `casex.tsx`, since tsc
    drops a `.tsx` that shares a stem with a `.ts` sibling.
  - [x] `restrict-template-expressions`. Every `allow<Kind>` flag and the
    union/intersection walk are ported; the `allow` list supports library
    specifiers via `isBuiltin`. Not done: `file`/`package` type-or-value
    specifiers, the same gap no-floating-promises leaves.
  - [x] `prefer-nullish-coalescing`, logical-or forms only. `||` and `||=`
    with a nullable left side report `preferNullishOverOr` at the operator
    with a `suggestNullish` suggestion; the `ignoreConditionalTests`,
    `ignoreMixedLogicalExpressions`, `ignoreBooleanCoercion` and
    `ignorePrimitives` options and the `strictNullChecks` gate are ported.
    Not done: the ternary (`preferNullishOverTernary`) and if-statement
    (`preferNullishOverAssignment`) rewrites and their options.
  - [x] `no-unnecessary-type-assertion`, non-null (`!`) form only. A `!` whose
    operand is already non-nullable reports `unnecessaryAssertion`; an
    assignment target (`x! = y`) or a nullable operand the context accepts
    reports `contextuallyUnnecessary`; each is fixed by dropping the `!`. The
    `strictNullChecks` gate and the used-before-assignment guard (an
    uninitialized, non-definite variable is left alone) are ported. Not done:
    the `as`/`<T>` cast forms, which need whole-type structural comparison, and
    their `checkLiteralConstAssertions` and `typesToIgnore` options.
  - [x] `strict-boolean-expressions`, condition detection and reporting. The
    `if`/`while`/`for`/`do`/ternary tests, the `!` argument and the `&&`/`||`
    operands are classified into variant kinds (`inspectVariants`) and matched
    against the `determineReport` table; all `conditionError*` messages, the
    `allow*` options and the `strictNullChecks` gate are ported. Added the
    `TypeFacts::literalText` question for truthy-literal detection. Not done:
    the suggestion fixes, the array-method-predicate path
    (`predicateCannotBeAsync`, `explicitBooleanReturnType`) and the
    truthiness-assertion argument path.
  - [x] `no-misused-promises`, conditional/spread/void-return checks. A promise
    in a boolean context (`conditional`), a thenable spread (`spread`), an async
    argument where a void-returning function is expected (`voidReturnArgument`)
    and one assigned to a void-returning variable (`voidReturnVariable`) are
    reported; the `checksConditionals` (`none`/`all` flag-unions),
    `checksSpreads` and `checksVoidReturn` (`arguments`, `variables`) options are
    ported. Not done: the property/return-value/JSX-attribute/inherited-method
    void-return checks (they need contextual property typing and a heritage
    walk), the array-predicate `predicate` check, the `strict` flag-unions mode,
    the `using`/dispose cases and rest-parameter void spreading.
  - [x] `no-unnecessary-condition`, truthiness and nullish checks. A condition
    that is always truthy/falsy/`never` (`alwaysTruthy`, `alwaysFalsy`, `never`)
    and a `??`/`??=` left that is never or always nullish (`neverNullish`,
    `alwaysNullish`) are reported over the if/while/for/do/ternary tests, the
    logical operands and the `!` argument; the array-index exemption, the
    `allowConstantLoopConditions` option and the `strictNullChecks` gate are
    ported. Fixed a tsgo decode bug: a boolean literal's value arrives as a JSON
    boolean, which was dropped, leaving `literalText` empty for `true`/`false`.
    Not done: the literal-comparison checks (`comparisonBetweenLiteralTypes`,
    `noOverlapBooleanExpression`), the optional-chain check
    (`neverOptionalChain`), the array-predicate callback checks and the
    type-predicate check.
  - [x] A typed rule's suggestions carry a `fix: {range, text}` in the JSON
    output the same as a syntactic rule's, since the edit is computed from the
    already-typed reprint (the 6.1 suggestion-application item). The rule tester
    still does not assert on suggestions.
- [x] Each rule's type queries logged so the cache working set is measured.
  - [x] `--type-stats` prints the totals of a run.
  - [x] Per-rule attribution: the dispatcher carries an owner token per
    listener and fires a scope hook around each, so the linter charges the gain
    in the shared type stats to the running rule. `--type-stats` lists the
    fetches each rule drove, busiest first; the per-rule totals sum to the run
    totals. Zero cost when the flag is off (no hook installed).

### 6.4 Dogfood
- [x] Lint `C:/dev/TypeScript/packages/typescript/src` and our own `tools/`;
  compare against typescript-eslint output; triage diffs.
  - `tools/` (2026-09-07): 20 non-strict-boolean findings, all true positives.
    The unsafe-* cluster is a `msg: any` RPC spike; `no-unnecessary-condition`
    fires where a lib type is non-nullable (`argv._`, `JSON.stringify`);
    `no-unnecessary-type-assertion` flags a `child.stderr!` the `spawn` stdio
    tuple already types non-null.
  - `C:/dev/TypeScript/packages/typescript/src` (60 files): 3587 findings,
    unsafe-* dominating an `any`-heavy protocol package; no crash.
  - Head-to-head against typescript-eslint 8.69 on a webgl file: identical
    findings and identical columns after the two fixes below.
  - Fixed a crash: `awaitedDeep` / `isBuiltinDeep` / `isPromiseLike` held a
    `TypeRow &` or a `children` span across a type-server query that reallocates
    the graph, so a second type-heavy rule could dangle it. They now copy the
    fields and members out first, matching `isThenable` / `signatures`.
  - Fixed a CRLF column off-by-one: the scanner recorded a `\r\n` (and a 3-byte
    U+2028 / U+2029) line start one byte early, so every column on a CRLF file
    read one too high. All reported columns now match typescript-eslint.
- [x] Ran the 13 type-aware rules over two real projects
  (2026-09-07): `C:/dev/visualnovel` (well-typed, `strict`) and
  `C:/dev/webgl-app-framework` (widely uses `any`).
  - Precision signal: on the well-typed authoring package the unsafe-* rules
    fire zero; on the `any`-heavy webgl scripts they dominate
    (`no-unsafe-member-access` 198, `no-unsafe-assignment` 100, and the rest).
    Both match how typescript-eslint's defaults behave on that code.
  - `strict-boolean-expressions` is the loudest rule on both (not in the
    recommended preset, expected).
  - `no-unnecessary-type-assertion` findings on webgl are true positives; the
    rule distinguishes a `!` on a required property (flagged) from one on an
    optional property (left alone) on the same line.
  - Fixed a false positive: `no-unnecessary-type-assertion` reported
    `contextuallyUnnecessary` on a `!` in a property value (`{ file: m.get(k)! }`
    where `file?` is optional). The nullable-context check now runs only where
    the operand has a contextual type, matching typescript-eslint's
    `getContextualType` (call/`new` argument, annotated variable or field
    initializer, right of a plain `=`).
  - Graceful degradation confirmed: a tsgo compiler panic on a file yields
    "no types" for that file and the run continues.
  - Running a narrow rule subset with `--no-config` surfaces existing
    `eslint-disable` directives as unused; a byproduct, not a finding.

---

## 7. Plugin API (TS rules via litestl bindings)

Goal: custom rules in TypeScript, loaded as an N-API native module or as
WASM, and native rule plugins, all over the same AST (docs/ast-design.md
"Interop" and "Plugins").

### 7.1 Binding surface
- [x] The generated TS view surface, from the same `nodes.def` as the C++
  views (`gen-ast` `emitTsViews` → `plugin/generated/ts/views.ts`): the
  `NodeKind`/`Flag`/field-enum vocabularies as `as const` objects (TS `enum`
  is out under `erasableSyntaxOnly`), the `childNames` tables, a handle-based
  `Node` interface (`kind` as `type`, `flags`, `parent`, `child(i)`,
  `childCount`, `text`, `hasFlag`), one interface per kind with typed child
  and field accessors, the union aliases, and a `NodeByKind` map so `is` and
  `descendants` narrow.
- [x] Batch-friendly traversal: `descendants<K>(kind: K)` returns
  `readonly KindNode<K>[]`, so a rule does one typed query, not N hops.
- [x] `plugin/ts/example-rule.ts` exercises the surface (a typed
  `descendants` query, `is` narrowing, a required-child read) so `tsc` keeps
  it honest, and the root tsconfig now includes `source/fastlint/plugin`.
- [x] The runtime that backs the view accessors (`gen-ast` `emitTsRuntime`,
  appended to `views.ts`): a `Host` accessor interface each embedding
  implements over its own memory, a per-kind member table and a `wrap(host,
  handle)` factory that gives a handle real getters for its children, flags and
  data bytes. The N-API addon implements `Host`; the base `Node` gained a
  `range` so a rule can report a span.
- [ ] Still deferred to the runtime (need the type server or more binding):
  `TypeFacts` predicates, the fixer API, and templates/`match` through litestl
  `binding/generators/typescript`. An embedding has no tsgo process, so the
  typed rules stay host-only for now.

### 7.0 Native plugin C ABI
- [x] The stable ABI is `fastlint/plugin/abi.h` (hand-written): opaque
  `fl_node`, the `fl_host_api` accessor table, `report`/`report_fix`, template
  compilation and the fixer ops, plus the `fl_rule`/`fl_plugin` tables and
  `FL_ABI_VERSION`. `tools/gen-ast.ts` emits `fastlint/plugin/generated/ast.h`
  with `FL_NODES_DEF_HASH` and C kind/flag enums, wired into `gen-ast` and
  `check` through the one `generate()`.
  - Deferred from the original bullet: the exported `Node` layout for a direct
    read path (the accessor table is the only path today), and the binder entry
    points.
- [x] `ast/access.h` now has both implementations behind `FASTLINT_PLUGIN`:
  the host reads `Node` inline; a plugin routes each accessor through the
  `fl_host_api` table, with `plugin/plugin_node.h` supplying the litestl-free
  `Node`/`View`/`span`/`string_view` the generated views name. The views that
  include it compile byte-identically on both sides.
- [x] `fastlint_plugin_init(const fl_host_api *)` returns an `fl_plugin` rule
  table; `plugin::Plugin::load` does `LoadLibrary`/`dlopen`, checks the ABI
  version and the `nodes.def` hash, and wraps each `fl_rule` as a `RuleDef`
  (one shared `create` recovers the `fl_rule` from the `RuleDef`, since `def`
  is the wrapper's first member) for the linter's registry and its
  kind-to-rules dispatch. `hostApi()` implements the table over `access::`,
  `ast::Template` and `ast::Fixer`.
- [x] Plugin-side build of the C++ views: `source/tests/plugin_sample` is a
  shared library (CMake `MODULE`) built against the plugin headers only, no
  litestl, no host library. Its `sample/no-foo` rule reads through the
  `Identifier` view and fixes `foo` to `bar` through a template. `plugin_host`
  loads it from disk and asserts the report and the applied fix.

### 7.2 N-API build
- [x] `node make.ts build --napi [--smoke]` produces `build/napi/fastlint.node`
  (docs/embedding.md). cmake-js runs the configure step only (downloading the
  runtime headers and the Windows import library, and injecting `CMAKE_JS_*`),
  and the build is an ordinary `cmake --build` of the one target. `--runtime
  electron` targets the Electron ABI. `source/napi/addon.cc` calls the C N-API
  directly, so node-addon-api is not a dependency; `cmake-js` is the one added
  devDependency.
- [x] `fastlint/embed/lint_text.h` is the shared entry point both embeddings
  wrap: the recommended preset, no config lookup, no type-aware rules.
- [x] The rule-loading runtime (`plugin/ts/runtime.ts`): `lint(addon, source,
  filename, rules)` parses through the addon, wraps the root, walks it once and
  dispatches each node to the ESLint-shaped visitors a rule's `create(context)`
  returns, keyed by node-kind name; `context.report` collects a problem with
  its message (from `messageId` + `{{data}}`), rule id, node type and 1-based
  line/column. `plugin/ts/rules/no-debugger.ts` are two rules over it (a plain
  visitor and an `is`-narrowed member read); `runtime.smoke.ts` runs them
  through the built addon under `build --napi --smoke`.
- [x] Rule loading from a config (`plugin/ts/config.ts`): `loadConfig` imports
  a `lintrix.config.ts` (or `.js`/`.mjs`) and reads its `rules`; `defineConfig`
  type-checks the literal. A rule is just a value the config imports, so there
  is no plugin-resolution protocol beyond `import`. `plugin/ts/index.ts` is the
  package surface a host wraps the `.node` addon with; `example.config.ts` is a
  worked config. The decision recorded in this task is taken: the node side
  hosts the native core rather than the CLI hosting node. (Task 8.2 replaced
  `loadConfig` with `loadConfigFile`/`loadCompiledConfig` over the unified
  schema, and a rule now reaches a config through a `plugins` prefix.)
- [x] Threading model (`plugin/ts/driver.ts`): `lintFiles(files, {configPath,
  addonPath, concurrency})` shards files across a `worker_threads` pool, each
  worker loading the addon and config once and linting whatever file the driver
  hands it next, so the TypeScript rules run in parallel, one file per worker at
  a time; one file or `concurrency: 1` stays in-thread. `driver.smoke.ts` runs
  both paths under `build --napi --smoke`.

### 7.3 WASM build
- [x] `node make.ts deps fetch emsdk` installs the pinned SDK into
  `vendor/emsdk`, and `tools/make/lib/emsdk.ts` captures its environment as a
  delta in `.cache/emsdk.json` the way `captureVcvars` captures MSVC's.
  `make.ts env` reports the SDK and the resolved emcc.
- [x] `node make.ts build --wasm [--release] [--smoke]` produces
  `build/wasm/bin/fastlint.js` and its `.wasm` through the stock
  `Emscripten.cmake` toolchain and the `wasm`/`wasm-release` presets. litestl's
  `build_files/WASM.cmake` is not used: it re-derives the emsdk environment on
  every compile, which the captured delta makes unnecessary.
- [x] Same TS runtime over the WASM heap. `source/wasm/module.cc` exports the
  node accessors as the WASM twin of the addon's (`fl_wasm_parse`, `fl_wasm_kind`,
  `fl_wasm_child`, `fl_wasm_descendants`, ...), handles being heap pointers that
  cross to JS as numbers. `plugin/ts/wasm_addon.ts` wraps the Emscripten module
  as the same `Addon` the runtime consumes, re-reading the heap views each call
  since `ALLOW_MEMORY_GROWTH` can swap them and freeing a session through
  `Addon.freeSession` (the WASM heap has no finalizer; the addon's is a no-op).
  So `lint` runs unchanged over WASM; `wasm.smoke.ts` proves it under `build
  --wasm --smoke`. litestl's `typescriptRuntime` is not used: one accessor-based
  runtime already backs both embeddings, so a second one would only diverge.
- [x] Use case: browser/playground, and editors without native addons. The
  module is an ES module with a factory export usable in a browser, a worker or
  Node (`-sENVIRONMENT=web,worker,node`); `wasm_addon.loadWasmAddon` is the
  entry a playground calls.

### 7.4 Ecosystem
- [x] `create-lintrix-rule` template, docs, example rules. `node make.ts
  new-rule <name> [--selector <NodeKind>] [--out]` scaffolds a `Rule` module
  from the `lintrix` package surface (`tools/make/new-rule.ts`). Ported
  examples live in `plugin/ts/rules/`: `no-debugger`/`no-console`, `no-var`
  (enum field), `eqeqeq` (enum + `{{data}}`), `no-empty` (list child); they read
  the generated views only, so they run under either embedding. The writing-a-
  rule guide is in docs/embedding.md.
- [x] Perf budget documented (docs/embedding.md "Performance"), measured by
  `plugin/ts/bench.ts` against both embeddings. The bare walk is about 3 us per
  node on each, virtually all of it boundary crossings; a native rule pays a
  dispatch lookup per node instead. Five inspecting rules run about 40x the C++
  front end under N-API. The per-node handle is the cost to cut later (an
  integer index or a batched node record).

---

## 8. Unified config and the npm package

Goal: one config schema read by the native binary and by a JavaScript loader,
and a publishable `lintrix` npm package that resolves
`lintrix.config.{ts,js,json}`, drives the native binary when one is present,
and falls back to a bundled WASM build when one is not. Format unification only;
a single command that runs native and plugin rules together
(execution unification) stays a later decision.

### 8.1 Unified JSON config schema
The on-disk shape both consumers agree on. The native binary stays the JSON
reader (it has no JS engine), and the JS side compiles `.ts`/`.js` down to it.
- [x] Settle the schema on the native `lint::Config` shape, since it is the
  richer of the two: `extends` (presets), `rules` (name to a severity, or to
  `[severity, options]`), `overrides` (files plus rules), `ignores`,
  `project`/`projects`, `reportUnusedDisableDirectives`, `eslintDirectives`. The
  current TS config (a bare `Rule[]` in `plugin/ts/config.ts`) is replaced by
  this shape. `plugin/ts/schema.ts` is the TypeScript mirror: the types plus
  `defineConfig` over them.
  - [x] `loadConfig` and the driver still read the `Rule[]` shape, and index.ts
    still exports its `defineConfig`; both swapped over with the loader in 8.2.
- [x] Add `plugins`: a map from a prefix to a JS module specifier. A plugin
  rule is referenced namespaced as `prefix/rule`. The native binary skips a
  namespaced rule it has no registry entry for, rather than reporting it unknown
  (`lint::Config` already gathers `m_unknownRules`; teach it the prefix
  distinction so a typo still warns).
- [x] Add a `binary` item naming the native lintrix executable the npm CLI
  should drive when present. The WASM fallback (task 8.3) runs when it is absent
  or cannot be resolved, so the item is an optimization, not a requirement.
- [x] Ship a JSON Schema so an editor validates `lintrix.config.json`
  directly. `defineConfig` stays the typed authoring wrapper for `.ts`/`.js`.
- [x] Document the schema in docs/rules.md "Config", marking which keys are
  native-only (the presets resolve against the C++ registry) and which the
  plugin side reads. This settles the config half of the cross-cutting "ESLint
  compatibility surface" item.

### 8.2 Config compiler (the JS loader)
The package's core: read `lintrix.config.{ts,js,json}` and emit schema-valid
JSON.
- [x] A loader that imports a `.ts`/`.js` config (reusing the dynamic import in
  `plugin/ts/config.ts`) or reads a `.json` one, resolves it to the 8.1 schema,
  and writes or streams `lintrix.config.json`. A `.json` input passes through
  after validation.
- [x] Resolve `plugins` specifiers to rule objects at compile time. The JSON the
  native binary receives carries only resolved native rules; plugin rules are
  routed to the embedding instead.
- [x] Make TS rules configurable the way native rules are: `RuleContext` gains
  `options`, and `lint()` takes resolved `(rule, severity, options)` tuples
  rather than a bare `Rule[]`. It skips a rule set to `off`, tags each message
  with its severity, and applies `overrides`/`ignores` per file.
- [x] A `lintrix config` subcommand (or `node make.ts` task) that prints the
  resolved JSON, for debugging and for the `--config` handoff to the native
  binary. Landed as `node make.ts config [file] [--native] [--out <path>]`; the
  npm CLI grows the same command in 8.3.
  - [x] The native handoff document is `.lintrix.native.json`
    (`nativeConfigPath`), written beside the config it came from, since globs and
    tsconfig paths anchor at the config file's directory. 8.3 settled on keeping
    it there rather than adding an anchor flag to `lintrix lint`: the anchor a
    flag would name is always the config's own directory, so the flag would carry
    no information. .gitignore lists the name.

### 8.3 The npm `lintrix` package
- [x] A publishable package: a `package.json` with `bin` (the `lintrix` CLI),
  `exports` (the rule and config surface `index.ts` already sketches), and
  `files`, with `private` dropped. It needs a build, because the sources import
  `../generated/ts/views.ts` with `.ts` extensions and ship no compiled JS
  today. `tsconfig.package.json` emits `dist/` with declarations and rewrites
  those specifiers; `node make.ts pack [--wasm] [--smoke]` builds it and
  `prepack` runs it.
- [x] Bundle the WASM build (`build/wasm/bin/fastlint.js` plus its `.wasm`) in
  the package as the fallback engine, so `npm i lintrix` lints with no native
  binary installed. `pack` copies the `wasm-release` module to `dist/wasm/` and
  warns when it falls back to the debug one.
  - [x] `embed::lintTextWithConfig` and `fl_wasm_lint_config`, so the fallback
    applies the run's config. Without it the WASM path silently linted the
    recommended preset while the native path read the config.
- [x] The CLI wrapper: resolve the config (task 8.2), then lint through the
  native binary named by the `binary` item (or discovered on `PATH`) when it is
  present, handing it the emitted JSON through `--config`; otherwise lint
  through the bundled WASM runtime. Merge the native built-in results with the
  plugin-rule results into one report. `plugin/ts/{cli,engine,files,report}.ts`;
  `--engine native|wasm` pins the choice, and both engines report identically.
  - [x] `--fix` (2026-09-12): passed through to the native binary; the WASM
    path runs the fix loop the VS Code extension's fix-all already had, now
    shared in plugin/ts/fixes.ts.
  - [x] The PATH lookup for the native binary skips npm's own `lintrix` shims
    (2026-09-12); `pack --install` lints with `node_modules/.bin` first on
    PATH to cover it.
- [x] Decide how the native binary is distributed: an optional
  platform-specific dependency or a postinstall download, against WASM-only by
  default. The WASM fallback is what lets that stay a performance choice.
  Decided WASM-only: the binary is found through the config's `binary` item or
  on PATH, never fetched. A postinstall download fails inside a locked-down CI
  during `npm i`; per-platform optional dependencies are the right next step but
  need a release pipeline that does not exist yet. docs/embedding.md
  "Distributing the native binary" records this.
- [x] A release process. `node make.ts release <major|minor|patch|X.Y.Z>` runs
  the gates, bumps package.json and `source/fastlint/version.cc` together,
  builds `pack --wasm`, writes `build/release/lintrix-<version>.tgz`, installs
  that tarball in a throwaway project and lints through the linked `lintrix`
  command, then tags, pushes and creates the GitHub release. `node make.ts
  publish` sends the same tarball to npm, kept a separate command because it is
  the only irreversible step. `--dry-run` on either stops short of it.
  - [ ] Run the release from CI rather than a developer's machine, once there
    is a workflow that can build the WASM engine. That is also what the
    per-platform native packages above need.

---

## 9. VS Code extension

Goal: a `lintrix` extension shaped like vscode-eslint (C:/dev/vscode-eslint).
A thin client starts an LSP server. The server lints open documents, publishes
diagnostics, and serves quick fixes, suggestions, disable directives and
fix-all on save. The phases are split by engine. The first runs the syntactic
rules in-process through the embedding (8.3) and ships as one universal VSIX.
The second adds a long-lived native `lintrix serve` so the type-aware rules run
in the editor too.

What already lines up (surveyed 2026-09-12):
- The `--format json` shape (docs/rules.md "Output") is the wire format an
  editor wants: 1-based UTF-16 `line/column/endLine/endColumn`, `fix` as a
  UTF-16 `range` plus `text`, `suggestions` with their own fixes. Each maps to an
  LSP `Diagnostic` or `TextEdit` through `TextDocument.positionAt`.
- `plugin/ts/{config,compile,engine,report}.ts` resolve a config per file, pick
  native or WASM, and merge native and plugin messages. The server imports them.
- The disable directives exist (`lintrix-disable-line`, `-next-line`, the block
  pair), so the disable actions are text insertion.
- tsgo's `updateSnapshot` takes `openFiles`/`closeFiles`/`fileChanges`
  (docs/tsgo-api.md), mirroring LSP `didOpen`/`didChange`, which is what a serve
  mode needs for unsaved buffers.
- vscode-eslint's `diff.ts` (1k lines) and its eslintrc/flat/CLIEngine probing
  have no counterpart here and are not ported.

### 9.1 Layout and packaging
Landed 2026-09-12; docs/vscode-extension.md describes it.
- [x] `editors/vscode/` with its own `package.json`, `client/` and `server/`,
  bundled by `esbuild.mts` into `out/client.js` and `out/server.js`. The
  server imports `source/fastlint/plugin/ts` by path rather than copying it,
  so the config loader has one home. The bundles are CommonJS, and
  `import.meta.url` is defined to the bundle's own URL so `findWasmModule`
  resolves `out/../wasm/`. The extension's tsconfig switches to
  `moduleResolution: bundler`, since `nodenext` reads the directory's
  package.json as CommonJS; `check` typechecks it when it is installed.
  - [ ] `pack` learns to emit the published `lintrix` package instead once the
    extension depends on a released version.
- [x] `node make.ts vsix [--wasm] [--install]` installs the extension's
  dependencies, stages the WASM engine, the config schema and the license,
  bundles, packages with `vsce --no-dependencies` into build/vsix/, and
  optionally installs into the `code` on PATH. One VSIX serves every platform.
  Verified: the bundled server answers `initialize` over `--stdio` and logs
  the staged engine; the VSIX installs.
- [x] Manifest: `activationEvents: onStartupFinished`, `jsonValidation`
  mapping `lintrix.config.json` to the staged schema,
  `capabilities.untrustedWorkspaces: supported: false` (a `.ts` config is
  executed), `virtualWorkspaces: supported: false`. The client's document
  selector names javascript, javascriptreact, typescript and typescriptreact;
  `contributes.languages` is not needed, since VS Code defines those ids.
  - [ ] `publisher` is a placeholder until there is a Marketplace publisher.

### 9.2 Client
Landed 2026-09-12; docs/vscode-extension.md "The client" describes it.
- [x] Settings under `lintrix.*`: `enable`, `run` (`onType` | `onSave`),
  `validate` (language ids), `binaryPath` (a native `lintrix` for 9.5;
  otherwise the config's `binary` item, then PATH), `engine` (`auto` |
  `native` | `wasm`), `codeActionsOnSave.mode` (`all` | `problems`),
  `trace.server`. Each is `scope: resource`; `shared/protocol.ts` holds the
  shape and defaults for both sides. `run`, `validate` and `enable` act in the
  client's diagnostic pull filter; the server reads the rest through
  `workspace/configuration` (`server/settings.ts`) and drops the cache on
  `didChangeConfiguration`. `engine` and `binaryPath` choose the serve mode
  (9.5).
- [x] Commands: `lintrix.executeAutofix` (routed to the server's
  `lintrix.applyAllFixes`), `lintrix.restart`,
  `lintrix.revalidate` (a `lintrix/revalidate` notification; the server drops
  every cache and re-pulls), `lintrix.showOutputChannel`.
- [x] The server runs over Node IPC (the extension host's own transport for a
  Node server), with the file watchers for `lintrix.config.*` and
  `tsconfig.json` registered by the client.
- [x] Status bar item (`client/status.ts`) fed by `lintrix/status`
  notifications: the engine in use per document, an error state when the
  engine or the config did not load, hidden for documents the server has not
  reported on. Clicking it opens the output channel, which carries the reason.

### 9.3 Server: diagnostics
Landed 2026-09-12; docs/vscode-extension.md "The server" describes it.
- [x] `vscode-languageserver` over IPC with `TextDocuments`, and
  `diagnosticProvider` (pull model). `codeActionProvider` and
  `executeCommandProvider` come with 9.4.
- [x] `server/configs.ts`: the nearest `lintrix.config.*` walking up from the
  file, memoized per directory, compiled once per config path. A file an
  `ignores` glob claims is answered empty without a parse. The cache is dropped
  on `didChangeWatchedFiles` for a config or tsconfig and the client is asked
  to refresh, which re-pulls every open document. Module configs load with a
  new `fresh` option on `loadConfigFile`, since the server outlives edits to
  them. A config that fails to load shows its error as one diagnostic at the
  top of each file under it, until the status bar (9.2) can carry it.
- [x] Lint on the client's pull, with the document's current text. The client
  decides when to pull (on type, on save, on focus), which is where the `run`
  setting acts in 9.2. `server/engine.ts` runs the built-in rules through the
  WASM `lintText` and the plugin rules through the TypeScript runtime over the
  same addon, merged through `report.ts` as in the CLI. An untitled buffer
  lints under the recommended preset as a name with its language's extension.
- [x] `server/diagnostics.ts` maps each message: `severity` 2 to Error, 1 to
  Warning, `ruleId` to `code` with `codeDescription.href` from the rule's
  `url`, source `lintrix`, ranges clamped to the document. An unused directive
  gets `DiagnosticTag.Unnecessary`. Covered by `server/diagnostics.test.ts`,
  which `node make.ts test` runs when the extension is installed.
- [x] The report is kept per open document and each diagnostic's `data`
  holds its message's index, so a code action finds the `fix` and
  `suggestions` without a second lint.
- [x] The JSON messages carry `url`, the rule's docs page, when the rule has
  one (lint/format.cc; both embeddings print through it). `EslintMessage`
  gained `url` and typed `fix`/`suggestions`. docs/rules.md "Output" notes it
  as an addition over ESLint's shape.

### 9.4 Server: code actions and fixes
Landed 2026-09-12; docs/vscode-extension.md "Code actions and fixes"
describes it. `server/actions.ts` and `server/diff.ts`, with tests.
- [x] Quick fix per fixable problem: the `fix` range and text become one
  `TextEdit`, inline in a versioned `WorkspaceEdit`, marked preferred. A
  message's `suggestions` become one action each, titled by `desc`. "Fix all
  `<rule>` problems" when the rule has more than one non-overlapping fix.
- [x] Disable actions: `// lintrix-disable-next-line <rule>` inserted above
  with the line's indentation, or appended after a comma to a
  `disable-next-line` directive already there (either spelling, either comment
  form, before a ` -- justification` tail); `/* lintrix-disable <rule> */` at
  the top of the file, below a shebang. The `lintrix-` spelling is always the
  one written: `eslintDirectives` defaults on, so the `eslint-` spelling is an
  alias, not the preferred form.
- [x] "Show documentation" runs `lintrix.openRuleDoc` with the message's
  `url`; the client registers it.
- [x] Fix all (`source.fixAll.lintrix`, the `lintrix.applyAllFixes` command
  behind `lintrix.executeAutofix` and the closing quick fix): apply the
  non-overlapping fixes of one pass, re-lint the result, repeat up to ten
  passes, then diff the final text against the document. `diff.ts` is Myers
  over lines with each hunk trimmed to the differing characters, ~150 lines
  rather than vscode-eslint's thousand. The embedding keeps returning
  single-shot edits.
  - [ ] Decide whether `lintText` should return `output` instead (the native
    `--fix` fixpoint), which would make the server loop unnecessary. Deferred
    until the loop's cost on a large file is measured.
- [x] `codeActionsOnSave.mode: problems` applies the fixes already shown in
  one pass without linting again.
- [x] Found and worked around on the way: V8's background WASM tier-up makes
  `process.exit()` trip a libuv assertion on Windows (Node 24.14) after a few
  lints, and `vscode-languageserver` exits that way. `Engine.load` sets
  `--no-wasm-dynamic-tiering` at runtime; docs/vscode-extension.md "The exit
  crash" has the measurements. The npm CLI exits naturally and is unaffected.

### 9.5 Native serve mode (type-aware rules in the editor)
Landed 2026-09-12; docs/vscode-extension.md "The native serve mode" describes
it. The embedding runs no type-aware rules; they need tsgo and a resolved
tsconfig. A per-save `lintrix lint --format json` spawn pays tsgo startup every
time and cannot see an unsaved buffer, so the editor gets a resident server.
- [x] `lintrix serve` (source/cli/serve.cc): JSON-RPC 2.0 over stdio in LSP's
  `Content-Length` framing (rather than the tsgo msgpack envelope, which no
  editor-side library speaks) around the per-run setup of source/cli/lint.cc,
  whose helpers moved to source/cli/run.h. One session per config: one tsgo
  process, one open `Store`, the config loaded once. Requests: `lint {file,
  text?, config?}` answering `{results, typed, typeError?}` with the JSON
  messages, `close {file}`, `changed {files}`, `configChanged {path?}`,
  `shutdown`. `text` overlays the file on disk.
- [x] Open documents reach tsgo through `ProjectTypes`, which already serves
  the text it was handed and sends `changed`/`openFiles` when it differs from
  the disk; `addProject` opens a file's tsconfig on first sight in the running
  server, `forgetFile` and `filesChanged` drop held text so the disk is read
  again. A snapshot is replaced only when something changed, as the CLI does.
- [x] Watched-file changes: the client also watches the source files; every
  change is forwarded as `changed` (a tsconfig or config among them drops the
  session, which reloads on the next lint), and only a config or tsconfig
  change re-pulls the open documents.
- [x] The result cache stays on: a `text` equal to the disk is cached and
  replayed, a differing one is neither. The tsconfig hash keys each file's
  result rather than the store's environment hash, since the projects are not
  known when the store opens.
- [x] `plugin/ts/serve.ts` is the `ServeClient`; `server/native.ts` keeps
  one per binary, restarting after an exit, and writes the handoff config.
  `resolveRun` picks it when `binaryPath`, the config's `binary` or PATH
  yields a binary, and falls back to WASM with a status bar note that the
  type-aware rules are off; `engine: native` with no binary is an error.
- [x] Tests: `cli_serve` (`[integration]`) spawns the built binary and drives
  a saved file, an overlay, a config change and a shutdown;
  plugin/ts/serve.test.ts covers `ServeClient` against a built binary; the
  VS Code smoke test runs through the native engine when a preset has built
  `lintrix`, expecting a type-aware diagnostic and its fix.
- [x] Dogfood on a monorepo (2026-09-12) found two type-server faults, both
  in the CLI as well: a run over two tsconfigs handed one project's type
  handles to the other (`type handle N not found in project registry`), since
  handles are scoped to a project's registry within a snapshot and only a new
  snapshot cleared them; binding a project now clears them too
  (`types_facts.switching_projects_drops_the_session_ids`). And a
  `getTargetOfType` on a non-object type panics the server, so `targetOf` and
  `isTuple` check the `Object` type flag before the `Reference` object flag.
  `lintrix.tsgoPath` names the `tsc` the native engine types with (passed as
  `FASTLINT_TSGO`), and a `tsc` named that way runs with a note instead of a
  refusal when its version is not in the probed list, for a master build.
- [x] The same dogfood hit a tsc 7.0.2 crash serializing an empty tuple
  literal's type (`checker.TypeData is *checker.TypeReference, not
  *checker.TupleType`, fixed upstream in microsoft/typescript-go#64080). The
  server recovers and only that query fails, but the run reported the file as
  "no types" and the whole file's type-aware results looked lost. Now
  `FileResult::typed` separates a file the source could not type from one
  query that failed mid-walk: the CLI prints "type query failed", the serve
  response keeps `typed` true with the failure in `typeError`, and the status
  bar words it as a query failure. A `panic:` payload loses its goroutine
  stack and gains a note naming the tsc version and `FASTLINT_TSGO`
  (`tsgo_client.a_server_panic_loses_its_stack_and_names_the_fix`,
  `types_facts.a_query_that_crashes_the_server_fails_alone`). Also fixed on
  the way: a `beginFile` failure's message was wiped by `lintFile` clearing
  the result, so the CLI never printed it. The smoke test retries a code
  action request VS Code cancels while the built-in TypeScript extension
  registers its providers.

### 9.6 Tests
Landed 2026-09-12; docs/vscode-extension.md "Tests" describes them.
- [x] `node --test` over the server's mapping code, with no VS Code process:
  `server/diagnostics.test.ts` (message to `Diagnostic`),
  `server/actions.test.ts` (quick fixes, suggestions, disable insertions in
  both spellings and comment forms, stale-version refusal) and
  `server/diff.test.ts` (fix-all edits round-trip, including past the
  distance cap). Landed with 9.3 and 9.4.
- [x] `@vscode/test-electron` smoke: `test/run.mts` downloads a stable VS
  Code into editors/vscode/.vscode-test/, writes a fixture workspace and
  launches the extension from its directory over it (other extensions and
  workspace trust off); `test/suite.ts` runs inside, waits for the `curly`
  and `no-debugger` diagnostics, checks the quick fix is offered, runs
  `lintrix.executeAutofix` and waits for the text to change and the
  diagnostics to clear. Verified that a failing assertion fails the run.
- [x] `node make.ts test` runs the `node --test` files when the extension is
  installed; the smoke is `node make.ts vsix --smoke`, which runs it before
  packaging. It is not in `check`, since it downloads VS Code.

---

## Cross-cutting

- [x] `docs/tests.md` — testing strategy + framework spec (written
  2026-09-04; update as the framework lands).
- [x] `docs/debugging.md` — debugging aids per subsystem (written
  2026-09-04; each listed flag/subcommand becomes a real task in its
  component: `dump-tokens`, `dump-tree`, `dump-ast`, `--trace-parser`,
  `--trace-tsgo`, `--trace-fixes`, `cache inspect/verify`, `--explain`,
  `--timing`, `--trace-json`, `--alloc-stats`).
- [x] Fuzz harness for the parser (2026-09-05): `lintrix fuzz` mutates
  files at token level (delete/duplicate/replace/swap tokens, insert
  fragments, truncate, flip bytes, insert or overwrite with random bytes)
  and, for one case in twelve, feeds random data (arbitrary bytes, NULs,
  invalid and truncated UTF-8, astral code points, long delimiter runs)
  alone or grafted onto the file; parses each mutant and checks tree
  invariants (token order and bounds, node token ranges, parent links).
  `node make.ts fuzz` drives it under the `asan` preset in batches, pins
  a crash/hang/invariant failure to its seed from the `# file seed`
  progress lines, replays the seed to build/asan/fuzz-failures/ and
  ddmin-minimizes it. First run over the corpus (50 mutants per file)
  found three bugs, all fixed: async arrow `firstToken` one token early
  (underflow at file start); unterminated `` `${ `` left an orphan
  `TemplateSpan`; a multibyte character cut off by EOF produced a token
  past the end of the source. Second run after the fixes: 643850
  mutants, no failures, 375 s. Under `clang-asan` (ASAN + UBSan) with the
  random-data mutations, seed 11: 772620 mutants, no failures. Deep ASAN
  run, 300 mutants per file, seed 7: 3863100 mutants, no failures, 43 min.
  Fuzz processes cap the ASAN quarantine (`quarantine_size_mb=16`) and run
  50 files each; the parser itself holds flat at 8 MB across mutants.
  - [ ] Promote minimized cases to fixtures automatically.
- [x] `make.ts bench` with JSON baselines and `--compare`.
- [x] `README.md` — what/why, quickstart, `make.ts` commands (8.3 restructured
  it to lead with `npm i -D lintrix`).
- [ ] `CLAUDE.md` — repo conventions (build, style, layout), pointing at
  docs/STRATEGY.md and this list.
- [ ] Bench suite (`node make.ts bench`) tracking parse MB/s, lint files/s,
  warm-cache rerun time.
- [ ] Decide ESLint compatibility surface (config, rule names, directives).
