# litestl binding system: feasibility for the TypeScript rule API

Assessment of `vendor/litestl/binding` as the bridge between the C++ AST
(docs/ast-design.md) and TypeScript rules (MASTER.md task 7). Everything
below is read from the litestl sources at `120e54f` and from sculptcore's
N-API runtime at C:/dev/sculptcore/source/napi. Written 2026-09-04.

## Verdict

- Feasible for the WASM build, with a specific division of labour: litestl
  binds the handful of runtime objects (`Node`, `AstFile`, `Fixer`,
  `Template`, the enums) and our own generator emits the typed views.
- Feasible for a native N-API addon too, using sculptcore's runtime as the
  starting point, but with a cost profile that is the inverse of WASM: field
  reads become C++ callbacks. The report recommends WASM first and treats
  N-API as a follow-up gated on measurement.
- Two small litestl additions are needed regardless: a `string_view` binder
  and the nullable vector element flag. The second landed in litestl
  `0d0416a` and `120e54f` during this review.

## What the system is

- A descriptor graph. `Bind<T>()` dispatches at compile time through
  `Binder<T>` specializations to a `BindingBase` subclass: `Struct`,
  `Method`, `Constructor`, `Pointer`, `Reference`, `Array`, `Number`,
  `Boolean`, `Enum`, `Union`, `LiteralType`. Descriptors are allocated with
  `new` once and never freed, by design (litestl CLAUDE.md).
- A C ABI (`LSTL_*` in `binding/binding.cc`) that walks the graph and
  invokes method and constructor thunks through `void *self, void **args,
  void *ret`.
- A TypeScript runtime (`binding/typescriptRuntime/`) for WASM that reads
  descriptors and object fields directly out of the Emscripten heap.
- A TypeScript generator (`generators/typescript.cc`) that emits one
  interface per struct plus an index, and a Python `.pyi` generator that
  documents a ctypes runtime living in sculptcore.
- An N-API runtime in sculptcore (`source/napi/napi_runtime.cc`, 3250
  lines) that ports the TS runtime to C++ over the same descriptors.

## How the WASM runtime accesses objects

This is the part that decides whether a tree-walking API is fast.

- **Field reads are heap indexes, not calls.** `createBoundType` in
  `bind.ts` builds each bound class as JavaScript source and evaluates it.
  A numeric member becomes `this.wasm.HEAP32[(this.ptr + off) >> 2]`; a
  pointer member reads `HEAPPTR` and wraps the target. No FFI call is
  involved in reading `kind`, `flags`, `parent` or a child pointer.
- **Vector members are cached per owner.** The `VectorBinding` special
  generator stores one `BoundVector` per object and member key on the
  object itself. Elements are read through a `Proxy`; struct elements are
  cached per element address, pointer elements go through
  `getBoundPointer`.
- **Struct pointer reads allocate a wrapper every time.** `getBoundPointer`
  for a struct does `new cls(wasm, ptr, manager)` on every call; the
  `boundPointers` cache in `manager.ts` is commented out. A rule that walks
  `node.parent` or `node.children[i]` through bound classes creates one
  short-lived object per hop.
- **Null pointers come back as `undefined` or `null`.** `getBoundPointer`
  returns `undefined` for a zero pointer, and pointer members generate a
  `null` check. Vector elements of pointer type already handled zero at
  runtime; only the emitted type claimed non-null, which the nullable vector
  flag fixes.
- **Method calls are expensive.** `MethodType.invoke` allocates one heap
  block per argument, one for the pointer list and one for the return slot,
  invokes through `LSTL_Method_Invoke`, then frees them. The code carries a
  TODO to use scratch buffers. Struct arguments by value go through the
  struct's registered copy constructor.
- **Strings are opaque and read byte by byte.** `readLiteStlString` reads a
  `litestl::util::string` by `String.fromCharCode` per byte, which is Latin-1
  rather than UTF-8. There is no `string_view` or `const char *` binder.
- **No callbacks from C++ into TS.** Nothing in the descriptor model or the
  C ABI describes a function pointer, so C++ cannot drive a TS rule loop.

## How the N-API runtime accesses objects

- One JS class per struct, built once and cached in `classRefs_`.
- Every member read is a `memberGetter` callback: `napi_unwrap` the
  instance, add the member offset, and call `getBoundPointer`, which wraps
  the result. Struct and pointer results are new wrappers per call.
- Bulk data has a fast path: `VectorView` and `PointerBytes` hand JS an
  external `ArrayBuffer` over C++ memory, falling back to a copy when V8's
  sandbox forbids external buffers (Electron does).
- Pointers never cross into JS as numbers, by design; `ObjectAddress` exists
  only as an identity key.
- Vectors are handled by a hard-coded minimal surface (`VectorLength`,
  `VectorGet`, typed `assign` helpers) rather than the generic descriptor
  walk.

The consequence for fastlint is that under N-API a rule reading five fields
of a node makes five native calls and allocates up to five wrappers, while
under WASM it makes zero calls. The bulk fast path only helps for flat
arrays of scalars, which the AST is not.

## Build wiring

- WASM links through `build_wasm_post` in `build_files/macros.cmake`: a
  newline-separated symbol list becomes `-sEXPORTED_FUNCTIONS`, with
  `MODULARIZE`, `EXPORT_ES6` and `HEAPU8` exported. `binding/CMakeLists.txt`
  contributes the `LSTL_*` symbols through `lt_wasm_add_symbols`.
- `lt_native_export_symbols` mirrors the same list onto a native shared
  library via a generated `.def` on Windows or `--undefined` elsewhere.
- The Emscripten SDK is not part of fastlint's toolchain today. CLAUDE.md
  lists VS, cmake, ninja and clang-format only; sculptcore installs emsdk
  through its own `make.mjs install-emsdk` with a pinned commit. fastlint's
  `make.ts` would need the same.
- sculptcore's N-API build uses cmake-js to fetch the runtime headers and
  builds the addon with clang, targeting NW.js by default.

## What is proven by tests

- `tests/test_binding_system.cc` registers `Foo`, `VecTest`,
  `Vector<void *>`, `Vector<VecTest>` and, as of `0d0416a`,
  `NullablePtrVector<VecTest *>`; it builds and runs natively.
- `tests/test_binding_system.ts` (WASM only) constructs a `VecTest`, reads a
  nested vector element's fields, disposes it, constructs through a named
  constructor with numeric arguments, and calls one method. Snapshots cover
  the printed output and the sorted type list.
- Not covered by any test: enums and unions on the TS side, string members,
  pointer members that are null, method arguments of pointer or vector type,
  or the generated `.ts` output itself.

## Mapping to the AST design

What litestl binds:

- `Node`: `kind` (enum), `flags`, `dirty`, `parent` (nullable pointer),
  `grammar` (a small struct), `children` as `NullablePtrVector<Node *, 3>`.
  All of these are direct heap reads under WASM.
- `AstFile`: the preorder `Vector<Node *>`, `root`, and lookups into the
  comment table and binder output as methods.
- `Fixer` and `Template`: methods only. These are called rarely relative to
  tree reads, so `invoke`'s allocation cost is acceptable.
- Enums: `NodeKind`, the flag bits, `CommentPolicy`.

What our own generator emits, from the same `nodes.def` that produces the
C++ views:

- A TS view per kind that indexes `children` by slot. Views hold the raw
  node pointer as a number and read `HEAPPTR` and `HEAP16` directly, which
  sidesteps the per-hop wrapper allocation in `getBoundPointer`.
- Kind and child-name tables, and the `.d.ts` for the views.

What runs where:

- Rule dispatch is a TS loop over the preorder vector. There is no C++ to TS
  callback, and none is needed, because reading the vector is a heap read.
- Fixes are TS closures that call `Fixer` methods across the boundary.
- Template instantiation crosses once per instantiation.

## Gaps to close

1. **`string_view` binder.** Identifier names, literal raw text and
   `node.text()` need a `{data, size}` pair the TS side decodes with
   `TextDecoder` over `HEAPU8`. Small; also fixes the Latin-1 read for our
   own strings.
2. **Wrapper allocation.** Either restore the `boundPointers` cache in
   `manager.ts` with a `Map<number, WeakRef>` or, as recommended above, have
   the generated views bypass bound classes for node-to-node hops.
3. **Method call scratch buffers.** The TODO in `MethodType.invoke`. Only
   matters if fixer traffic turns out to be high.
4. **Emscripten in `make.ts`.** Install, pin, and add a `wasm` preset.
5. **Lifetime.** A TS rule can hold a node pointer past its `AstFile`. The
   design doc's rule that nodes do not outlive a rule pass needs enforcing
   on the TS side, or a per-file generation stamp that reads check.
6. **Tests.** The WASM test needs cases for enums, null pointers, strings and
   vector arguments before we depend on those paths.

## Recommendation

- v1 of TS rules targets WASM. Bind the runtime objects through litestl,
  generate the views ourselves, dispatch in TS.
- Record in STRATEGY.md that the N-API addon is deferred. When it is
  revisited, sculptcore's `NapiRuntime` is the base, and the AST would want
  a bulk path of its own: an external `ArrayBuffer` over the node pool's
  slabs so views can read fields without a callback, with the Electron
  sandbox fallback sculptcore already handles.
- Land the `string_view` binder in litestl alongside the nullable vector
  flag, with a test case in `test_binding_system.cc` and the TS test.
