# Parser

How the scanner and the recursive-descent parser produce the grammar tree,
and how to work on them. docs/STRATEGY.md holds the reasoning behind the
design; this document describes what is built. Status and the remaining
gaps are tracked in docs/tasklists/MASTER.md task 3.

## Layout

| File | Contents |
|---|---|
| source/fastlint/syntax/tokens.def, tokens.h | `TokenKind`, the `Token` record |
| source/fastlint/syntax/scanner.h, scanner.cc | The scanner: modes, rescans, snapshot and rewind |
| source/fastlint/syntax/unicode.cc | ID_Start / ID_Continue tables for non-ASCII names |
| source/fastlint/syntax/node_kind.def, tree.h, tree.cc | `NodeKind`, `NodeFlags`, the `Node` record, `GrammarTree` |
| source/fastlint/syntax/diagnostics.h | `Diagnostic` and the ordered `Diagnostics` list |
| source/fastlint/syntax/parser.h | The `Parser` class and `dumpTree` |
| source/fastlint/syntax/parser/internal.h | Helpers shared by the parser's translation units |
| source/fastlint/syntax/parser/parser.cc | Token access, `expect`, ASI, diagnostics, speculation, `parseFile` |
| source/fastlint/syntax/parser/lists.cc | Sync sets and the recovery step for every list production |
| source/fastlint/syntax/parser/statements.cc | Statements, blocks, variable declarations, labels |
| source/fastlint/syntax/parser/declarations.cc | Functions, classes, interfaces, enums, modules, imports and exports, decorators |
| source/fastlint/syntax/parser/expressions.cc | Expressions, arrow functions, templates, object and array literals |
| source/fastlint/syntax/parser/types.cc | The type grammar |
| source/fastlint/syntax/parser/jsx.cc | JSX elements, fragments, attributes and children |
| source/fastlint/syntax/parser/dump.cc | The s-expression dump |

## Entry point

```cpp
syntax::Diagnostics diagnostics;
syntax::GrammarTree tree;
syntax::Parser parser(std::string_view(bytes), options, diagnostics);
parser.parseFile(tree);
```

`Parser::Options` has two flags. `javaScript` (for `.js`, `.jsx`, `.mjs`,
`.cjs`) turns on the scanner's JS lexing and the JS-only statement forms and
turns off type assertions. `jsx` (for `.tsx` and `.jsx`) makes `<` in
expression position start a JSX element. The CLI derives both from the file
extension in source/cli/files.cc.

The parser always terminates and always consumes every token. It reports
diagnostics but never fails: a file full of garbage still yields a
`SourceFile` whose children are `ErrorNode`s.

## The grammar tree

The tree is a faithful parse tree: every token of the file belongs to exactly
one node, keywords and punctuation included, and trivia (whitespace, comments,
shebang) hangs off the tokens. Nothing is dropped, so fixers can edit the tree
and print it back without losing anything (docs/STRATEGY.md "AST").

### Nodes

A `Node` is a flat record addressed by a `NodeId` (`uint32_t`):

| Field | Meaning |
|---|---|
| `kind` | One of node_kind.def |
| `flags` | `NodeFlags` bits, below |
| `parent` | `kNoNode` for the root |
| `firstChild`, `childCount` | A range into the tree's shared child-id vector |
| `firstToken`, `tokenCount` | A range into the tree's token array; zero-length for missing nodes |

Nodes hold no pointers, no inline containers and no virtual dispatch. Children
of one node are contiguous in the child-id vector, so `tree.children(id)`
returns a span. Modifier keywords do not get nodes of their own: `export`,
`async`, `static`, `readonly`, `public` and the rest become flags on the
declaration they modify, and the modifier token stays inside that
declaration's token range. The one exception is `Decorator`, which is a node
because it owns an expression.

The flags are:

| Flag | Set on |
|---|---|
| `FLAG_MISSING` | A node synthesized for something the source lacks; its token range is empty |
| `FLAG_ERROR` | An `ErrorNode` wrapping a token the parser could not place |
| `FLAG_AMBIENT` | Declarations under `declare` or inside an interface body |
| `FLAG_EXPORTED`, `FLAG_DEFAULT` | `export`, `export default` |
| `FLAG_ASYNC`, `FLAG_GENERATOR` | `async`, `*` |
| `FLAG_CONST`, `FLAG_READONLY`, `FLAG_STATIC`, `FLAG_ABSTRACT`, `FLAG_OVERRIDE`, `FLAG_ACCESSOR` | The modifier keywords |
| `FLAG_PUBLIC`, `FLAG_PRIVATE`, `FLAG_PROTECTED` | Accessibility on members and constructor parameters |
| `FLAG_OPTIONAL` | `x?` on members, parameters, tuple elements and index signatures |
| `FLAG_REST` | `...x` on parameters, spreads, tuple elements and `{...x}` in JSX |
| `FLAG_OPTIONAL_CHAIN` | `a?.b`, `a?.[b]`, `a?.()` |
| `FLAG_ASI` | A statement that ended by automatic semicolon insertion rather than a written `;` |
| `FLAG_USING`, `FLAG_AWAIT` | `using` lists, `await using` lists, `for await` loops |
| `FLAG_TYPE_ONLY` | `import type`, `export type` |

`FLAG_ASI` lets a rule distinguish a written `;` from an inserted one from a
missing one (a missing one produces a diagnostic and no flag).

### Shapes

Node shapes follow tsgo's parser where the two disagree only in convenience,
because the differential harness compares against tsgo (docs/parser-conformance.md).
The deliberate differences are handled by the harness's normalizer, not by
the parser, and are the following:

- Type parameter and type argument lists are nodes (`TypeParameters`,
  `TypeArguments`) rather than bare child runs.
- `ExportDeclaration` wraps an exported declaration; tsgo flags the
  declaration instead.
- A `QualifiedName` is flat (`A.B.C` is one node with three identifiers), as
  is a dotted `module A.B.C` name.
- Keyword types (`string`, `void`, `bigint`, …) are one `KeywordType` node.
- `MissingNode` and `ErrorNode` exist only here.

Two conventions are worth knowing when reading dumps. A binary expression is
built after both operands, so `beginNode` for it takes the left operand's
first token and the left operand is added as a child of a node that was
begun later; `GrammarTree::addChild` allows this because an ended node can be
attached to any open node. The callee of a dynamic `import(…)` is an
`ImportKeyword` node, so the call looks like any other call.

### Tokens and trivia

The scanner works on UTF-8 bytes and reports byte offsets only. A `Token`
records its kind, offset and length, the index and count of its leading
trivia, and whether a line break precedes it. Trivia runs (whitespace,
newline, single-line comment, multi-line comment, shebang) live in their own
array. The line-start table is a by-product of scanning; line and column are
computed from it on demand (`GrammarTree::lineOf`).

Contextual keywords lex as their own `TokenKind` (`TypeKeyword`,
`DeclareKeyword`, `AsyncKeyword` and so on). Whether one is a keyword or a
name in a given position is the parser's decision, through
`isAlwaysIdentifier` (keywords that may always be names) and
`isBindingIdentifier` (adds `let`, `yield`, `abstract`, `public`, `private`,
`protected`, which are reserved only in strict mode or by context).

## The scanner

The parser pulls tokens one at a time with `scanOne()` and inspects
`current()`. Some tokens cannot be lexed without grammatical context, and for
those the parser asks for a rescan, which rewinds to the token's own start
and lexes it again under a different rule:

| Situation | Call |
|---|---|
| `/` or `/=` in expression-start position | `rescanSlash()`, which fails and leaves division when no regex terminates on the line |
| `>>`, `>>=`, `>>>` closing a type argument list | `rescanGreaterThan()` splits off one `>` |
| `}` ending a template substitution | `rescanTemplateTail(tail)` lexes `}` plus the following text as a middle or tail token |
| A name in a JSX tag or attribute | `rescanJsxIdentifier()` allows `-` and joins escaped segments |
| A string as a JSX attribute value | `rescanJsxAttributeString()` allows newlines and takes no escapes |

Modes handle the cases where the next token (rather than the current one)
needs a different rule. `setMode()` persists until changed; the parser's
`scanNext(mode)` helper in jsx.cc scans one token under a mode and resets to
`Normal`. `JsxText` lexes children text up to `<` or `{` and skips no trivia,
`SingleGreaterThan` stops a `>` from merging into `>=` or `>>` inside a tag,
and the template modes do the same for `}`.

The scanner never produces a zero-length token except `EndOfFile`; a
character that cannot start anything becomes a one-byte error token, which
is what keeps every parser loop moving.

## Recursive descent

Every production returns a `NodeId`. A production begins its node with
`beginNode(kind, firstToken)`, adds children with `addChild`, and ends it with
`endNode(id, pos())`, where `pos()` is the index of the current (unconsumed)
token, so the node's range ends just before it. `beginNode` and `endNode`
nest strictly. A node may be retagged after it began (`opening` becomes
`JsxSelfClosingElement` when `/>` turns up) and flags may be set at any time
before the parent is ended.

Token access is through `is()`, `eat()` and `expect()`. `expect` reports
"'X' expected" with TS's code and returns `kNoToken` without consuming
anything, so a missing `)` costs one diagnostic and nothing else.
`missingNode(kind, at)` creates a zero-length node with `FLAG_MISSING` where a
name or expression had to be.

Binary expressions use precedence climbing over `binaryPrecedence` in
internal.h. `in` has precedence 0 while `m_disallowIn` is set, which is how a
`for` head stops at `in`; brackets, arguments and blocks reset the flag.

### Lookahead and speculation

One-token lookahead is `peekKind()` and two tokens is `peekKind2()`; both
scan ahead and rewind. Anything wider uses a `Mark`:

```cpp
Mark mark = begin();
… parse tentatively …
rollback(mark);   // or keep going: commit() is a no-op
```

A mark captures the scanner state (position, mode, token and trivia counts),
the tree's build state (node count, pending children, open-node marks) and
the diagnostic count. `rollback` truncates all three, so a failed attempt
leaves no nodes and no diagnostics behind. Because the scanner mode is part
of the state, probes are safe inside JSX and templates.

Speculation is used for the decisions that need it: arrow function versus
parenthesized expression (`isArrowHead`, guarded by the cheap `mayBeArrowHead`
pre-check and memoized in `m_notArrowHead` by token index so nested
parentheses stay linear), function type versus parenthesized type (the same
routine with `typeContext`), and type arguments in expression position
(`tryParseTypeArguments`, accepted only when `canFollowTypeArguments`).

The `await` and `yield` decisions follow tsgo: outside their contexts they are
still expressions when a name, keyword or literal follows on the same line
(`operandFollowsOnLine`).

### Context flags

`m_allowAwait`, `m_inYieldContext`, `m_inIteration`, `m_inSwitch`,
`m_ambient`, `m_noConditionalType` and `m_disallowIn` describe the enclosing
constructs. Set them with `detail::FlagScope`, which restores the previous
value when the scope ends, and set them before parsing the parameter list of
a function so that parameter defaults follow the function's own context.

### Lists and error recovery

Every repetition goes through `parseList` or `parseDelimitedList`
(internal.h) with a `ListKind`. The loop owns progress: it parses an element
while the current token starts one, and otherwise consults the sync sets in
lists.cc.

- `isListElement(kind)` says whether the current token can start an element.
- `isListTerminator(kind)` says whether it ends the list.
- Any other token gets the list's TS diagnostic ("Declaration or statement
  expected.", "Property or signature expected.", …). Then, if an enclosing
  list on the stack (`m_lists`, one bit per kind) would accept the token as
  an element or a terminator, the current list ends there without consuming
  it. Otherwise the token is wrapped in an `ErrorNode` and skipped.

Productions never unwind. A statement that meets `}` in the middle of an
expression reports once, ends, and lets the block's list see the `}`.
Diagnostics at the same offset as the previous one are dropped in
`Parser::error`, so a token that fails several nested productions is reported
once. The delimited variant accepts a trailing comma and reports "',' expected"
when two elements touch.

The invariants that recovery must keep are the ones the fuzzer checks
(source/cli/fuzz.cc): tokens in order and inside the source, node token
ranges inside the token array, parent links that point back, and no open
node left behind. A production that reports an error and breaks out of a
loop must still end every node it began.

### JSX

In `.tsx`, `<` in primary position parses a JSX element or fragment unless
`isJsxGenericArrowHead` sees `<T,>` or `<T extends U>` (with `U` not
followed by `=`, `>` or `/`), in which case it is a generic arrow. Type
assertions `<T>x` do not exist in `.tsx`.

Shapes match tsgo: `JsxElement(JsxOpeningElement, children…,
JsxClosingElement)`, `JsxSelfClosingElement`, `JsxFragment(JsxOpeningFragment,
children…, JsxClosingFragment)`. A `JsxAttributes` node is always present,
empty or not. Every text run between children, whitespace-only ones included,
is a `JsxText`. `{expr}` is a `JsxExpression`; `{...expr}` carries `FLAG_REST`.
Tag names are `Identifier`, `PropertyAccessExpression`, `JsxNamespacedName`
or `ThisExpression`. A stray `}` or `>` in children is text with an error, as
in tsgo.

## Diagnostics

`Diagnostics` is an ordered list of `{code, offset, length, message}`. Codes
are TypeScript's where one exists (1005 "';' expected", 1128 "Declaration or
statement expected.", 17008 for an unclosed JSX tag) so that rules and
editors can map them. The scanner and the parser share one list.

## Debugging

`fastlint dump-tree [--spans] [--errors] <file>` prints the s-expression
dump. Each node is one line: the kind, a `:` followed by flag names, and
then, in quotes, the tokens in the node's range that no child owns. For
`const x = 42;`:

```
(SourceFile
  (VariableStatement ";"
    (VariableDeclarationList : const "const"
      (VariableDeclaration "="
        (Identifier "x"
        )
        (NumericLiteral "42"
        )
      )
    )
  )
)
```

With `--spans` each kind is followed by `@start-end` byte offsets
(`VariableDeclaration@6-12`) and the quoted tokens are left out, which is
the form the differential harness diffs. `--errors` prints the diagnostics
after the tree.

The dump is iterative and caps indentation, so a pathological file cannot
overflow the stack or produce a quadratic dump. `--batch <list>` dumps many
files in one process for the differential harness. The rest of the
debugging aids (token dumps, the harness, the fuzzer, ASAN) are in
docs/debugging.md.

## Gates

Parser work is gated on three commands (CLAUDE.md):

- `node make.ts parse-diff [--jsx]` diffs the tree against tsgo's over the
  TypeScript test corpus and writes docs/parser-conformance.md. Mismatches
  land in .cache/parse-diff/mismatches.txt, tagged `[invalid input]` when
  tsgo itself reports errors on the file.
- `node make.ts fuzz` mutates the corpus under ASAN and checks the tree
  invariants; a failure is pinned to a seed, replayed and minimized under
  build/asan/fuzz-failures/.
- `node make.ts bench` reports parse MB/s on the release build.

Parser tests live in source/tests/parser_test.cc and assert on the dump text;
scanner tests in scanner_test.cc drive `scanOne` and the rescans directly.

## Adding a construct

1. Add any new `NodeKind` to node_kind.def and, if tsgo names it
   differently, a rename in tools/parse-diff/normalize.ts.
2. Write the production next to its neighbours, beginning and ending its
   node and putting keywords into flags rather than nodes.
3. If it introduces a repetition, give it a `ListKind` with element and
   terminator predicates and a TS diagnostic in lists.cc.
4. If a lookahead is needed, prefer `peekKind` over a `Mark`; if a `Mark`
   is needed, make sure the probe is bounded or memoized.
5. Add a parser test, then run `node make.ts parse-diff --filter <name>
   --show 3` against the tsgo corpus files that use the construct, and
   `node make.ts fuzz --filter <name>` before committing.
