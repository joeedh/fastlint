# no-unsafe-member-access

Reports member access on a value with type `any`, and a computed key whose
own type is `any`. In the recommended preset. Type-aware: runs only with
`--project`.

```ts
declare const x: any;
x.foo; // Unsafe member access .foo on an `any` value.
```

The object's type decides. When it is `any`, the access reports
`unsafeMemberExpression`; an error type reports `errorMemberExpression`. In a
chain, only the innermost `any` object is reported, since every access after
it is unsafe for the same reason and stays silent. A computed access reports
its key in brackets, `x[y]`, and a dotted access reports `.foo`.

A computed key that is itself `any` reports `unsafeComputedMemberAccess`
(`errorComputedMemberAccess` for an error type), pointing at the key. A
literal key and an update expression are never `any` and are skipped. The
reported span strips wrapping parentheses so `x[(y)]` underlines `[y]`.

A heritage clause names a type, not a value, so `class B implements FG.A`
and `interface B extends FG.A` are not checked.

With `noImplicitThis` off, access on a `this` typed as `any` reports
`unsafeThisMemberExpression`, or `errorThisMemberExpression` when `this`
cannot be resolved, naming the compiler option or a `this` parameter as the
fix.

## Options

- `allowOptionalChaining`: `false` by default. When `true`, an optional
  access (`x?.foo`) and an optional computed key are not checked, since the
  chain short-circuits on a nullish value.

## Compared with ESLint

Same messages as typescript-eslint's `no-unsafe-member-access`. No fix or
suggestion is offered, matching upstream.
