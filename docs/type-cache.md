# Type cache store

The store in `source/fastlint/cache/` is the SQLite database that holds the
interned type graph, each file's node types and each file's rule results
between runs. It is the persistence half of docs/type-facts.md: `TypeGraph`
rows are the unit stored, and a warm run answers from `node_types` without
consulting `tsgo`. `Store` is the only code that touches SQLite; the `std::`
types it uses stay inside store.cc.

## Vendoring

- `node make.ts deps` downloads the sqlite amalgamation zip from sqlite.org
  into `vendor/sqlite/` (gitignored), alongside the git externals. The
  archive is pinned by version and sha3-256 in tools/make/deps.ts; a
  mismatch fails the fetch. `deps fetch sqlite --force` refetches.
- tools/make/lib/zip.ts is a small reader for stored and deflated entries,
  so the fetch needs no extra dependency.
- The root CMakeLists.txt builds `sqlite3` as a static C library (the project
  enables C for it) with `SQLITE_THREADSAFE=2`, `SQLITE_DQS=0`,
  `SQLITE_OMIT_LOAD_EXTENSION` and the other defines listed there, and fails
  configure with a message naming the fetch command when the sources are
  missing. Warnings are off for that target.

## Connection

- `Store::open` takes a path (`:memory:` for tests), the pinned `tsc`
  version and a hash of the lib `.d.ts` set. File databases run in WAL mode
  with `synchronous=NORMAL`.
- One connection, one writer. Callers wrap a file's worth of writes in
  `begin`/`commit` so a lint run commits once per file rather than per row.
- Every method returns false with an error string on a database failure.
  A missing row is reported through a `found` flag and is not an error.

## Schema

`kSchemaVersion` in store.h names the shape; every table is created with
`IF NOT EXISTS` on open.

| Table | Columns | Notes |
| --- | --- | --- |
| `meta` | `key, value` | `schema_version`, `tsgo_version`, `lib_hash` |
| `files` | `path, content_hash, closure_hash, tsconfig_hash` | one row per source file |
| `types` | `hash, seq, flags, object_flags, is_tuple, symbol_hash, alias_hash, text, child_kind` | `hash` is the `TypeGraph` structural hash |
| `type_children` | `parent_hash, ordinal, child_hash` | union members or type arguments |
| `symbols` | `hash, seq, name, flags, check_flags` | |
| `symbol_declarations` | `symbol_hash, ordinal, handle` | tsgo node handles |
| `node_types` | `file_hash, start, end, kind, type_hash` | keyed by span and kind within a file version |
| `rule_results` | `file_hash, closure_hash, rule, payload` | opaque per-rule blob |

- Hashes are 64-bit and stored as signed `INTEGER` by bit cast.
- Graph rows are addressed by hash, never by the in-memory `TypeId`, so a
  graph loaded in another process resolves to the same rows.
- `seq` records the order rows were interned. Loading in `seq` order puts
  every child and symbol before the row that refers to it, so
  `TypeGraph::intern` recomputes the same hash from the same inputs;
  `loadGraph` checks that and fails if a stored hash disagrees.

## Graph save and load

- `saveGraph(graph, cursor)` writes the rows appended since `cursor` and
  advances it. Graph rows only append in memory, so a cursor per store is
  enough; inserts use `OR IGNORE`, so two processes writing the same type
  agree.
- `loadGraph(graph)` interns every stored row into a graph, which may already
  hold rows; identical rows merge by hash. Loaded rows carry no tsgo session
  id.
- `countTypes` is for reporting.

## Versioning

- `open` compares `meta` with the options. A different `schema_version`,
  `tsgo_version` or `lib_hash` drops every table and recreates them;
  `rebuilt()` reports that so the driver can log a cold run. There are no
  incremental migrations: a schema bump is a rebuild.
- A fresh database writes `meta` and reports no rebuild.

## Invalidation (v1, file closure)

The store answers for a file only while the file, everything it imports and
the tsconfig are unchanged. `source/fastlint/cache/imports.h` and `closure.h`
compute that key.

- `collectImports(file, specifiers)` walks the AST once and returns every
  module specifier the file names: import and export declarations,
  `import x = require("m")`, dynamic `import("m")` and `require("m")` with a
  literal argument, and `import("m")` types. Type-only imports count, since
  they change types.
- `resolveImport(from, specifier, fs)` handles relative specifiers the way
  `moduleResolution: bundler` does: the exact path, a `.js`-family extension
  rewritten to its TS source, the TS extensions appended, then a directory
  index. Bare package specifiers and `paths` aliases are not resolved; they
  are kept as unresolved names so adding or dropping one still changes the
  hash. Package contents are covered only through `lib_hash` and the
  tsconfig hash, so a driver that wants lockfile changes to invalidate folds
  the lockfile into the tsconfig hash.
- `ImportGraph` holds one `FileEntry` per file seen: path, content hash,
  resolved imports, unresolved specifiers and a `selfHash` over those. The
  closure hash of a file is the hash of the sorted `selfHash`es of every file
  reachable from it, itself included, so it does not depend on import order
  and a cycle contributes each member once. Results are memoized until the
  next `setFile`.
- `loadClosure(graph, path, fs, error)` reads, parses and adds a file and,
  transitively, every resolved import; a file already present with the same
  content hash is not reparsed. `FileSystem` is the seam the tests replace
  with an in-memory tree; `DiskFileSystem` is the real one.
- `FileCache` ties a graph to a store. `lookup` builds the current
  `FileRecord` (content, closure and tsconfig hashes) and compares it with the
  stored one: `Missing`, `Stale` or `Fresh`. A stale file has its node types
  and rule results dropped on the spot so a lint refills them; `commitFile`
  writes the new record afterwards. `ruleResult` and `saveRuleResult` replay
  and store a rule's payload under the record's content and closure hashes,
  which is the rule-result cache from docs/STRATEGY.md.

Touching a widely imported file therefore invalidates every importer, which
is coarse but correct. Per-type provenance (v2) waits on measuring v1 on a
real monorepo.

## Measurement

`fastlint cache-bench [--cache <db>] [--limit N] [--keep] [--json] [--unmapped]
<tsconfig>` (source/cli/cache_bench.cc) runs a cold pass from an empty
database and a warm pass over the same one. Each pass opens the project in
tsgo, lists its source files with `getSourceFileNames`, loads their import
closure, and for every file either replays `node_types` or fetches the type
of every expression node and stores it. It reports time per phase, the pipe
wait inside the fetch, peak working set and database size. `--unmapped`
tallies expression nodes without a tsgo counterpart by kind and prints
samples from the worst file.

Release build on visualnovel (541 project files, 378k expression nodes),
2026-09-06:

| | cold | warm |
| --- | --- | --- |
| total | 11.5 s | 1.3 s |
| tsgo start + project open | 0.23 s | 0.24 s |
| graph load from the store | 0 | 0.31 s (41,920 types, 23,742 symbols) |
| import closure | 0.34 s | 0.34 s |
| parse + lower | 0.24 s | 0.19 s |
| type fetch | 7.7 s (3.8 s waiting on the pipe) | 0 |
| store writes | 2.8 s | 0 |
| replay | 0 | 0.11 s (367k node types) |
| rpc | 33,323 calls, 29 MB sent, 74 MB received | 3 calls |
| peak working set | 97 MB | 97 MB |
| database | 31 MB | 31 MB |

What the measurement changed:

- Three byte-at-a-time appends into litestl strings were quadratic and
  dominated everything: reading a file in `DiskFileSystem`, building a
  request in `JsonWriter`, and decoding string values in the JSON parser.
  The closure phase went from 25 s to 0.1 s on 200 files and the fetch from
  58 s to 1.2 s on 50 files once those gathered into a `std::string` first.
  litestl's `string` grows to the exact size on every append; any bulk text
  belongs in a `std::string` at the boundary.
- tsgo spans are UTF-16 units over BOM-stripped text (docs/tsgo-client.md);
  before the conversion 80% of expression nodes were unmapped, after it 0.3%.

What is left, in order of payoff:

- 23,744 of the 33,323 calls fetch one symbol each (`getSymbolOfType` while
  interning). Batching them through `batchRequests` or deferring symbol
  interning would remove most of the remaining pipe wait.
- Store writes are 2.8 s for 377k node types in per-file transactions; one
  transaction per batch of files, or a prepared-statement cache, would cut
  that.
- The whole graph stays in memory (the LRU from docs/type-facts.md is still
  open); 97 MB for this project is fine, a monorepo will need the eviction.

## Verify

`Store::verify` runs `PRAGMA integrity_check` and then counts dangling
references: children without a type row, types whose symbol or alias symbol
is missing, and node types pointing at a missing type. The driver's
`cache verify` mode (task 6) will call it and recompute a sample of node
types against `tsgo`.

## Tests

`source/tests/cache_store_test.cc` (`[fast]`) opens in-memory stores for the
round trips and one temp-file store to check that a version bump rebuilds and
that a matching reopen does not. `cache_closure_test.cc` covers the import
collector over every syntax form, path joining, resolution against an
in-memory tree, closure hashes following dependency edits, cycles, loading
tests/fixtures/projects/basic from disk, and the freshness transitions of
`FileCache`.
