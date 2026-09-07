# no-unsafe-assignment

Reports assigning a value with type `any` to a variable or property, and the
destructuring and spread forms of the same mistake. In the recommended
preset. Type-aware: runs only with `--project`.

```ts
const x = 1 as any; // Unsafe assignment of an `any` value.
```

The value's type decides. An `any` value assigned to anything but `unknown`
reports `anyAssignment`, or `anyAssignmentThis` with `noImplicitThis` off
when the value is a `this` typed as `any`. Assigning `any` to `unknown` is
safe and passes.

The receiver's type comes from its annotation. A binding identifier has no
type of its own from the server, so its annotated type is read as the
initializer's contextual type. When the value and the receiver are generic
instances of one type whose type arguments disagree, so `Set<any>` into
`Set<string>`, it reports `unsafeAssignment` naming both types. A bare
`new Map()` is exempt, matching upstream, since its argument is inferred from
the receiver.

Destructuring is checked element by element. An array pattern over an `any`
array reports `unsafeArrayPattern`; a tuple element that is `any` reports
`unsafeArrayPatternFromTuple`. An object pattern whose property is `any`
reports `unsafeObjectPattern`. Both recurse into nested patterns. A spread of
an `any` value into an array literal reports `unsafeArraySpread`.

The rule is registered on class fields, assignments, assignment patterns
(including a parameter property default), variable declarators, object-literal
properties and JSX attributes.

## Options

None.

## Compared with ESLint

Same messages as typescript-eslint's `no-unsafe-assignment`. No fix or
suggestion is offered, matching upstream.
