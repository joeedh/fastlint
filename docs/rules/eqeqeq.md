# eqeqeq

Requires `===` and `!==` in place of `==` and `!=`. Not in the recommended
preset.

```ts
if (a == b) {} // Expected '===' and instead saw '=='.
```

The report spans the operator token.

## Options

The first option is a mode:

- `"always"` (default): every loose comparison is reported. A second option
  `{ "null": "always" | "never" | "ignore" }` refines comparisons against
  the `null` literal: `never` requires `==`/`!=` for them, `ignore` leaves
  them alone.
- `"smart"`: loose comparison is allowed between two literals of the same
  type, against `typeof x`, and against `null`.
- `"allow-null"`: as `always` with `{ "null": "ignore" }`.

## Fix

The operator changes in place when both sides are known to share a runtime
type: a `typeof` comparison, or two literals of the same type (strings,
templates without substitutions, numbers, booleans). Every other report
carries the change as a suggestion instead, since `a === b` can differ from
`a == b`.

## Compared with ESLint

Same options, messages and fix conditions as ESLint's `eqeqeq`.
