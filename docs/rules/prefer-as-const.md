# prefer-as-const

Prefers `as const` to a literal type that repeats the literal value. In the
recommended preset.

```ts
let a = 'x' as 'x'; // Expected a `const` instead of a literal type assertion.
let b: 2 = 2; // Expected a `const` assertion instead of a literal type annotation.
```

Value and type must spell the same token: `'x' as "x"` and `` `x` as 'x' ``
are not reported.

## Options

None.

## Fix

An assertion (`v as T`, `<T>v`) has its type replaced by `const`. An
annotated variable or class property is not fixed, because moving the type
onto the value changes the declaration; its report carries a suggestion
that removes the annotation and asserts the value instead.

## Compared with ESLint

Same reports, messages, fix and suggestion as typescript-eslint's
`prefer-as-const`.
