# no-unsafe-call

Reports a call whose callee has type `any`. In the recommended preset.
Type-aware: runs only with `--project`.

```ts
declare const x: any;
x(); // Unsafe call of an `any` typed value.
```

The callee's type decides. When it is `any`, a normal call reports
`unsafeCall`, a `new` reports `unsafeNew`, and a tagged template reports
`unsafeTemplateTag`. An error type reports the matching `errorCall`,
`errorNew` or `errorTemplateTag` instead, so an unresolved name is named as
such rather than as `any`.

The callee is read through the same `this` walk the other unsafe rules use,
so a member or nested call on `any` is caught. With `noImplicitThis` off, a
call whose receiver is `this` typed as `any` reports `unsafeCallThis`, or
`errorCallThis` when that `this` cannot be resolved, pointing at the
compiler option or a `this` parameter as the fix.

The built-in `Function` type is unsafe to call: a value typed as `Function`
has no call or construct signature of its own, so calling or constructing it
reports `unsafeCall` or `unsafeNew`.

## Options

None.

## Compared with ESLint

Same messages as typescript-eslint's `no-unsafe-call`. Suggestions and fixes
are not offered, matching upstream, which reports without a fix.
