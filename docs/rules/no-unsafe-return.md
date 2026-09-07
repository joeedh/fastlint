# no-unsafe-return

Reports returning a value with type `any` from a function whose return type
is more specific. In the recommended preset. Type-aware: runs only with
`--project`.

```ts
function foo() {
  return 1 as any; // Unsafe return of a value of type `any`.
}
```

The returned value's type decides. An `any` return reports `unsafeReturn`
with type `` `any` ``, an `any` array reports it with `` `any[]` ``, and a
promise resolving to `any` in an async function reports it with
`` `Promise<any>` ``. An error type is named `error`. A `Promise<any>`
returned from a non-async function is safe and passes.

Both an arrow expression body and a `return` statement are checked, against
the nearest enclosing function.

Two cases suppress the report:

- **Declared return type.** When the function has an explicit return
  annotation whose type the value already has, or whose type is `any` or
  `unknown`, the return is intentional and passes. An async function
  compares awaited types too.
- **`unknown` receiver.** A function declared to return `unknown` accepts
  `any`, and one returning `unknown[]` accepts an `any` array; an async
  function returning `Promise<unknown>` accepts a resolved `any`.

When neither the value nor the declared return type is `any` but they are
generic instances of one type whose type arguments disagree, so a body
returning `Set<any>` from a function typed `() => Set<string>`, it reports
`unsafeReturnAssignment` naming both types. A function expression takes its
contextual type here, since the checker types its own return from the body.

With `noImplicitThis` off, returning a `this` typed as `any` reports
`unsafeReturnThis`.

## Options

None.

## Compared with ESLint

Same messages as typescript-eslint's `no-unsafe-return`. No fix or suggestion
is offered, matching upstream.
