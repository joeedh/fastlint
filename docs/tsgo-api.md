# tsgo API surface

What `tsc --api` actually gives us, measured against the installed binary
rather than read off the Go source. Task 2 of docs/tasklists/MASTER.md.

Measured 2026-09-04 on Windows 11, Node v24.14.0:

- Binary: `@typescript/typescript-win32-x64@7.0.2`, resolved through
  `typescript/lib/getExePath.js`.
- Go checkout for reading the protocol: C:/dev/TypeScript, branch
  `joeedh/profiling-tests`, HEAD 253c5e2074a (2026-08-31).
- Spike: `node tools/spikes/tsgo-api/main.ts [phase…]`, phases `queries`,
  `compat`, `handles`, `update`, `callbacks`, `bench`. Raw output lands in
  `.cache/tsgo-api-spike.json`.

## Headline results

- Both transports are trivially implementable by hand. The spike's
  clients are ~120 lines each, with no msgpack or JSON-RPC library.
- **The released binary is not the checkout.** 31 of the checkout's 142
  methods are absent from 7.0.2, including `createProgram` and
  `batchRequests`, and several endpoints renamed their parameters. Every
  protocol fact must be probed against the binary we ship against.
- Per-query latency is ~0.1ms, so the type layer's cost is dominated by
  program setup and by how many queries a rule set issues, not by the wire.
- Position-based queries reach tokens only. Expression-level types need a
  `NodeHandle`, and a handle can be synthesized from the encoded parse tree
  the server will hand us.

## Transport

`tsc --api` accepts:

| Flag | Effect |
| --- | --- |
| `--cwd <dir>` | Server working directory; required in practice. |
| `--pipe <path>` | Listen on a named pipe (Windows) or Unix socket instead of stdio. |
| `--async` | JSON-RPC over LSP framing instead of the default msgpack envelope. |
| `--callbacks a,b` | Delegate `readFile,fileExists,directoryExists,getAccessibleEntries,realpath` to the client. |
| `--timing` | Collect per-request server processing time, read back with `getServerTiming`. |

### Default (msgpack) framing

Despite the name, this is not msgpack serialization. Every message is a
msgpack 3-element array whose payload is a **JSON document**:

```
0x93                     fixarray, 3 elements
<fixint | 0xCC uint8>    message type
0xC4/C5/C6 <len> <utf8>  method name, as msgpack bin
0xC4/C5/C6 <len> <bytes> payload, JSON text (or raw binary, see below)
```

Message types are `1` request (client to server), `2` call response, `3` call
error, `4` response, `5` error, `6` call (server to client, used by FS
callbacks). The method string doubles as the correlation id, so at most one
request per method may be in flight, and the server answers strictly in
order. Payloads for `getSourceFile` and `echo` are raw bytes rather than JSON;
errors carry a bare message string, not a JSON object.

Source: tsc/internal/api/protocol_msgpack.go.

### `--async` (JSON-RPC) framing

Standard JSON-RPC 2.0 with `Content-Length` headers. Requests carry integer
ids, so many can be in flight at once. Source:
tsc/internal/ipc/protocol_jsonrpc.go.

### Choice for the C++ client: msgpack over stdio

- It is the default, so it is the path the TypeScript team exercises.
- Framing is ~60 lines and needs no dependency; the JSON payload needs a JSON
  parser either way, which we need regardless.
- It moves roughly half the bytes of JSON-RPC for the same work (42KB vs 75KB
  sent over the fixture bench), because the envelope is 6 bytes rather than a
  `Content-Length` header plus a JSON-RPC wrapper.
- Its one-request-at-a-time limit costs little: pipelining 100 queries over
  JSON-RPC came out at 0.03ms per query against 0.1ms sequential, and the
  plural endpoints (below) beat both.

Named pipes buy nothing here — we spawn the server ourselves, so stdio is
already private, and a pipe adds a path to manage. Revisit only if we move to
a shared long-lived server.

**Decision: msgpack envelope over stdio, spawned per project. No msgpack
library; hand-rolled framing plus our own JSON parser.** This drops the
"msgpack-c or a header-only msgpack" dependency from task 5.1.

## Version drift

The installed 7.0.2 binary and the Go checkout disagree, in both directions.

Absent from 7.0.2 but present in the checkout (31 methods):

```
batchRequests, createProgram, emit, emitToString, formatNodeForInsertion,
getApparentPropertiesOfType, getConfigFileNames, getConfigSourceFile,
getDeclarationEmit, getDefaultFromTypeParameter, getFullyQualifiedName,
getImportAdderEdits, getJavaScriptEmit, getNonMissingTypeOfSymbol,
getNonPrimitiveType, getReducedType, getSymbolOfSourceFile, getSymbolsInScope,
getSymbolsOfSourceFiles, getTargetSymbol, getTypeParameterAtPosition,
getWellKnownSignatures, isReadonlySymbol, parseCommandLine,
parseJsonConfigFileContent, readConfigFile, transpileDeclaration,
transpileDeclarationFromFile, transpileModule, transpileModuleFromFile,
updateTemporarySnapshot
```

Parameter names moved too. The checkout routes `getNonNullableType` through
`GetTypePropertyParams`, whose type id is named `objectId`; 7.0.2 reads it as
`type`. Probed against the binary, the split is:

| Spelling | Endpoints |
| --- | --- |
| `objectId` | `getSymbolOfType`, `getTypesOfType`, `getAliasSymbolOfType`, `getAliasTypeArgumentsOfType`, and the other `get*Of{Type,Symbol,Signature}` sub-property endpoints |
| `type` | `getNonNullableType`, `getBaseTypeOfLiteralType`, `getWidenedType`, `getApparentType`, `getPropertiesOfType`, `getBaseTypes`, `getIndexInfosOfType`, `isArrayType`, `isArrayLikeType`, `typeToString` |
| `source` / `target` | `isTypeAssignableTo` |
| `signature` | `getReturnTypeOfSignature` |

The encoded source file format drifted as well: the checkout documents
`ProtocolVersion 8` at header byte 0, while 7.0.2 answers with 5 in the high
byte of the first word.

Stability signals: the npm package exports the API only under `./unstable/*`
(`typescript/unstable/async`, `typescript/unstable/sync`, `typescript/unstable/ast`),
and `initialize` negotiates nothing — it returns only
`{useCaseSensitiveFileNames, currentDirectory}`.

**Consequences.** The C++ client pins a tsgo version, records it in the cache
`meta` table, and refuses to run against an unrecognized one rather than
guessing. `tsc --version` is the gate. A per-version parameter-name table
lives beside the client, and `node tools/spikes/tsgo-api/main.ts compat`
regenerates it when we move versions.

## Session lifecycle

7.0.2 has no `createProgram`, so the only way in is the snapshot model:

1. `initialize` (no params) → `{useCaseSensitiveFileNames, currentDirectory}`.
2. `updateSnapshot {openProjects: [tsconfigPath]}` → `{snapshot, projects[],
   changes?}`. Each project carries `id` (its canonical tsconfig path) and
   `configFileName`. Opens are ref-counted and persist across snapshots.
3. Query with `{snapshot, project, …}`.
4. `release {snapshot}` when done.

`getDefaultProjectForFile {snapshot, file}` maps a file to the project that
owns it, for the case where we do not know the tsconfig up front.

`updateSnapshot` also takes `openFiles`/`closeFiles` (mirroring LSP
`didOpen`), `closeProjects`, and `fileChanges {changed, created, deleted}` or
`{invalidateAll: true}`.

### Handle lifetimes

`release` takes a snapshot and nothing else — there is no per-type or
per-symbol release. Type ids, symbol ids and signature ids live as long as the
snapshot that minted them, and are scoped to a project registry within it.
That is the whole discipline: one snapshot per lint pass, released at the end.
Ids are *not* stable across snapshots and must never reach our SQLite store as
keys; the store keys on structural hashes, as docs/STRATEGY.md already
assumes.

## Queries the rules need

All confirmed working against 7.0.2 (spike phase `queries`). `TypeResponse`
fields quoted from tsc/internal/api/proto.go.

| Need | Call | Result |
| --- | --- | --- |
| type at a token | `getTypeAtPosition {file, position}` | `TypeResponse` |
| many at once | `getTypesAtPositions {file, positions[]}` | `TypeResponse[]` |
| type of an expression | `getTypeAtLocation {location}` | `TypeResponse` |
| union members | `getTypesOfType {objectId}` | `TypeResponse[]` |
| nullability | `getNonNullableType {type}` | `TypeResponse` |
| call signatures | `getSignaturesOfType {type, kind}` | `SignatureResponse[]` |
| return type | `getReturnTypeOfSignature {signature}` | `TypeResponse` |
| parameters | `getParametersOfSignature {objectId}` | `SymbolResponse[]` |
| array-likeness | `isArrayLikeType {type}` | `bool` |
| assignability | `isTypeAssignableTo {source, target}` | `bool` |
| provenance | `getSymbolAtPosition {file, position}` | `SymbolResponse` |
| diagnostics text | `typeToString {type}` | `string` |

`TypeResponse` is flat and cheap: `{id, flags, objectFlags, isTupleType,
value, target, typeParameters, elementFlags, fixedLength, objectType,
indexType, checkType, extendsType, baseType, texts, freshType, regularType,
intrinsicName, aliasTypeArguments, aliasSymbol, symbol}`. Union and
intersection members are deliberately omitted and fetched separately, as are
template-literal member types.

Observed shapes from the fixture:

```
string                  {id:14, flags:32,        intrinsicName:"string"}
any                     {id:1,  flags:1,         intrinsicName:"any"}
string | undefined      {id:112, flags:134217728}            + getTypesOfType
"literal" (const)       {id:122, flags:1024, value:"literal", freshType:123, regularType:122}
42                      {id:86, flags:2048, value:42, freshType:87}
Promise<User>           {id:91, flags:1048576, objectFlags:1073741828, target:87, symbol:3}
(id: string) => Promise<User>   {id:86, flags:1048576, objectFlags:16, symbol:1}
```

### What "one hop" means concretely

The interned type row (docs/STRATEGY.md, "What we hold") is `TypeResponse`
plus exactly these follow-ups, issued only when the flags say they exist:

- `getTypesOfType` when `flags & (Union | Intersection)`.
- `getSignaturesOfType` + `getReturnTypeOfSignature` when the rule asks about
  a callee.
- `getTypeArguments` for a type reference (`objectFlags & Reference`).
- `getSymbolOfType` → `declarations` for provenance.

Anything deeper materializes on demand, as planned.

Guard every kind-specific call by flags. The server is not defensive:
`getTargetOfType` on a union panics inside Go (`Unhandled case in Type.Target`),
`getFreshTypeOfType` on a non-freshable type panics on an interface
conversion, and `getTypeParametersOfType` dereferences nil. The panic is
recovered into an error response and the connection survives (the spike kept
issuing requests afterwards), but a rule that trips one gets nothing back.

## Node handles

`NodeHandle` is a string, `"<index>.<kind>.<canonical path>"`, e.g.
`43.214.c:/dev/fastlint/tools/spikes/tsgo-api/fixture/src/main.ts`. Only the
index and path are read back; the kind is informational. The index is the
node's position in the file's node index table
(tsc/internal/api/session.go, `nodeHandleFrom` / `resolveNodeHandle`).

This matters because positions do not address expressions.
`getTypeAtPosition` runs `astnav.GetTouchingPropertyName`, which lands on the
token at that offset. Asking at the start of `fetchUser(id)` yields the type
of `fetchUser` (`(id: string) => Promise<User>`), not the call's
`Promise<User>`. Rules like `no-floating-promises` need the latter.

Two routes, both verified:

1. **Through the callee's signature.** `getTypeAtPosition` on the callee
   identifier, then `getSignaturesOfType` → `getReturnTypeOfSignature`. Works
   for the fixture, but returns the *declared* return type, so it is wrong
   wherever inference matters (generic calls, overload resolution).
2. **Synthesize the handle.** `getSourceFile {file}` returns the encoded parse
   tree, whose node table is exactly the table handle indices refer to. Decode
   it, find the node covering the span, and format the handle ourselves. The
   spike does this and `getTypeAtLocation` on the result returns
   `Promise<User>`.

Route 2 is the one to build. The encoded format suits us: a 44-byte header
(protocol version, an xxh3 content hash, section offsets) then 28 bytes per
node (`{kind, pos, end, nextSibling, parent, data, flags}`), flat, in source
order, indexed by exactly the number a handle carries. `pos`/`end` are UTF-8
byte offsets, matching our scanner. The fixture's 747-byte file encodes to
7127 bytes over 201 nodes; `getSourceFile` costs 0.5–1.3ms for it. Node-list
pseudo-nodes appear in the table with kind `0xFFFFFFFF` and must be skipped.
Full format documentation is in tsc/internal/api/encoder/encoder.go.

Handles survive `updateSnapshot` for an unchanged file (the spike mints one
against snapshot 1 and resolves it against snapshot 2) because the retained
program keeps the same `SourceFile` and therefore the same index table. An
edited file gets a fresh table, so its handles must be re-derived.

### Answering 2.3: can we reuse handles as our node key?

No, and we should not want to. A handle is an index into *tsgo's* parse tree,
so using it as our key would tie our node identity to tsgo's traversal order
and its encoder version — both of which the version drift above shows moving
underneath us (the encoded format is already at 5 here and 8 in the checkout).
It would also be wrong on its own terms: a handle addresses only nodes tsgo
parsed, and our tree carries error and missing nodes it does not.

Our node ids stay ours. We keep a per-file side table mapping our node id to
the tsgo index, built by walking the two flat arrays in source order — both
are in source order with byte-offset spans, so it is a linear merge, not a
search. The table is rebuilt whenever the file's content hash changes, which
is exactly when we would refetch types anyway.

Positions remain the fallback for token-level queries. One trap:
`getTypeAtPosition` converts its argument with `positionMap.UTF16ToUTF8`, so
its `position` is a **UTF-16 offset**, while node `pos`/`end` in the encoded
tree are UTF-8 byte offsets. The two agree only on ASCII. Handles avoid the
conversion entirely, which is a second reason to prefer them.

## Enum values for C++

The flag sets the responses carry ship as machine-readable TypeScript in the
npm package, under `node_modules/typescript/dist/enums/*.enum.js`: `TypeFlags`
(73 members), `ObjectFlags`, `SymbolFlags`, `ElementFlags`, `SyntaxKind`, and
about twenty more. Each is a plain reverse-mapped enum object, so a generator
can `import` it and emit a C++ header directly — no parsing of Go source, and
the values come from the same package version as the binary.

Spot-checked against the installed 7.0.2:

```
TypeFlags.Any 1   .Undefined 4   .Null 8   .String 32   .NumberLiteral 2048
TypeFlags.Object 1048576   .Union 134217728   .Intersection 268435456
ObjectFlags.ClassOrInterface 3   .Reference 4   .Anonymous 16
SyntaxKind.Identifier 79   .CallExpression 214   .AwaitExpression 224   .SourceFile 307
```

One gap: `CheckFlags` is not in the npm `dist/enums`, only in the Go
checkout's packages/typescript/src/enums/checkFlags.enum.ts. We read
`SymbolResponse.checkFlags` for readonly-property detection, so the generator
needs that one value set from elsewhere — either vendored into our repo with
the version it came from, or dropped until a rule actually needs it.

## Provenance

`SymbolResponse` is `{id, project, name, flags, checkFlags, declarations[],
valueDeclaration, parent, exportSymbol}`. Each declaration is a `NodeHandle`,
whose third field is the declaring file's canonical path — so
symbol → declarations → file is a string split, with no extra round trip.

That is enough for v2 per-type invalidation as docs/STRATEGY.md describes it:
a type row's provenance is the set of declaring files of its own symbol, its
alias symbol, and the symbols of its one-hop children. What it does *not* give
us is provenance for types with no symbol (unions of intrinsics, template
literal types); those inherit provenance from their members, which the one-hop
children already record.

## Edits

Measured on the fixture, with one file rewritten on disk:

```
updateSnapshot {fileChanges: {changed: [file]}}   3.6ms
first query on the changed file                   2.1ms
query in an untouched file                        0.3ms
```

The response's `changes` field is exactly the invalidation input we want:

```json
{"changedProjects": {"…/tsconfig.json": {"changedFiles": ["…/src/scratch.ts"]}}}
```

per project, plus `removedProjects`. Re-checking is lazy (an untouched file
still answers from the retained program), so watch mode costs a few
milliseconds per edit plus whatever the rules re-query.

`updateTemporarySnapshot`, which layers new text over one file without
touching disk, does not exist in 7.0.2. Until it ships, previewing a fixer's
output against the checker means writing the file.

## FS callbacks

`--callbacks=readFile,fileExists` makes the server ask us for file contents
over the same connection (message type 6 in the msgpack envelope, an ordinary
server-to-client request under JSON-RPC). Returning `null` falls back to
disk; `readFile` answers `{content: "…"}` to serve text.

Verified: with `main.ts` served from memory as
`export const fromCallback: 42 = 42;`, the type at that declaration comes back
as the literal `42` (`flags: 2048, value: 42`) rather than anything on disk.
The fixture took 63 `readFile` callbacks, of which 3 were ours and the rest
fell through to lib files.

This is worth taking: our cache already holds file contents keyed by hash, so
serving them avoids a second read and makes the server's view of the world
match the one our parser saw — which matters for the position/handle mapping
above. It also gives fixer preview a path once we want it.

## Measurements

`node tools/spikes/tsgo-api/main.ts bench`. Fixture is 2 files; the mid-size
project is C:/dev/TypeScript/packages/typescript, 108 files and ~35k lines.

Ranges are across three runs; run-to-run spread is larger than the difference
between transports on everything except the sequential median and the byte
counts.

| | fixture / msgpack | fixture / jsonrpc | midsize / msgpack | midsize / jsonrpc |
| --- | --- | --- | --- | --- |
| spawn → `initialize` | 11.6–13.3ms | 11.5–12.8ms | 11.5–11.9ms | 11.7–13.2ms |
| `updateSnapshot` (open project) | 40–43ms | 45ms | 70–76ms | 68–109ms |
| first type query | 1.7–1.8ms | 1.9–2.4ms | 1.3–1.5ms | 1.5–1.7ms |
| per query, sequential (median) | 0.079ms | 0.122ms | 0.070ms | 0.111ms |
| per query, `getTypesAtPositions` | 0.008ms | 0.010ms | 0.015ms | 0.007ms |
| per query, pipelined | — | 0.031ms | — | 0.025ms |
| bytes sent, whole run | 42KB | 75KB | 82KB | 147KB |

Reading these:

- Startup is ~12ms and project load is 40–110ms, so a CLI run pays roughly
  0.1s per project before any rule executes. Sequential per-project runs are
  affordable; the concurrency cap in docs/STRATEGY.md stands for memory
  reasons, not latency.
- The first query being 1.5ms on the mid-size project confirms the checker is
  lazy — it checks what the query needs, not the program.
- Plural endpoints are worth 10x over sequential singles, so the type layer
  should collect a file's positions and issue one `getTypesAtPositions`
  rather than one call per node. With `batchRequests` absent from 7.0.2, the
  plural endpoints are the only batching we have.
- `bytes sent` is dominated by repeating `{snapshot, project, file}` on every
  request. Another argument for the plural endpoints.

## What this changes in the plan

- Task 5.1 no longer needs a msgpack library, and does need a version gate.
- Task 5.1 gains a `getSourceFile` decoder and a node-index side table; the
  transport is settled as msgpack over stdio.
- Task 5.2's `TypeFacts` should take our node ids and resolve handles
  internally, never exposing positions to rules.
- `createProgram` and `batchRequests` come out of the task list for now; the
  snapshot model and the plural endpoints replace them.
