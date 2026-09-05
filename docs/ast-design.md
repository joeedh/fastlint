# AST design

The rule-facing tree. The parser produces a grammar tree (docs/STRATEGY.md,
MASTER.md task 3); this document describes the AST that is lowered from it,
the API rules and fixers use, and the printer and template machinery that
sit on it. Status: draft for review (MASTER.md 4.1).

## Goals

- Rules read like the typescript-eslint rules they are ported from. Kind and
  accessor names match typescript-eslint except where a listed divergence
  says otherwise.
- A node is a list of child nodes. Typed views are index arithmetic over that
  list and carry no data of their own.
- The AST is mutable. Fixers edit it directly with `replace`, `insert`,
  `remove` and builders, and the printer prints the edited AST.
- Untouched source prints byte-for-byte. Comments are never silently lost.
- Everything is reachable through pointers, integer kinds and child indexes,
  so the TS binding is generic.

## Two trees

- The grammar tree is the parser's output: a faithful parse tree over the
  token stream with recovery shapes, tokens and trivia. After parsing it is
  read-only. It answers "which tokens and trivia did this AST node come
  from" and feeds the differential harness. The append-only mutation API in
  STRATEGY.md moves to the AST and is removed from the grammar tree.
- The AST is lowered from the grammar tree in one pass. It has the
  typescript-eslint shape, fixed child layouts per kind, and a link back to
  the grammar node it came from.
- The lowering pass is the only code that knows both trees.

## Node

```cpp
struct GrammarRef {
  const syntax::GrammarTree *tree;  // the file's tree, or a template's
  syntax::NodeId id;
};

struct Node {
  NodeKind kind;
  uint16_t flags;
  uint16_t dirty;      // set on edit, propagated to every ancestor
  Node *parent;
  GrammarRef grammar;  // {nullptr, kNoNode} for synthesized nodes
  Vector<Node *, 3> children;
};
```

- Allocated from `util::Pool<Node, 256>` owned by the file. The pool is
  released as a unit when the file leaves the parsed-file LRU. Nodes are
  never freed individually; a removed node stays allocated until the file
  is dropped.
- Each node's byte span comes from its grammar node. A dirty node has no
  span until the printer recomputes it.
- Comments attach to nodes through a side table keyed by `Node *` (see
  Comments). They are not stored inline so the struct stays at one cache
  line.
- `GrammarRef` names the tree because template-instantiated nodes link into
  the template's grammar tree, not the file's.

## Child layout rule

Every kind has a fixed layout, declared once in `source/fastlint/ast/nodes.def`.

- Single children come first at fixed indexes. A required child is never
  null. An optional child holds `nullptr` when absent.
- At most one list per node, always the tail. A view exposes it as
  `span<Node *>` starting at a fixed index.
- A kind that needs two lists gets a wrapper node for one of them (this is
  why `ClassBody`, `TSInterfaceBody` and `TSTypeParameterDeclaration` exist
  as nodes; typescript-eslint has them too).
- `TemplateLiteral` and `TSTemplateLiteralType` interleave quasis and
  expressions in one list, `quasi, expr, quasi, …, quasi`. `quasis()` and
  `expressions()` stride by two. This keeps the printer order trivial.
- Anything that is a keyword or punctuation choice rather than a child is a
  flag or a small enum field, never a child: `var`/`let`/`const`, operator,
  `computed`, `optional`, `async`, `generator`, `static`, accessibility,
  literal kind, unary versus update operator.

## Kind taxonomy

Kinds follow typescript-eslint (ESTree plus the `TS*` and `JSX*` sets).
The `.def` file is authoritative; this section records the deliberate
divergences and the layouts that need explanation.

### Divergences from typescript-eslint

- **No `ChainExpression`.** `a?.b?.()` is a `MemberExpression` and
  `CallExpression` with the `optional` flag, and the chain's short-circuit
  extent is the outermost node with a flag set. `ChainExpression` exists to
  express that extent in a JSON AST and only gets in the way of rules.
- **No `TSTypeAnnotation` wrapper.** A `typeAnnotation` slot holds the type
  node directly. The wrapper's only content is the colon, which the grammar
  link already knows.
- **One `TSKeywordType`** with a keyword field instead of `TSStringKeyword`,
  `TSNumberKeyword` and the other fourteen.
- **No `ParenthesizedExpression` node.** The parenthesized flag is set on the
  inner node. Rules that care (no-extra-parens, fixers deciding whether a
  slot needs parens) use the grammar link. This matches typescript-eslint.
- **`Literal` is one kind** with a literal-kind field (string, number,
  bigint, boolean, null, regex) and `raw()` from the grammar link, as in
  ESTree.
- **`Error` and `Missing`.** An `ErrorNode` subtree lowers to one `Error`
  node with no children. A `Missing` child lowers to `nullptr` in its slot
  and sets `FLAG_INCOMPLETE` on the parent. Rules see complete productions
  or nothing.
- **Function-likes share one layout** so a single `FunctionLike` view works
  over `FunctionDeclaration`, `FunctionExpression`,
  `ArrowFunctionExpression`, `TSDeclareFunction` and
  `TSEmptyBodyFunctionExpression`. The kinds stay separate for
  typescript-eslint parity; the view is what rules use.

### Layout table (representative)

The list child is marked with `…`. `?` marks an optional slot.

| Kind | Children | Fields |
| --- | --- | --- |
| `Program` | `…body` | sourceType |
| `Identifier` | `typeAnnotation?` | name, optional |
| `PrivateIdentifier` | | name |
| `Literal` | | literalKind |
| `TemplateLiteral` | `…quasi/expression interleaved` | |
| `TemplateElement` | | cooked, raw, tail |
| `TaggedTemplateExpression` | `tag, typeArguments?, quasi` | |
| `ArrayExpression` | `…elements` (null for holes) | |
| `ObjectExpression` | `…properties` | |
| `Property` | `key, value` | kind (init/get/set), computed, shorthand, method |
| `SpreadElement` | `argument` | |
| `MemberExpression` | `object, property` | computed, optional |
| `CallExpression` | `callee, typeArguments?, …arguments` | optional |
| `NewExpression` | `callee, typeArguments?, …arguments` | |
| `ImportExpression` | `source, options?` | |
| `MetaProperty` | `meta, property` | |
| `UnaryExpression` | `argument` | operator |
| `UpdateExpression` | `argument` | operator, prefix |
| `BinaryExpression` | `left, right` | operator |
| `LogicalExpression` | `left, right` | operator |
| `AssignmentExpression` | `left, right` | operator |
| `ConditionalExpression` | `test, consequent, alternate` | |
| `SequenceExpression` | `…expressions` | |
| `AwaitExpression` | `argument` | |
| `YieldExpression` | `argument?` | delegate |
| `ArrowFunctionExpression` | `id?, typeParameters?, returnType?, body, …params` | async, expression |
| `FunctionExpression` | same as above | async, generator |
| `FunctionDeclaration` | same as above | async, generator, declare |
| `ClassDeclaration` | `id?, typeParameters?, superClass?, superTypeArguments?, body, …implements` | abstract, declare |
| `ClassExpression` | same as above | |
| `ClassBody` | `…body` | |
| `MethodDefinition` | `key, value` | kind (constructor/method/get/set), static, computed, accessibility, override |
| `PropertyDefinition` | `key, typeAnnotation?, value?` | static, computed, declare, readonly, accessibility, definite |
| `AccessorProperty` | `key, typeAnnotation?, value?` | as above |
| `StaticBlock` | `…body` | |
| `Decorator` | `expression` | |
| `VariableDeclaration` | `…declarations` | kind (var/let/const/using/await using), declare |
| `VariableDeclarator` | `id, init?` | definite |
| `ObjectPattern` | `typeAnnotation?, …properties` | |
| `ArrayPattern` | `typeAnnotation?, …elements` | |
| `RestElement` | `argument, typeAnnotation?` | |
| `AssignmentPattern` | `left, right` | |
| `TSParameterProperty` | `parameter` | accessibility, readonly, override |
| `ExpressionStatement` | `expression` | directive |
| `BlockStatement` | `…body` | |
| `IfStatement` | `test, consequent, alternate?` | |
| `ForStatement` | `init?, test?, update?, body` | |
| `ForInStatement` | `left, right, body` | |
| `ForOfStatement` | `left, right, body` | await |
| `WhileStatement` | `test, body` | |
| `DoWhileStatement` | `body, test` | |
| `ReturnStatement` | `argument?` | |
| `ThrowStatement` | `argument` | |
| `BreakStatement` | `label?` | |
| `ContinueStatement` | `label?` | |
| `LabeledStatement` | `label, body` | |
| `SwitchStatement` | `discriminant, …cases` | |
| `SwitchCase` | `test?, …consequent` | |
| `TryStatement` | `block, handler?, finalizer?` | |
| `CatchClause` | `param?, body` | |
| `ImportDeclaration` | `source, attributes?, …specifiers` | importKind |
| `ImportSpecifier` | `imported, local` | importKind |
| `ExportNamedDeclaration` | `declaration?, source?, attributes?, …specifiers` | exportKind |
| `ExportDefaultDeclaration` | `declaration` | |
| `ExportAllDeclaration` | `exported?, source, attributes?` | exportKind |
| `ImportAttributes` | `…attributes` | |
| `TSTypeReference` | `typeName, typeArguments?` | |
| `TSQualifiedName` | `left, right` | |
| `TSTypeParameterDeclaration` | `…params` | |
| `TSTypeParameter` | `constraint?, default?` | name, in, out, const |
| `TSTypeParameterInstantiation` | `…params` | |
| `TSAsExpression` | `expression, typeAnnotation` | |
| `TSSatisfiesExpression` | `expression, typeAnnotation` | |
| `TSNonNullExpression` | `expression` | |
| `TSTypeAssertion` | `typeAnnotation, expression` | |
| `TSUnionType` / `TSIntersectionType` | `…types` | |
| `TSFunctionType` / `TSConstructorType` | `typeParameters?, returnType?, …params` | abstract |
| `TSConditionalType` | `checkType, extendsType, trueType, falseType` | |
| `TSMappedType` | `typeParameter, nameType?, typeAnnotation?` | readonly, optional modifiers |
| `TSIndexedAccessType` | `objectType, indexType` | |
| `TSTypeLiteral` | `…members` | |
| `TSInterfaceDeclaration` | `id, typeParameters?, body, …extends` | declare |
| `TSTypeAliasDeclaration` | `id, typeParameters?, typeAnnotation` | declare |
| `TSEnumDeclaration` | `id, …members` | const, declare |
| `TSModuleDeclaration` | `id, body?` | kind (module/namespace/global), declare |

Import attributes are the one place `ImportDeclaration` would need a second
list; they go in an `ImportAttributes` wrapper node in an optional slot.

## Views

```cpp
struct View {
  Node *n = nullptr;
  explicit operator bool() const { return n != nullptr; }
  Node *node() const { return n; }
};

struct CallExpression : View {
  static constexpr NodeKind kind = NodeKind::CallExpression;
  Node *callee() const           { return n->children[0]; }
  Node *typeArguments() const    { return n->children[1]; }
  span<Node *> arguments() const { return tail(n, 2); }
  bool optional() const          { return n->flags & FLAG_OPTIONAL; }
};
```

- Views are value wrappers, not subclasses of `Node`. `node->as<T>()` checks
  the kind and returns a null view on mismatch. `node->is<T>()` is the
  boolean form.
- Views, `kindName()`, the per-kind child-name tables and the dump format are
  all generated from `nodes.def` by `tools/gen-ast.ts`.
- Union views cover kinds that share a layout: `FunctionLike` (the five
  function kinds), `ClassLike` (declaration and expression), `Loop`
  (`for`, `for-in`, `for-of`, `while`, `do-while`), `NamedDeclaration`.
- Convenience predicates live on `Node`, not on views, because rules apply
  them before they know the kind: `isIdentifier("name")`, `isLiteral()`,
  `isStringLiteral("x")`, `enclosingStatement()`, `enclosingFunction()`.
  There is no `skipParens()` because parens are a flag.

## Traversal and dispatch

- Generic: `children()`, `parent()`, `ancestors()`, `descendants()`,
  `descendants<T>()`, `firstChild<T>()`. All iterate the child lists;
  no visitor.
- The lowering pass fills `Vector<Node *> preorder` on the file. Rule
  dispatch is one linear scan of that vector against a kind-to-rules table.
  Each entry also records its subtree end index, so `descendants()` of a
  clean node is a contiguous slice of the vector.
- After an edit the preorder vector is stale for the dirty region. Dispatch
  within a pass is unaffected because fixes are collected during the pass
  and applied after it. Rules that run after fixes get a rebuilt vector.
- `match<T>(node, callback)` invokes the callback with the typed view when
  the kind matches; `switch` on `node->kind` with `as<T>()` is the general
  form.

## Binder

- A separate pass over the AST producing `Scope`, `Declaration` and
  `Reference` records in file-owned vectors, keyed by `Node *`.
- v1 scopes: module, function, block, class, catch, for-head, and TS
  namespace and enum. Hoisting for `var` and function declarations. Both
  the value and the type namespace are tracked so unused-import and
  unused-type rules work. TDZ is not modelled.
- Covers `no-unused-vars`, `no-shadow`, `prefer-const`, `no-undef`,
  `no-redeclare`, `no-use-before-define`.
- Binder output is not updated by fixers. A rule that runs after a fix in
  the same pass sees pre-fix scopes; the fixpoint driver rebinds after
  applying a pass's fixes.

## Mutation

Fixers edit the AST. The file owns a `Fixer` that exposes:

- `replace(Node *old, Node *fresh)`: swaps the parent's child pointer and
  reparents `fresh`.
- `insertBefore(Node *sibling, Node *fresh)` / `insertAfter`: only valid in a
  list slot; inserts into the parent's child vector.
- `remove(Node *node, CommentPolicy)`: only valid in a list slot or an
  optional slot. Removing from a required slot is an authoring error and
  asserts in debug.
- `set(Node *parent, int index, Node *fresh)`: sets an optional slot.
- Builders: `ast.identifier("x")`, `ast.call(callee, args)`,
  `ast.literal(...)`, and the template instantiation below. Builders return
  synthesized nodes with a null `GrammarRef`.
- Every mutation sets `dirty` on the parent and walks up setting `dirty` on
  each ancestor. Inserted subtrees are dirty relative to the file whether or
  not they carry a grammar link.
- A removed node is detached (`parent = nullptr`) but its comments stay in
  the side table until the policy moves or drops them.

Fixes are collected as closures during a rule pass and applied after it,
one at a time in source order. A fix whose target is already dirty from an
earlier fix in the same pass is deferred to the next pass. This replaces
ESLint's text-range overlap check with an ancestor-or-self check.

## Comments

- Lowering attaches every comment to exactly one AST node, as leading or
  trailing, using the trivia rule in STRATEGY.md: a comment on the same line
  after a node's last token trails that node; anything else leads the next
  node that starts after it. Comments before end-of-file trail `Program`.
- Storage is a side table `Map<Node *, CommentList>` on the file. Most nodes
  have no entry.
- `remove` with the default policy moves the node's leading comments to the
  next sibling (or to the previous sibling's trailing list when the node is
  last) and drops its same-line trailing comment. Rules pass a policy to
  keep the trailing comment or to drop everything, and every policy other
  than drop-all is lossless.
- `replace` moves the old node's comments to the new node.
- Directive comments (`eslint-disable`, `@ts-ignore`, `fastlint-disable`)
  are indexed by position in the grammar tree; rules never look for them by
  walking comments.

## Printer

The printer walks the AST and produces the file's new text.

- Clean node with a grammar link: emit the grammar node's source slice
  verbatim, trivia included. Untouched code carries zero risk.
- Dirty node with a grammar link: emit its own tokens (the tokens of its
  grammar node not covered by any child) verbatim, and recurse into
  children in layout order. Each child prints by the same rule. The link may
  point at a template's grammar tree, in which case the template author's
  spacing is what gets emitted.
- Synthesized node with no link: print from a per-kind template with
  sniffed style. Only builders produce these, and templates are preferred
  over builders precisely so this path stays small.
- List edits: an inserted element copies the separator and the whitespace
  after it from its nearest surviving neighbour. A removed element takes its
  preceding separator with it, or its following separator if it was first.
- Style is sniffed once per file: semicolons, quote character, tabs or
  spaces and indent width, trailing commas. Indentation for a synthesized
  line is copied from the nearest clean sibling's line.
- Comments print from the side table around their node. A comment whose
  node is dirty prints in the same relative position; a comment that was
  moved by a policy prints in its new place.
- The printer recomputes spans on dirty nodes as it goes, so diagnostics
  reported against post-fix nodes have positions.

## Templates

A template is a code snippet with placeholders, parsed once and instantiated
many times.

```cpp
static const Template kOptCall = Template::compile("$a?.$b?.($c)");

Node *n = kOptCall.instantiate(file, {{"a", obj}, {"b", prop}, {"c", arg}});
fix.replace(call, n);
```

### Compile

- `compile` runs the ordinary parser on the text and lowers it to a
  prototype AST. A placeholder is an `Identifier` whose name starts with
  `$`; no parser mode is needed because `$a` is a legal identifier. `$$`
  escapes a literal dollar.
- Compiled templates are cached by string. The template's source and grammar
  tree stay alive with the cache so instantiated nodes can link to them.
- Each placeholder records the category its position accepts, derived from
  the slot it landed in: expression, statement, type, property name,
  binding, or list. A placeholder followed by `...` in a list slot is a
  splice.

### Instantiate

- Deep-clones the prototype into the file's pool. Cloned nodes keep their
  `GrammarRef` into the template's grammar tree, which is what lets the
  printer emit the template's own tokens verbatim.
- Replaces each placeholder with the supplied node by reparenting it. The
  argument arrives with its own grammar link and its comments; nothing is
  copied or re-tokenized.
- Unwraps by position: a bare `$s` in statement position parses as an
  expression statement, and a statement argument replaces the whole
  statement; `$T` in type position parses as a type reference and a type
  argument replaces the reference; `$b` after a dot is a property slot and
  accepts only an identifier or private identifier. A splice placeholder
  takes a `span<Node *>` and inserts all of them.
- Checks that each argument's kind belongs to the placeholder's category.
  A mismatch is an authoring error: debug assert plus an error return, not
  a diagnostic.
- Parenthesizes on demand. When an expression argument has lower precedence
  than its slot requires (`a + b` into `$x * 2`), instantiate sets the
  parenthesized flag on the argument and the printer emits the parens.
- The result is dirty relative to the file and is passed to `replace`,
  `insert` or `set` like any other node.

### Match

- `template.match(node)` walks the prototype and the node together,
  ignoring trivia and the parenthesized flag, binding each placeholder to
  the corresponding subtree. A splice placeholder binds a span. It returns
  the bindings or nothing.
- A placeholder that appears twice must bind structurally equal subtrees.
- Rules that are "find this shape, rewrite to that shape" are a `match`
  followed by an `instantiate` with the same bindings.

## Ownership and lifetime

- One `AstFile` per source file owns the node pool, the preorder vector, the
  comment table, the binder output, and a pointer to the grammar tree it was
  lowered from. The grammar tree outlives the AST.
- Rules receive `Node *` and may hold them for the duration of a rule pass.
  They must not hold them across files or across the fixpoint driver's
  reparse.
- Files are linted in parallel with `util/task.h`, one `AstFile` per task,
  no sharing. Template caches are immutable after compile and shared
  read-only.

## Interop (task 7)

- The binding exposes `Node` with `kind`, `flags`, `parent`, `child(i)`,
  `childCount`, `span`, `text`, plus the generated kind-name and child-name
  tables. A generated `.d.ts` turns those into typed accessors on the TS
  side with no per-kind binding code.
- Templates, `match`, and the fixer API bind as they are; a TS rule calls
  `instantiate` with TS-side node handles.

## Plugins

Native rule plugins consume the host's AST through a generated C ABI. They
do not receive the grammar tree and do not build a tree of their own: a
plugin that lowered its own AST would have to re-implement comment
attachment and the printer contract, and could only return text edits.

### One definition file, three outputs

`nodes.def` generates the C header, the C++ views and the TS views. The C++
views are the same classes built-in rules use, so a built-in rule is a
plugin that happens to be statically linked.

- The C header (`fastlint/plugin/ast.h`) declares `fl_node` as an opaque
  type and accessor functions: `fl_node_kind`, `fl_node_flags`,
  `fl_node_parent`, `fl_node_child_count`, `fl_node_child`, `fl_node_text`
  (a `{ptr, len}` UTF-8 pair), plus the fixer, binder and template entry
  points. Calls across a shared-library boundary are plain calls, so a
  field read costs a few nanoseconds.
- The C header also declares the `Node` layout, so `kind`, `flags`,
  `parent` and the child array can be read directly. The accessor functions
  remain as the versioned path; a plugin chooses one or the other per
  build.
- The C++ views are written against a two-line accessor interface,
  `ast/access.h`: `child(n, i)`, `kind(n)`, `flags(n)`, `text(n)`. The host
  implements it with inline reads of `Node`; a plugin implements it with the
  C functions or the exported layout. The view code is byte-identical on
  both sides.

### Rules for the generated surface

- Views touch only the accessor interface. A view that reads `Node` directly
  stops compiling for plugins, so the generator does not emit such code and
  review rejects hand-written views that do.
- litestl does not appear in the plugin surface. Lists are a pointer and
  count (`std::span<Node *>`), strings are `{ptr, len}`, and the fixer,
  `match` and template functions take and return `fl_node *`. Inside the
  host these convert without cost.
- The header carries the layout version and a hash of `nodes.def`.
  `fastlint_plugin_init` refuses a mismatch, so a plugin built against a
  stale definition fails at load rather than misreading children.
- The C++ views are inline and identical in every translation unit that
  includes them, which keeps the one-definition rule satisfied across the
  host and any number of plugins.

### Protocol

- A plugin exports `fastlint_plugin_init(const fl_host_api *host)` and
  returns a rule table: rule name, the node kinds it listens to, and a
  callback taking `fl_node *`.
- The host loads plugins with `LoadLibrary` or `dlopen`, checks the version,
  and merges their rule tables into the same kind-to-rules dispatch used
  for built-in rules. Plugin callbacks see the identical tree, so comment
  policy, `match`, templates and the fixpoint loop apply unchanged, and
  fixes from plugins and built-in rules compose within one pass.
- Diagnostics go through the host, which owns positions and directive
  handling.
- Out-of-process isolation over the same C API is possible for untrusted
  plugins; it costs a serialization step per node and is not the default.

### Relation to the TS side

TS rules read the WASM heap through views generated from the same
`nodes.def` (docs/ts-binding-report.md). WASM rules, native plugins and
built-in rules are three consumers of one AST through interfaces generated
from one file.

## Anti-goals

- No per-node heap allocation outside the pool; no node-class hierarchy; no
  virtual dispatch; no visitor base class.
- No formatter. The printer synthesizes the minimum whitespace needed for
  edited regions and leaves everything else to the user's formatter.
- No AST mutation from rules outside a fix closure.

## Documents to update on sign-off

- CLAUDE.md "AST nodes are flat arena records addressed by `uint32_t` ids"
  describes the grammar tree only. Rewrite to say the grammar tree is a flat
  arena and the AST is pooled `Node` objects with an SBO child vector.
- STRATEGY.md "Fixers" and "Printer": move mutation to the AST, remove the
  append-only grammar-tree API, add the tree-qualified grammar link and
  templates.
- MASTER.md 4.2: add `nodes.def` + generator, lowering pass, comment side
  table, template compiler and cache, `match`, precedence-aware
  substitution; drop "append-only arena" and "dirty flags" from the grammar
  tree items.
- MASTER.md 7: add the generated C header, `ast/access.h`, the plugin entry
  point and version check, and the plugin-side build of the C++ views.

## Open questions

- Whether `Identifier` carries `typeAnnotation` as a child (typescript-eslint
  does) or the annotation lives on the declarator and parameter only. The
  table above follows typescript-eslint; the alternative is one fewer null
  slot on every identifier.
- Whether JSX lowers in v1 or is deferred with the JSX kinds present but
  unpopulated.
- Whether `dirty` should be a counter per subtree so the printer can skip
  clean subtrees under a dirty ancestor without walking them. The current
  design walks them and checks the flag, which is cheap enough to start.
