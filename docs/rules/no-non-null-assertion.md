# no-non-null-assertion

Disallows the `!` postfix non-null assertion. Not in the recommended
preset.

```ts
x!.y; // Forbidden non-null assertion.
```

## Options

None.

## Fix

None. When the assertion is the object of a member access or the callee of
a call that is not being assigned to, the report carries a suggestion that
drops the `!` and makes the access optional: `x!.y` becomes `x?.y`,
`x!()` becomes `x?.()`. On an access that is already optional (`x!?.y`) the
suggestion only drops the `!`.

## Compared with ESLint

Same report, message and suggestion as typescript-eslint's
`no-non-null-assertion`.
