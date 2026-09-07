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

## One hop

- A type reached through `typeOf` or a signature's return type is interned
  with its children fetched: `getTypesOfType` for unions and intersections,
  `getTypeArguments` for references. Each child is interned shallowly, with
  its own symbol but without children of its own.
- A shallow row that is later reached directly is re-interned with children;
  the new row supersedes it in the session-id lookup and the old one stays as
  an orphan.
- Symbols are fetched with `getSymbolOfType` and `getAliasSymbolOfType` when
  a response names one the graph has not seen in this session.

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
| `isNullable` | row flags (`Undefined`, `Null`, `Void`) or any union member's |
| `isPromiseLike` | symbol or alias named `Promise`/`PromiseLike`, any union member, else a `then` property with a call signature (server) |
| `isArrayLike` | `isArrayLikeType` on the live id, cached per row |
| `unionMembers` | the children range of a union or intersection |
| `callSignatures` | `getSignaturesOfType`, each return type interned with one hop, parameters as symbols |
| `assignableTo` | `isTypeAssignableTo` on the two live ids |
| `symbolOf`, `declarationsOf` | the rows |

`lastError()` carries the most recent server or transport failure; node
queries return 0 instead of failing so a rule can keep walking.

## Not yet

- Rows persist through the store in docs/type-cache.md, but nothing evicts
  them yet; the LRU over rows is still open.
- Children interned as members are shallow, so a union inside a union hashes
  by its flags alone. Measure before deciding whether to deepen on demand.
