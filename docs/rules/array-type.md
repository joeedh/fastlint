# array-type

Requires one spelling of array types: `T[]` or `Array<T>`, with a separate
choice for `readonly T[]` versus `ReadonlyArray<T>`. Not in the recommended
preset.

```ts
let a: Array<number>; // Array type using 'Array<number>' is forbidden. Use 'number[]' instead.
```

A local type named `Array`, `ReadonlyArray` or `Readonly` is not the
built-in and is left alone, as is a bare `Array` with no type argument.

## Options

```json
{ "default": "array", "readonly": "array" }
```

Each of `default` and `readonly` is one of:

- `"array"`: always `T[]`.
- `"generic"`: always `Array<T>`.
- `"array-simple"`: `T[]` for simple types (keywords, names, `this`,
  qualified names, arrays of simple types) and `Array<T>` for the rest.

`readonly` defaults to the value of `default`. `Readonly<T[]>` counts as a
readonly array and becomes `readonly T[]`.

## Fix

The element type moves between the two shapes. Parentheses come and go with
the slot: `(A | B)[]` becomes `Array<A | B>`, and `Array<A | B>` becomes
`(A | B)[]`. A type that upstream leaves bare, such as `typeof x[]`, is
left bare here too.

## Compared with ESLint

Same options, messages and fixes as typescript-eslint's `array-type`. The
fixer runs to a fixpoint, so where upstream's single pass would leave a
`readonly (A | B)[]` under `readonly: "array-simple"`, ours goes on to
`ReadonlyArray<A | B>`.
