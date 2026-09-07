# no-var

Requires `let` or `const` instead of `var`. Not in the recommended preset.

```ts
var x = 1; // Unexpected var, use let or const instead.
```

A `var` inside `declare global { ... }` describes the host and is not
reported.

## Options

None.

## Fix

Changes `var` to `let` in place when the program keeps its meaning. The
check answers ESLint's questions from the binder:

- not in a `case` clause, and the parent is a statement list or a loop head;
- no name is `let`, redeclared, or shadowed by a `catch` parameter;
- in a script (`.js` without imports or exports), not at the top level,
  where `var` creates a global-object property;
- no reference outside the block that would become the `let`'s scope, and
  none before the declaration (the temporal dead zone), including a name
  read in its own initializer unless the initializer is a function;
- in a loop body, every declarator has an initializer and no closure in the
  loop captures the name.

Files with a TypeScript extension count as modules.

## Compared with ESLint

Same report, message and fix conditions as ESLint's `no-var`. One `var`
with several declarators is one report, as upstream.
