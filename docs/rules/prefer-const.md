# prefer-const

Requires `const` for a `let` name that is assigned once and never
reassigned. Not in the recommended preset.

```ts
let x = 1; // 'x' is never reassigned. Use 'const' instead.
foo(x);
```

A name is reported when it has exactly one write, that write sits in the
name's own scope, and the write is a declarator initializer or a
destructuring assignment statement that could become a declaration. A read
before the write does not save the name; it moves the report to the
declaration. The `let` of a classic `for` head is never reported.

## Options

- `destructuring`: `"any"` (default) reports each name of a destructuring
  that could be `const` on its own; `"all"` reports the names only when every
  name of the destructuring could be.
- `ignoreReadBeforeAssign`: `false` by default. When `true`, a name read
  before its single write is not reported.

## Fix

Changes `let` to `const` when every declarator has an initializer (or the
declaration is a `for-in`/`for-of` head) and every name the declaration
binds is reported. A name assigned after a bare `let x;` is reported without
a fix, since the initializer would have to move.

## Compared with ESLint

Same message, options and fix conditions as ESLint's `prefer-const`.
Upstream attaches the fix to the last declarator's report and applies it in
one pass; ours attaches it to every report of the declaration, which gives
the same text after `--fix`. There is no `/* exported */` directive and no
`markVariableAsUsed`, so names other rules mark as used are still reported.
Names bound in a nested pattern of a multi-declarator `let` count
individually, where upstream counts the outer pattern's entries.
