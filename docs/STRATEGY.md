# fastlint — Strategy

A fast TypeScript/JavaScript linter in C++. Own parser, full-fidelity AST,
AST-level fixers, types from `tsgo` over its API protocol, type facts cached in
SQLite. Built on `vendor/litestl` for containers, allocation, tasks.

## Goals

- Fast: parse + syntactic rules at parser speed; type-aware rules only pay for
  the nodes they query; warm runs on unchanged files never touch `tsgo`.
- Ergonomic AST: one node representation, generic traversal for free, typed
  accessors as zero-cost views. Not 200 node classes.
- Fixers manipulate the AST, not text. Comments are never silently lost.
- Bounded memory regardless of program size — the type working set is
  proportional to distinct types queried, not to the program.

## Non-goals (v1)

- Own type checker. Types come from `tsgo`.
- Formatter. Fixer output is correct and comment-preserving; run a formatter
  afterwards for style.
- Incremental reparsing. Per-file reparse is sub-ms; revisit for editor mode.

## Components

```
scanner ──► parser ──► arena AST (per file) ──► rules ──► diagnostics
                          ▲   │                    │
             fixers ──────┘   └── printer          ▼
                                            type facts ◄──► tsgo (API mode)
                                                 │
                                              SQLite
```

## Parser

- Hand-written recursive descent, mode-aware scanner. Same shape as tsc, oxc,
  swc, Hermes. Chosen over tree-sitter because:
  - We emit our own arena nodes directly; tree-sitter's CST would need a full
    adapter copy per file and impose its node shapes.
  - Hand-written error recovery (sync on `;`, `}`, statement keywords) gives
    rules lintable nodes; tree-sitter's generic `ERROR` subtrees don't.
  - Faster (oxc-class parsers are ~3–5× tree-sitter on the same inputs).
  - tree-sitter-typescript's contextual hacks live in an external C scanner +
    GLR tables — harder to debug than the same logic in code.
- TS is wide, not deep: each wrinkle is a lexer mode or a lookahead. Treat as a
  checklist, not a research problem.

### Scanner

- Pull API. Parser drives it; `rescan*` family for context the scanner can't
  know: `rescanSlash` (regex vs divide), `rescanTemplateTail`,
  `rescanGreaterThan` (`>>`/`>>=` inside type args), `rescanJsxIdentifier`,
  JSX text mode.
- Contextual keywords always lex as identifiers; parser checks text: `type`,
  `declare`, `abstract`, `readonly`, `satisfies`, `as`, `accessor`, `using`,
  `out`, `in`, `override`, `async`, `get`/`set`, plus `await`/`yield` by
  function-context flags.
- Speculative parsing via scanner snapshot/rewind (`tryParse`) for
  arrow-vs-parenthesized-expression and generic-call-vs-comparison.
- Scanner produces byte offsets only, plus the line-start table as a
  side product. Line/col computed lazily.

### Testing

- Differential harness against `tsgo`'s AST over TypeScript's
  `tests/cases/conformance` corpus, plus oxc/tree-sitter corpora. Build this
  in week one; it turns "wide" into a grind-through list.

## Grammar tree — flat arena, full fidelity

The parser produces a read-only grammar tree. Rules do not see it; they see
the AST lowered from it (next section, and docs/ast-design.md).

### Nodes

- Per-file arena, `uint32_t` NodeId. One node struct:
  `{ kind, flags, parent, first_child, child_count, first_token, token_count }`
  (~24–32 bytes). Children are a range into one shared `Vector<NodeId>` per
  arena — nodes do not own inline child vectors (SBO in every node bloats the
  tree; use SBO in scratch/rule code instead).
- Span derives from the token range.
- `Error` and `Missing` node kinds from day one — linters parse half-typed
  code constantly.
- The grammar tree is never mutated. Its job is to own the tokens and
  trivia the printer copies from, and to stay valid for the life of the
  file so AST nodes can link back to it.

### Tokens & trivia (Roslyn / rowan model)

- Token array per file: `{ kind, offset, length, leading_trivia_start,
  leading_trivia_count }`. Every source byte is owned by exactly one token's
  trivia; nothing can be lost.
- Trivia = whitespace, newlines, comments, in a flat trivia array.
- Attachment rule (deterministic):
  - *Trailing trivia* of a token: everything after it up to and including the
    first newline. `foo(); // note` keeps the comment with `foo()`.
  - *Leading trivia*: everything else before the token. A comment block on its
    own line above a statement belongs to that statement.
- Always on (~12 bytes/token; 5k-line file ≈ 500 KB). Disable directives and
  JSDoc-aware rules need it regardless of fixing.
- Comments also indexed in a side table by position for directive lookup.

### Memory

- File ASTs are not the memory problem: bounded by an LRU of parsed files,
  reparse is fast. The type graph is (see below).

## AST — the rule-facing tree

Full design in docs/ast-design.md; the summary here is what the rest of
this document depends on.

- Lowered from the grammar tree in one pass. typescript-eslint kinds and
  accessor names with a short list of divergences, fixed child layout per
  kind declared once in `source/fastlint/ast/nodes.def`, views and name
  tables generated from it.
- A `Node` is `{ kind, flags, dirty, parent, GrammarRef grammar,
  Vector<Node *, 3> children }`, allocated from a per-file `util::Pool` and
  released with the file. `GrammarRef` names a tree and a node id, so a
  node can link into the file's grammar tree or into a template's.
- Typed accessors are views: `CallExpr(node).callee()` knows the child slot
  and costs nothing. Generic API (`children()`, `ancestors()`,
  `descendants(kind)`) works uniformly — rules don't reinvent traversal.
- Comments live in a side table keyed by `Node *`, attached at lowering.

## Fixers — AST mutation, not text edits

- Fixers edit the AST in place. `replace(old, fresh)` swaps the parent's
  child pointer; `insertBefore/After` edit a list slot; `set` fills an
  optional slot; `remove(node, CommentPolicy)` detaches. The grammar tree
  is untouched, so untouched code still prints from it verbatim.
- Every mutation sets `dirty` on the parent and each ancestor; the flag
  drives the printer.
- Templates are the preferred way to build replacement code:
  `Template::compile("$a?.$b?.($c)")` parses once and caches by string,
  `instantiate` deep-clones into the file's pool with the arguments
  reparented in, and `match` binds placeholders against an existing node.
  Instantiated nodes link to the template's grammar tree, so the printer
  emits the template author's tokens. Builders (`ast.call(callee, args)`,
  …) exist for the small cases and produce nodes with no grammar link.

### Comment policy on `remove`

- Default: leading trivia migrates to the next sibling (or previous sibling's
  trailing if last child). Node's own same-line trailing comment is dropped
  only under the default policy; rules may opt to keep it.
- Never silently discard a comment. Rules pass an explicit policy to deviate.

### Printer (no formatter)

- Clean node with a grammar link → emit the grammar node's source slice
  verbatim, trivia included. Untouched code carries zero risk.
- Dirty node with a grammar link → emit its own tokens (those not covered by
  a child) verbatim and recurse into children in layout order. The link may
  point at a template's grammar tree.
- Synthesized node with no link → per-kind template with sniffed style. Only
  builders produce these.
- Synthesized whitespace, minimal rules only: indentation copied from the
  nearest clean sibling's line; per-file style sniffed once (semicolons, quote
  char, tabs/spaces, trailing commas); list inserts copy the separator from
  a neighbour. Everything else is the formatter's job.

### Composing fixes

- Disjoint subtrees compose in one pass — no text-range overlap arithmetic.
- A fix is skipped when an earlier fix has dirtied or detached its target;
  the next pass finds it again. This replaces ESLint's text-range overlap
  arithmetic.
- Run to fixpoint (bounded, ESLint uses 10) with a reparse, relower and
  rebind between passes so positions and type queries are fresh.
- Side product: this is a codemod engine. Treat as a differentiator.

## Types — `tsgo`

### Transport

- Use `tsgo`'s API mode, not LSP. LSP has no "type at location"; hover is
  stringified markdown.
- Measured 2026-09-04 against the installed 7.0.2 binary — see
  docs/tsgo-api.md for the full surface, the numbers, and the spike that
  produced them.
- Transport is the default msgpack mode over stdio: a 3-element msgpack
  envelope around a JSON payload, ~60 lines to implement, no library. Half
  the bytes of `--async` JSON-RPC and slightly lower per-query latency.
- Structured, not stringly: `getTypeAtLocation(s)`, `getTypeOfSymbol`,
  `getTypesOfType` (union/intersection members), `getSignaturesOfType`,
  `getReturnTypeOfSignature`, `getParametersOfSignature`,
  `getNonNullableType`, `isTypeAssignableTo`, `isArrayLikeType`,
  `getSymbolAtPosition` → `declarations` (provenance). `TypeResponse` carries
  `flags`, `objectFlags`, `target`, type parameters, tuple data, literal
  value. Lifecycle: `initialize` → `updateSnapshot {openProjects}` → queries
  → `release {snapshot}`. There is no `createProgram` and no `batchRequests`
  in 7.0.2; the plural endpoints are the batching, and they are worth 10x
  over sequential singles.
- Positions address tokens, not expressions, and are UTF-16 offsets. Query by
  `NodeHandle` instead, synthesizing handles from the flat parse tree
  `getSourceFile` returns; its node spans are UTF-8 byte offsets like ours.
- Guard kind-specific calls by flags. The server panics internally on a
  mismatched type kind, recovers into an error, and answers nothing.
- One `tsgo` per tsconfig project. CLI mode: cap concurrent instances (2–3),
  run projects sequentially otherwise, kill on completion. Server mode: keep
  warm.
- Never ask for anything that makes `tsgo` check more than it would anyway
  (e.g. fully-expanded `typeToString` on huge unions).

### tsgo memory

- Not ours to fix — the checker's instantiation cache is GBs on large programs.
  Schedule around it as above.

### Type facts held locally, not the type graph

- Pull types lazily, only for nodes rules ask about (calls, member accesses,
  `await` operands, conditions, assignment RHS). Nothing else is fetched.
- Most rules stop at flags (`isNullable`, `isAnyLike`, `isPromiseLike`,
  `isEnumLiteral`, …): 4 bytes per queried node, no graph walk.
- Shallow interned type graph, one hop materialized:
  - `types { id, structural_hash, kind, flags, children_range }` — children
    are union/intersection members, type args, signature params/return,
    array element. Deeper levels materialize on demand.
  - Interned by structural hash: `Promise<void>` at 10k sites is one row;
    `string | undefined` is one row for the monorepo. This dedup is the
    memory win.
  - `symbols { id, name_id, decl_file_hash, decl_offset }` — stable across
    `tsgo` sessions, unlike in-process type ids.

### SQLite is the type graph; memory is a working set

- Tables: `types`, `type_children`, `symbols`, `node_types(file_hash, offset,
  type_id)`, `strings`, `files(path, content_hash, closure_hash)`.
- WAL mode, single writer, batched commits per file.
- Linting file F: load F's `node_types` and reachable type rows; flush on
  completion. Memory ≈ workers × one file's facts + LRU of hot type rows.
- Warm run on an unchanged file: `node_types` answers, `tsgo` never consulted.
- Also cache per-file rule results: unchanged file + unchanged closure ⇒
  replay diagnostics. This is the cache that makes reruns fast.

### Invalidation (tiered)

- **v1 — file closure.** Invalidate F's `node_types` when F or anything in
  its import closure changes. Key: `(content_hash, closure_hash,
  tsconfig_hash)`. Coarse (touching a shared `types.ts` refetches most of the
  repo) but correct; refetch is queries against an already-built program.
  Import graph comes from our parser.
- **v2 — per-type provenance.** Each type row records its set of
  `decl_file_hash`es (small sorted list or bitset over the project file
  table). Change one file → invalidate only types whose provenance includes it
  and the `node_types` pointing at them. Requires walking symbols during
  fetch; the shallow graph keeps that bounded.
- Structural hash includes `lib.*.d.ts` hashes, so a TypeScript upgrade
  invalidates everything. Acceptable and correct.

### Interaction with fixers

- Type queries are position-based against the pre-fix tree. Lint → fix →
  reparse → repeat. Fixed file = new content hash = new cache entry.

## litestl usage

- Pull in `platform/`, `util/`, `path/` only. Skip `math/`, `extern/eigen`,
  `io/` unless needed.
- **litestl containers over the STL, everywhere in our code.** `Vector`,
  `Map`, `Set`, `OrderedSet`, `Span`, `Array`, `BoolVector`, `string` from
  `litestl::util` — not `std::vector`, `std::unordered_map`, `std::span`,
  `std::array`, `std::string`. Reasons: SBO and no-throw semantics, one
  allocation path (the leak tracker sees everything), WASM-safe alignment,
  and a single place to fix performance. `std::` containers are acceptable
  only at boundaries that require them (SQLite/msgpack/N-API glue, OS APIs)
  and should not leak past the adapter.
- `util::Array` is (or will be) a thin alias of `std::array`. Still spell it
  `Array` — if we ever need our own, it's a one-line change instead of a
  repo-wide one.
- Use SBO containers for scratch buffers, scope chains, token lookahead,
  rule-local state. Not inside AST nodes (flat ranges into arena arrays
  instead).
- Allocation through `alloc::alloc/New` for the leak tracker — matters for
  long-running server mode.
- `util/task.h` for file-level parallelism: one arena per file, zero sharing.
- `binding/` TS generator is a strategic option for plugins (below).

## Open questions

- ~~Plugins~~ — resolved 2026-09-06: TS rules via litestl bindings (N-API
  and WASM) and native plugins via a generated C ABI, both over the AST,
  with the C++ views generated from the same `nodes.def` the host uses.
  See docs/ast-design.md "Interop" and "Plugins", MASTER.md task 7.
- **ESLint compatibility surface.** Support `// eslint-disable-*` comments and
  common rule names for adoption? Config format?
- ~~tsgo API protocol shape~~ — resolved: structured, msgpack over stdio.
  See docs/tsgo-api.md.
- ~~Is the protocol versioned or stable?~~ — resolved: neither. It negotiates
  nothing, ships under `./unstable/*`, and the released 7.0.2 binary differs
  from the checkout in 31 methods, several parameter names, and the encoded
  source file version. We pin a tsgo version and refuse others.
- **Structural hash stability** across `tsgo` versions. The version gate
  makes this safe (a version change drops the cache) but expensive; whether a
  hash can survive a tsgo upgrade is open.
- **Expression coverage of the handle route.** Synthesizing `NodeHandle`s
  from tsgo's parse tree works, but the node-id side table assumes our tree
  and tsgo's agree on spans. Error recovery is where they will not; decide
  what a rule gets when the mapping fails.

## Risks

- Parser breadth: ASI, regex/template/JSX lexer modes, arrow vs paren,
  `<T>` generics vs comparison vs JSX in `.tsx`, decorators, `satisfies`,
  `using`, `accessor`. Mitigated by the differential harness.
- `tsgo` API instability. Mitigated by isolating the transport behind one
  interface; flags-only fallback.
- Cache correctness. v1 file-closure invalidation is conservative by design;
  add a `--no-cache` escape hatch and a cache-consistency check mode.

## Phases

1. Scanner + parser + arena AST + differential harness. Syntactic rules only.
2. Tokens/trivia, fixer API, printer, fixpoint loop.
3. `tsgo` transport, flags-only type facts, SQLite cache with file-closure
   invalidation, rule-result cache.
4. Shallow type graph, per-type provenance invalidation.
5. Plugins / server mode (decide direction after phase 3).
