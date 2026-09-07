# no-unsafe-argument

Reports passing a value with type `any` to a parameter of a more specific
type. In the recommended preset. Type-aware: runs only with `--project`.

```ts
declare function foo(a: number): void;
declare const x: any;
foo(x); // Unsafe argument of type `any` assigned to a parameter of type `number`.
```

The resolved signature of the call decides. Each argument is lined up with
its parameter and reports `unsafeArgument` when the argument is `any` and the
parameter is neither `any` nor `unknown`, naming both types. The same message
covers two generic instances of one type whose type arguments disagree, so
`Set<any>` into `Set<string>`. An error-typed argument is named `error typed`
in the same message rather than as `any`.

A rest parameter is walked by its element type. A spread argument reports
`unsafeSpread` when its type is `any`, `unsafeArraySpread` when it is an
`any` array, and `unsafeTupleSpread` when a tuple element cannot safely land
in the parameter it reaches. Calls, `new` expressions and tagged templates
are all checked through their resolved signature.

## Options

None.

## Compared with ESLint

Same messages as typescript-eslint's `no-unsafe-argument`. No fix or
suggestion is offered, matching upstream.
