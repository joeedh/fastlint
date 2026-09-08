# no-unreachable

Reports statements after one that never lets control through: `return`,
`throw`, `break`, `continue`, an `if` whose branches both exit, a `switch`
whose every path exits, a `try` that exits, a `do` whose body exits, or a
`while (true)` / `for (;;)` with no `break` that leaves it. In the
recommended preset.

```ts
function f() {
  return;
  g(); // Unreachable code.
}
```

Consecutive unreachable statements make one report spanning them all.
Function declarations, `var` declarations without initializers, empty
statements, imports and type-only declarations are hoisted or erased, so
they are skipped and split the run.

## Options

None.

## Fix

None.

## Compared with ESLint

ESLint decides reachability from its code-path analysis; this rule walks a
statement's structure to ask whether control can complete it normally
(rules/flow.h). The two agree on the cases this rule reports. A `break` or
`continue` is matched to the loop or switch it targets, so a `break` aimed at
an inner loop no longer keeps a `while (true)` from exiting, a labelled
`break` that leaves an outer loop counts against that loop, and a `switch`
counts as exiting only when it has a `default` and every path through it
exits. Analysis stays within one function; a `throw` that a caller catches
still reads as an exit here, as it does in ESLint.
