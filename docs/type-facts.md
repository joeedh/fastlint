# Type facts

What rules can ask about types, and how the answers are fetched and held.
The transport underneath is docs/tsgo-client.md; the design it implements is
the "Types" section of docs/STRATEGY.md. Task 5.2 of docs/tasklists/MASTER.md.
Code lives under `source/fastlint/types/`.

## Two layers

- `TypeGraph` (`type_graph.h`) is the interned store: type rows, symbol rows,
  a children table and a string table. Ids are row indices; 0 means none.
  Rows are appended and never removed, so an id stays valid for the graph's
  lifetime.
- `TypeFacts` (`type_facts.h`) answers questions for one `tsgo::Session`
  against one graph. It owns the per-file working set and issues every server
  request; rules never see positions, handles or tsgo ids.

## Rows and identity

- A `TypeRow` holds `flags`, `objectFlags`, `isTuple`, a `text` (intrinsic
  name or literal value), `symbol`, `aliasSymbol`, and a children range with
  a `ChildKind` (union members, intersection members, or type arguments).
- A `SymbolRow` holds the name, `flags`, `checkFlags` and the declaration
  node handles as strings. The handle's tail is the declaring file's
  canonical path, so provenance is a string split.
- The interning key is a 64-bit FNV-1a over every field above plus the
  children's hashes, so `Promise<void>` at ten thousand sites is one row and
  `string | undefined` is one row for the project. Member order matters:
  tsgo returns union members in a stable order, so no sort is applied.
- `sessionId` on a row is the live tsgo id from the snapshot that produced
  it. Server-backed questions (`isArrayLike`, `callSignatures`,
  `assignableTo`) need it; `clearSessionIds()` drops every one when the
  snapshot is released. Hashes never include it.

## Children

- A type reached through `typeOf`, a signature's return type, a property or
  an apparent type is interned with its children fetched: `getTypesOfType`
  for unions and intersections, `getTypeArguments` for references. The
  children are interned the same way, down to `kChildDepth` levels, so a
  row's hash carries the whole shape of the type. A generic instantiation is
  only told apart by its arguments, so a shallow `Array<T>` row would merge
  `Array<number>` with `Array<Promise<number>>`; leaves below the depth limit
  can still share a row that way.
- A shallow row that is later asked about (`unionMembers`, `typeArguments`,
  `isBuiltin`) is re-interned with children by `deepen`; the new row
  supersedes it in the session-id lookup and the old one stays as an orphan.
- Symbols are fetched with `getSymbolOfType` and `getAliasSymbolOfType` when
  a response names one the graph has not seen in this session.
- The pinned server omits `isTupleType`, so `isTuple` asks for a reference's
  target once (`getTargetOfType`) and reads its `Tuple` object flag.

## Working set

- `beginFile(file, path)` selects the AST and the path the server knows the
  file by. The first node query fetches `getSourceFile` and builds the
  `NodeIndexTable` that turns our nodes into handles.
- `typeOf(node)` caches per node, including a 0 for nodes the side table
  cannot place, so a miss costs one round trip and a later ask costs none.
- `prefetch(nodes)` sends one `getTypeAtLocations` for the nodes not yet
  cached. Rules that know their query set up front should call it once per
  file; the plural endpoint is ten times cheaper per type than singles.
- `endFile()` drops the node cache and the side table. The graph keeps its
  rows, which is the interning win across files.

## Questions

| Question | Answered from |
| --- | --- |
| `isAnyLike` | row flags (`Any`, `Unknown`) |
| `isAny`, `isUnknown` | the single row flag |
| `isErrorType` | `isAny` and the row's intrinsic name is `error`, so an unresolved name reads apart from a written `any` |
| `flags`, `objectFlags` | the row |
| `contextualTypeOf` | `getContextualType` at the node's location, so a value's expected type is read where the receiver has no type of its own |
| `resolvedSignature` | `getResolvedSignature` at a call, `new` or tagged template |
| `constructSignatures` | `getSignaturesOfType` with the construct kind, gathered like `callSignatures` |
| `targetOf` | `getTargetOfType` of a reference, cached; two references share a target when they instantiate one generic |
| `typeToString` | `getTypeToString` on the live id, cached per row |
| `awaitedType`, `awaitedDeep` | `getAwaitedType`, unwrapping a promise to `kChildDepth`; a mixed union awaits to 0 |
| `thenValueType` | the value a `then` callback receives, for a hand-written thenable |
| `isNullable` | row flags (`Undefined`, `Null`, `Void`) or any union member's |
| `isPromiseLike` | symbol or alias named `Promise`/`PromiseLike`, any union member, else a `then` property with a call signature (server) |
| `isArrayLike`, `isArray` | `isArrayLikeType` / `isArrayType` on the live id, cached per row |
| `isTuple` | the row's flag, else the reference target's `Tuple` object flag, cached |
| `isTypeParameter` | row flags |
| `unionMembers` | the children range of a union or intersection, fetched for a shallow row |
| `typeArguments` | the children range of a reference, fetched for a shallow row |
| `apparentType` | `getApparentType` for primitives and type parameters, cached; other types are their own |
| `constraintOf` | `getBaseConstraintOfType` of a type parameter, cached; 0 when unconstrained |
| `propertyType` | `getPropertyOfType` then `getTypeOfSymbol` |
| `numberIndexType` | the `number` entry of `getIndexInfosOfType` |
| `hasWellKnownSymbolProperty` | a `getPropertiesOfType` name shaped `__@name@<id>` |
| `isCallable` | `getSignaturesOfType` on some member of the apparent type, cached |
| `isThenable(type, n)` | a `then` property on some member of the apparent type with a call signature whose first `n` parameters are callable |
| `isBuiltin(type, name)` | the default library's symbol `name` on the type, an intersection member, every union member, a type parameter's constraint, or a class or interface base (`getDeclaredTypeOfSymbol` + `getBaseTypes`) |
| `isDefaultLibrary` | a declaration handle whose file is `lib.*.d.ts` |
| `callSignatures` | `getSignaturesOfType` over each member of the apparent type, so a union gathers the signatures of its callable members; each return type interned with children, parameters as symbols |
| `typeOfSymbol` | `getTypeOfSymbol` on the symbol's live id |
| `assignableTo` | `isTypeAssignableTo` on the two live ids |
| `symbolOf`, `symbolFlags`, `declarationsOf` | the rows |

`isBuiltin` mirrors typescript-eslint's `isBuiltinSymbolLike`: `Promise`
means the library's `Promise`, not any type with that name. The default
library test is by file name, since the server does not say which files are
its libs.

`strictOption(name)` reads a strictness flag from the project's compiler
options, so a rule whose behavior turns on `noImplicitThis` can ask. The
options come from `setCompilerOptions`, which `TypeSource` fills from
`parseConfigFile`. A flag serializes as a JSON boolean, and an unset one
falls back to `strict`, which defaults to true when no config is read.

`lastError()` carries the most recent server or transport failure; node
queries return 0 instead of failing so a rule can keep walking.

## Type sources

`TypeSource` (`type_source.h`) is what the linter holds instead of a
`TypeFacts`: `beginFile(file, path, text)` returns the facts for one file or
null with an error, and `endFile` releases them. `ProjectTypes` is the
implementation over one tsgo project.

- `open(tsconfig)` starts the server with the tsconfig's directory as its
  working directory (`tsc` resolved from there, then from the working
  directory) and opens the project. It also calls `parseConfigFile` to read
  the compiler options into `TypeFacts`, so `strictOption` can answer.
  `close` releases the snapshot, stops the server and clears the graph's
  live ids.
- The `FileProvider` serves a held file from memory and falls back to disk
  for anything it holds no override for. `parseConfigFile` routes the
  tsconfig read through the provider, so the fallback lets the server read a
  config and its `extends` chain that were never handed over.
- The text handed to `beginFile` is what the server checks. `ProjectTypes`
  is the server's `FileProvider`, serving every file it has been handed from
  memory, so a fixpoint pass sees its own edits and a test case need not be
  on disk. The text of every file seen is kept for the server's re-reads.
- A file whose text differs from what the server holds gets an
  `updateSnapshot` with it in `changed`; the graph's live ids are cleared,
  the old snapshot released and a `Session` and `TypeFacts` made for the new
  one. A file already on disk with the same bytes needs no update. The
  first sight of a file compares its text with the disk, so a served text
  that differs from the file (the typed rule tester) is a change too.
- A file the disk lacks is sent as `created` and opened (`openFiles`), which
  lands it in the server's inferred project; a configured project only
  globs the disk. An open file keeps its text until it is closed, so a later
  change closes it with the change and reopens it in a second update.
- `defaultProjectForFile` picks the project each file is queried in, so a
  tsconfig with references or an inferred-project file get the right
  program.
- `stats()` sums the `FactsStats` over every session; `rpcStats()` is the
  client's call and byte counts. `fastlint lint --type-stats` prints both.

## Not yet

- Rows persist through the store in docs/type-cache.md, but nothing evicts
  them yet; the LRU over rows is still open.
- `ProjectTypes` keeps the text of every file it has served for the whole
  run; a large project pays its source size in memory.
