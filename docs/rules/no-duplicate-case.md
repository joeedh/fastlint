# no-duplicate-case

Disallows a `case` whose test repeats an earlier one in the same `switch`.
In the recommended preset.

```ts
switch (a) {
  case 1: break;
  case 1: break; // Duplicate case label.
}
```

Two tests are the same when their trees are equivalent under
`ast::equivalent`: spacing and parentheses do not matter, tokens do, so
`x.y` and `x . y` collide while `1` and `'1'` do not.

## Options

None.

## Fix

None.

## Compared with ESLint

Same report and message as ESLint's `no-duplicate-case`, which compares
token sequences. Structural equivalence agrees with it on every upstream
test case.
