# tsgo client

The C++ side of the type layer: how lintrix talks to `tsc --api`. The
protocol facts it relies on are measured in docs/tsgo-api.md; this document
covers the code under `source/fastlint/tsgo/`. Task 5.1 of
docs/tasklists/MASTER.md.

## Pieces

| File | Holds |
| --- | --- |
| `process.h` | `Process`: a child with piped stdin/stdout, Win32 and POSIX. `runCapture` for one-shot commands. |
| `msgpack.h` | `encodeFrame` / `decodeFrame` for the `[type, method, payload]` envelope. |
| `json.h` | `JsonDocument` (parser into pooled `JsonValue`s) and `JsonWriter` (request builder). |
| `client.h` | `Client`: exe resolution, version gate, spawn, `initialize`, `call`, snapshots, FS callbacks. |
| `queries.h` | `Session` and the typed responses; one method per endpoint the rules need. |
| `source_file.h` | `EncodedSourceFile` decoder, `nodeHandle`, `canonicalPath`, `NodeIndexTable`. |
| `generated/compat.h` | Parameter spellings probed against the pinned binary. |
| `generated/enums.h` | `TypeFlags`, `ObjectFlags`, `SymbolFlags`, `ElementFlags`, `SignatureFlags`, `SyntaxKind`. |

## Process and framing

- `Client::start` resolves the binary, runs `tsc --version`, refuses anything
  not in `kSupportedVersions`, then spawns `tsc --api --cwd=<dir>` with
  `--callbacks=readFile,fileExists` when a `FileProvider` is given.
- Resolution order: `FASTLINT_TSGO`, then `@typescript/typescript-<platform>-<arch>/lib/tsc`
  under a `node_modules` at or above the working directory (including pnpm's
  `.pnpm` store), then `tsc` on PATH. Only a real executable counts, so
  pnpm's `tsc.CMD` shim is skipped.
- Every frame is a msgpack fixarray of three elements. `decodeFrame`
  distinguishes an incomplete prefix from a malformed one; the client keeps
  reading on the former and marks the connection broken on the latter.
- The method name is the correlation id, so `call` is synchronous and one
  request is in flight at a time. Server-to-client `Call` frames (FS
  callbacks) arrive while a response is awaited and are answered inline.
- Error frames carry a bare message, which `call` returns as `error`. The
  connection stays usable; the server recovers its own panics.
- EOF or a write failure sets `m_broken`; every later call fails fast.
  `stop()` closes stdin, waits up to a second for a clean exit and then kills.

## JSON

- Own parser and writer; no dependency. Values live in a `Pool` owned by the
  `JsonDocument`, so a parsed response is freed in one go.
- Numbers are doubles; `asInt`/`asUint` truncate. Ids and flags fit.
- `JsonWriter` handles commas and quoting; `member(name, value)` is the common
  call. Integers print without a fraction, other doubles with `%.17g`.

## Snapshots

- `openProject(tsconfig)` is `updateSnapshot {openProjects: [tsconfig]}` and
  returns the snapshot id and the projects it holds. Queries need both the
  snapshot and a project id (the canonical tsconfig path).
- `SnapshotUpdate` covers `openProjects`, `closeProjects`, `openFiles`,
  `closeFiles`, `fileChanges {changed, created, deleted}` and
  `invalidateAll`. `SnapshotInfo::changedFiles` flattens the response's
  per-project change lists.
- Type, symbol and signature ids belong to the snapshot that minted them and
  never leave the process; `release(snapshot)` ends their lifetime.

## Node handles

- `Session::sourceFile` fetches the server's parse tree and decodes the
  44-byte header and the 28-byte node records. Node-list pseudo-entries have
  kind `0xFFFFFFFF` and are skipped by every lookup.
- `EncodedSourceFile::findNode(pos, end, kind)` is the narrowest cover, deeper
  node on ties. `nodeHandle(index, kind, canonicalPath)` formats
  `index.kind.path`; `canonicalPath` lower-cases on a case-insensitive file
  system, which `Client::caseSensitiveFileNames()` reports from `initialize`.
- `NodeIndexTable::build(file, encoded)` pairs our AST nodes with tsgo
  indices. Both tables are in source order, so a node matches the tsgo nodes
  with the same `end`; among those, the group with the largest `pos` not past
  our `start` (a tsgo `pos` includes leading trivia), and within a same-span
  run the k-th of ours takes the k-th of theirs. Error nodes and zero-width
  nodes stay unmapped, and so does anything our parser shapes differently.
- tsgo spans are UTF-16 code units over the text with its byte order mark
  removed; ours are UTF-8 bytes. `Utf16Offsets` (source_file.h) records the
  multi-byte characters of a file once and converts each node's span before
  matching. Measured on visualnovel (541 files, 378k expression nodes), 0.6%
  of expression nodes stay unmapped afterwards: `constructor` and `new`
  keyword identifiers, and parameter identifiers whose ESTree span includes
  the type annotation, none of which tsgo represents as a node.
- `getTypeAtPosition` also takes UTF-16 offsets and lands on tokens; handles
  avoid that and are the route for expression types.

## Queries

- `Session::sourceFileNames` lists every file in the project's program, lib
  and package files included, under the names the server uses; the bench
  filters it to project sources.
- `RpcStats::readSeconds` is the wall time spent blocked on the server's
  stdout; the difference from a call's elapsed time is our own encoding,
  parsing and interning. The JSON writer and parser buffer in `std::string`,
  as boundary adapters may.
- `Session` methods return false only on a transport or server failure. A
  null answer is `present == false` on the response struct.
- The type-id parameter is spelled per endpoint (`objectId` or `type`) from
  `generated/compat.h`; `typeIdParam(method)` falls back to `type`.
  Regenerate with `node tools/spikes/tsgo-api/main.ts compat --emit` after a
  version move, then extend `kSupportedVersions`.
- Kind-specific calls are guarded by the caller: `typeArguments` on a
  non-reference and `typesOfType` on a non-union make the server panic into
  an error response.
- `generated/enums.h` comes from `node make.ts gen-tsgo-enums`, which reads
  the installed typescript package so the values match the binary.

## Tests

- `tsgo_json_test`, `tsgo_msgpack_test` and `tsgo_source_file_test` are
  `[fast]` and need no server; the source-file test decodes a hand-built
  table for `foo(1);` and checks the side table against our lowering of it.
- `tsgo_client_test` is `[integration]`: it opens
  `tests/fixtures/projects/basic`, checks flags and structure of the queries
  from docs/tsgo-api.md, synthesizes a handle for the floating call, maps it
  through `NodeIndexTable`, and serves a replacement `main.ts` over callbacks.
  It skips when no native `tsc` can be resolved.
