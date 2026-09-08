# AST design

The rule-facing tree. The parser produces a grammar tree (docs/STRATEGY.md,
MASTER.md task 3); this document describes the AST that is lowered from it,
the API rules and fixers use, and the printer and template machinery that
sit on it. Status: signed off 2026-09-06 (MASTER.md 4.1); implementation is
MASTER.md 4.2.

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
  bool dirty;          // set on edit, propagated to every ancestor
  uint32_t flags;
  uint32_t data;       // enum fields, one per byte, in nodes.def order
  string_view text;    // name or raw text for kinds that carry one
  Node *parent;
  GrammarRef grammar;  // {nullptr, kNoNode} for synthesized nodes
  Vector<Node *, 3> children;
};
```

- `text` points into the source for lowered nodes and into the file's
  string arena for synthesized ones, so `Identifier` and `Literal` read
  their name without a lookup on either path.
- `GrammarRef` may narrow the link to a token range of the grammar node
  (`firstToken`, `tokenCount`). A `TemplateElement` stands for one template
  token, a method's `FunctionExpression` value for the tokens after the key,
  a `ClassBody` for the braces and members, and a `TSQualifiedName` built
  from a flat grammar `QualifiedName` for its prefix.
- A node's span (`start`, `end`) is the union of its own token range and
  its children's spans, computed at lowering. An `Identifier` with a type
  annotation therefore spans `a?: number` although its grammar node is only
  `a`, which is what typescript-eslint reports and what lets the printer
  print a clean node verbatim without losing the tokens between it and its
  children.

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
- **A parenthesized node spans its parentheses.** The node links to the
  outermost `ParenthesizedExpression` (or `ParenthesizedType`) so the
  parentheses are its own tokens and survive a reprint; typescript-eslint
  excludes them from the range.
- **Decorators live in a `Decorators` wrapper** on classes, methods,
  properties and parameter properties, so those kinds keep one list. A
  decorated plain parameter is wrapped in a `TSParameterProperty` with
  accessibility `none` and no flags, so its decorators have a home; the
  binder and the printer treat that wrapper like any other.
- **`namespace A.B.C` has a `TSQualifiedName` id** rather than nested
  module declarations, and a `declare global` block is a
  `TSModuleDeclaration` with kind `global`.
- **`null` in type position is `TSKeywordType`** with keyword `null`, as in
  typescript-eslint's `TSNullKeyword`.

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
  `isStringLiteral("x")` compares the raw text between the quotes and does
  not decode escapes.

## Traversal and dispatch

- Generic: `children`, `parent`, `ancestors()`, `descendants(fn)`,
  `descendants<T>(fn)`, `firstChild<T>()`, `enclosing(kind)`,
  `enclosing<T>()`, `enclosingStatement()`, `enclosingFunction()`. All
  iterate the child lists or the parent chain; no visitor. The `enclosing*`
  forms return strict ancestors, so a statement's enclosing statement is
  the one containing it.
- `nodes.def` assigns each kind its syntactic categories (`Statement`,
  `Expression`, `Type`, `Pattern`; a kind may hold several). They back
  `isStatement()` and friends on `Node`, and later the template category
  checks.
- Lowering fills `Vector<PreorderEntry> preorder` on the file. Each entry
  is the node plus its subtree end index, so a node's descendants are the
  contiguous slice between its index and its end.
- `Dispatcher` holds listeners keyed by kind, with an enter and an exit
  list per kind (`on`, `onExit`, and `on<T>` for every kind a view
  matches). `run(file)` is one linear scan of the preorder vector; exits
  fire when the scan reaches the subtree end, so enters and exits nest as
  in a recursive walk. Listeners are `function_ref`s that the rule keeps
  alive for the pass.
- After an edit the preorder vector is stale for the dirty region. Dispatch
  within a pass is unaffected because fixes are collected during the pass
  and applied after it. Rules that run after fixes get a rebuilt vector
  (`AstFile::buildPreorder`).
- `switch` on `node->kind` with `as<T>()` is the general form of typed
  dispatch inside a listener.

## Binder

- A separate pass over the AST (`ast/binder.h`, `bind(file, bindings)`)
  producing `Scope`, `Declaration` and `Reference` records in pools owned
  by a `Bindings` object, with lookups keyed by `Node *`: `scopeOf(node)`,
  `declarationOf(id)`, `referenceOf(id)`, plus `unresolved()`.
- v1 scopes: module, function, class, block, switch, for-head (only when
  the head declares with `let`, `const` or `using`), catch, static block,
  TS namespace, enum, and a type scope for the type parameters of
  interfaces, aliases, mapped, conditional and function types. `var` and
  function declarations hoist to the nearest variable scope (module,
  function, static block, namespace). A function body block opens no
  extra scope. A function or class expression's own name is declared
  inside its scope. TDZ is not modelled.
- Both the value and the type namespace are tracked (`Space`), so a
  `const I` and an `interface I` are two declarations chained by
  `nextSameName`, and a reference resolves against the space its position
  implies: type positions look up `Type`, expressions `Value`, and the
  leftmost part of a qualified name, `export { x }`, `export =` and
  `import x =` accept either. The name under `typeof` in a type looks up
  `Value` and falls back to a type-only import, with the `TypeQuery` flag
  on its reference. `infer I` declares in the scope of the conditional type
  it sits in, so the true branch sees it.
- A `Reference` records `Read`, `Write` and `Init` flags. A declarator
  with an initializer, a loop head binding and a defaulted parameter or
  pattern produce an `Init` write on the declared identifier, so
  `prefer-const` counts writes without special cases. Property names,
  labels, and import and export names are not references.
- Covers `no-unused-vars`, `no-shadow`, `prefer-const`, `no-undef`,
  `no-redeclare`, `no-use-before-define`.
- `dumpBindings` writes the scope tree for snapshot tests
  (tests/fixtures/binder/).
- `namespace A.B.C { }` declares `A` in the enclosing scope and opens one
  namespace scope per segment, so `B` lives in `A`'s scope and `C` in
  `B`'s. The outer scope is keyed by the declaration node and the inner
  ones by the segment identifiers.
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
- `addComment(Node *block, string_view text)`: puts a synthesized comment
  inside an empty braced node (a block, static block, module block or a
  switch with no cases), which prints between the braces; fails on a
  non-empty node. See Comments for how a synthesized comment is stored.
- `setData(Node *node, int index, uint8_t value)` / `setFlag(node, flag,
  on)`: changes an operator, a declaration kind or a flag in place. The node
  loses its captured layout and reprints from its kind template, because
  the token that changed sits in the node's own text; its children still
  print verbatim.
- Builders: `ast.identifier("x")`, `ast.call(callee, args)`,
  `ast.literal(...)`, and the template instantiation below. Builders return
  synthesized nodes with a null `GrammarRef`.
- Every mutation sets `dirty` on the parent and walks up setting `dirty` on
  each ancestor. Inserted subtrees are dirty relative to the file whether or
  not they carry a grammar link.
- A removed node is detached (`parent = nullptr`) but its comments stay in
  the side table until the policy moves or drops them.
- Parentheses belong to the slot, not the node. `detach` clears a moved
  node's `Parenthesized` flag and, for a clean node, shrinks its slice to
  the inside of the parentheses; every placement (`replace`, `set`,
  `insert*`, `append`, and the builders) sets the flag again where
  `needsParens(parent, index, child)` says the new slot needs it. A rule
  that moves `(A | B)` from an array type into `Array<...>` and back never
  touches parentheses itself.

Fixes are collected as closures during a rule pass (`Fix{target, apply}`)
and applied after it by `applyFixes`, one at a time in source order of
their targets. A fix is skipped when its target is dirty (an earlier fix in
the pass edited it or something below it) or detached (an earlier fix
replaced or removed it or an ancestor). Skipped fixes are not carried
over: the next pass runs the rules again over the reprinted file and
finds them afresh. This replaces ESLint's text-range overlap check. The
check is on the target alone rather than its ancestors because dirtiness
propagates to the root, so an ancestor check would serialize every fix.

`Fixer` is constructed over the file (`Fixer fixer(file)`); the builders
are its methods (`identifier`, `literal`, `member`, `call`, `unary`,
`binary`, `logical`, `expressionStatement`, `variableDeclaration`, and
`build(kind, {children})` for the rest). Synthesized nodes start dirty.

## Comments

- Lowering attaches every comment to exactly one AST node, as leading,
  trailing or dangling, using the trivia rule in STRATEGY.md. A comment on
  the same line after a node's last token trails that node; a list
  separator between them does not count, so `1, // one` trails the `1`.
  Otherwise the comment leads the outermost node starting at the next
  token. If no node starts there (the comment sits before a closing
  bracket) it trails the node that ended before it, and if there is no
  such node either (an empty block) it dangles on the innermost node that
  contains it. Comments before end-of-file trail `Program`.
- Storage is a side table `Map<const Node *, CommentList>` on the file,
  reached through `file.comments(node)`. Most nodes have no entry.
- `remove` with the default policy (`CommentPolicy::MoveLeading`) moves the
  node's leading and dangling comments to the next sibling (or to the
  previous sibling's trailing list when the node is last, or to the parent
  as dangling when it was the only element) and drops its same-line
  trailing comment; a trailing comment on its own line moves with the
  leading ones. Rules pass `KeepTrailing` to move the same-line trailing
  comment too, or `DropAll` to drop everything. `KeepTrailing` is
  lossless.
- `replace` moves the old node's comments to the new node.
- A `Comment` normally points at a source slice (`offset`, `length`). A
  fixer can add a synthesized one (`addComment`): its text lives in the
  file's arena, `synthetic` is set, and `offset` indexes that text through
  `file.syntheticComment`. The printer reads a synthetic comment from there
  rather than the source buffer.
- A node printed from its kind template has lost the text that held its
  children's leading and trailing comments and its own dangling ones, so
  the printer emits them itself: a child's before and after the child, a
  dangling one before the first child that follows it in the source.
- Directive comments (`eslint-disable`, `@ts-ignore`, `fastlint-disable`)
  are indexed by position in the grammar tree; rules never look for them by
  walking comments.

## Printer

The printer walks the AST and produces the file's new text.

- Clean node with a grammar link: emit the grammar node's source slice
  verbatim, trivia included. Untouched code carries zero risk.
- Dirty node with a grammar link: emit its own text verbatim and recurse
  into the current children in layout order. The own text is known from a
  `Layout` the fixer captured on the node's clean-to-dirty transition: the
  node's span cut at each child's span, in source order, with the slot each
  child occupied (`AstFile::captureLayout`). Nothing about the original
  children survives on the node itself, so the capture has to happen before
  the first edit. Each child prints by the same rule. The link may point at
  a template's grammar tree, in which case the template author's spacing is
  what gets emitted.
- Two layouts fall back to the kind template: one whose children overlapped
  (a shorthand property's key and value share a span) and one that has no
  place for a child the node now holds (an optional slot that was empty at
  capture, or a list that was empty).
- Synthesized node with no link: print from a per-kind template with
  sniffed style. Only builders produce these, and templates are preferred
  over builders precisely so this path stays small.
- List edits: an inserted element copies the separator and the whitespace
  after it from its nearest surviving neighbour. A removed element takes its
  preceding separator with it, or its following separator if it was first.
- Style is sniffed once per file: semicolons, quote character, tabs or
  spaces and indent width, trailing commas. Indentation for a synthesized
  line is copied from the nearest clean sibling's line.
- Comments stay in the text they came from: a clean node's slice and a
  dirty node's own text both carry their trivia, so most comments never
  touch the side table on the way out. A `remove` marks the removed node's
  comments as dead ranges the printer skips when copying text (with the
  spaces before them and, for a comment on its own line, the line break
  after), and flags the copies it moved to a neighbour as `moved`; the
  printer emits moved comments from the side table before or after their
  new node.
- The printer recomputes spans on dirty nodes as it goes, so diagnostics
  reported against post-fix nodes have positions.

## Templates

A template is a code snippet with placeholders, parsed once and instantiated
many times (`ast/template.h`).

```cpp
static const Template *kOptCall = Template::compile("$a?.$b?.($c)");

Node *n = kOptCall->instantiate(file, {{"a", obj}, {"b", prop}, {"c", arg}});
fix.replace(call, n);
```

### Compile

- `compile` runs the ordinary parser on the text and lowers it to a
  prototype AST. A placeholder is an `Identifier` whose name starts with
  `$`; no parser mode is needed because `$a` is a legal identifier. A name
  starting with `$$` is a literal identifier without the first dollar.
- Compiled templates are cached by mode and text and live for the process.
  The template's source and grammar tree stay alive with it so instantiated
  nodes can link to them.
- The mode picks the prototype. `Auto` takes the expression of a lone
  expression statement, a lone statement of any other kind, or the whole
  program. `Expression` wraps the text in parentheses so `{ a: 1 }` parses
  as an object literal. `Type` parses the text as a type alias's right side.
  `Statements` keeps the program and instantiates through `instantiateAll`.
- Each placeholder records the slot its position accepts, derived from its
  parent: expression, statement, type, name, property, property name,
  pattern, assignable, or any (an unchecked list such as object members).
- A name ending in a second `$` (`f($args$)`, `{ $body$ }`) is a splice
  that binds a list of nodes. It is only legal as a list element, or as
  the expression statement of a statement list. The design first used a
  `...` suffix, which the parser rejects; the trailing dollar parses as
  an ordinary identifier.
- A parse error or a misplaced splice makes `ok()` false; `instantiate`
  and `match` then fail without touching anything.
- `compile(text, mode, true)` parses the text with JSX enabled. Tag and
  attribute names are `JSXIdentifier`s, not placeholders; `{$x}` inside
  an element is an expression slot.

### Instantiate

- Checks every binding first: each placeholder has one, a splice's nodes
  and a single argument fit their slot. A mismatch is an authoring error
  and returns null with nothing moved.
- Deep-clones the prototype into the file's pool. Cloned nodes keep their
  `GrammarRef` into the template's grammar tree, which is what lets the
  printer emit the template's own tokens verbatim. The parent of each
  placeholder captures its layout before the swap, so the template's text
  around the argument prints unchanged.
- Replaces each placeholder with the supplied node by reparenting it. The
  argument is detached from its old parent, arrives with its own grammar
  link, and nothing is copied or re-tokenized. A name bound to several
  occurrences moves into the first and is deep-cloned into the rest, which
  is how shorthand `{ $a }` fills both key and value.
- Unwraps by position: a bare `$s` in statement position parses as an
  expression statement, and a statement argument replaces the whole
  statement while an expression argument keeps the wrapper; `$T` in type
  position parses as a type reference and a type argument replaces the
  reference while an identifier replaces the name; `$b` after a dot is a
  property slot and accepts only an identifier or private identifier. A
  template that is a bare placeholder returns the argument itself.
- Parenthesizes on demand. `needsParens` (`ast/precedence.h`) compares the
  argument's binding strength with its slot: `a + b` into `$x * 2`, an
  `||` under `??`, a unary operand left of `**`, a call as the callee of
  `new`, a sequence in an argument list, a union under `[]`. Instantiate
  and the `Fixer` builders set the parenthesized flag; the printer only
  honours it.
- The result is dirty relative to the file and is passed to `replace`,
  `insert` or `set` like any other node.

### Match

- `match(node, args)` walks the prototype and the node together, ignoring
  trivia and the parenthesized flag, binding each placeholder to the
  subtree that fills its slot. A statement placeholder binds any statement,
  a type placeholder any type. A splice binds the span between the list
  elements before and after it.
- A placeholder that appears twice must bind structurally equal subtrees
  (`equivalent`: kind, flags, data, text and children).
- Rules that are "find this shape, rewrite to that shape" are a `match`
  followed by an `instantiate` with the same `TemplateArgs`.

## Fixpoint driver

`runToFixpoint` (`ast/fixpoint.h`) owns the loop that turns proposed fixes
into final text.

- Each pass parses the current text, lowers it, binds it and hands a `Pass`
  (grammar tree, `AstFile`, `Bindings`, pass index, fix list) to the
  callback. The rule layer fills the fix list; nothing in the driver knows
  about rules.
- The fixes are applied in source order by `applyFixes`, so a fix whose
  target an earlier fix dirtied or detached waits for the next pass, where
  the reprinted and reparsed file shows it whether it still applies.
- The file is printed and the loop repeats on the printed text. It stops
  when a pass proposes nothing, when the applied fixes leave the text
  unchanged, or after `maxPasses` (10 by default, as in ESLint), and the
  report says which.
- A pass whose output fails to parse is thrown away: the report marks
  `reverted` and the text from before that pass stands. A file that already
  has syntax errors gets one pass for diagnostics and no fixes.
- Bindings, positions and trees are rebuilt every pass, so a rule may hold
  `Node *` and `Declaration *` within a pass and never across passes.

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

## Decisions at sign-off

- `Identifier` carries `typeAnnotation` as an optional child, matching
  typescript-eslint. The null slot on every identifier costs one pointer and
  keeps ported rules unchanged.
- JSX lowers to the typescript-eslint kinds with these choices: a
  self-closing tag is a `JSXElement` whose `openingElement` shares its
  grammar node and span; `this` in a tag name is a `JSXIdentifier` spelling
  `this`; `JSXText` carries the raw text; an empty `{}` holds a
  `JSXEmptyExpression` spanning the text between the braces, so a comment
  there survives reprinting; an unclosed element has a null
  `closingElement` and the `incomplete` flag.
- `dirty` stays a flag. The printer walks clean subtrees under a dirty
  ancestor and checks the flag on each node. Promote it to a per-subtree
  counter only if the printer shows up in a profile.
