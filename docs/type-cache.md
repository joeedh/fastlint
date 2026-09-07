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

## Verify

`Store::verify` runs `PRAGMA integrity_check` and then counts dangling
references: children without a type row, types whose symbol or alias symbol
is missing, and node types pointing at a missing type. The driver's
`cache verify` mode (task 6) will call it and recompute a sample of node
types against `tsgo`.

## Tests

`source/tests/cache_store_test.cc` (`[fast]`) opens in-memory stores for the
round trips and one temp-file store to check that a version bump rebuilds and
that a matching reopen does not.
