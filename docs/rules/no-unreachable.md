# no-unreachable

Reports statements after one that never lets control through: `return`,
`throw`, `break`, `continue`, an `if` whose branches both exit, a `try`
that exits, a `do` whose body exits, or a `while (true)` / `for (;;)`
without a `break`. In the recommended preset.

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

ESLint decides reachability from its code-path analysis; this rule looks at
statement lists with the exits above. The difference shows in loops: a
`break` anywhere inside a `while (true)` body, even one aimed at an inner
loop, keeps the code after the loop reachable here, and a `switch` whose
every case exits is not treated as exiting. Both err toward silence.
