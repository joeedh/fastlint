# no-unnecessary-type-assertion

Reports a `!` non-null assertion whose operand cannot be null. In the
recommended preset. Type-aware: runs only with `--project`.

```ts
declare function foo(): number;
const a = foo()!; // This assertion is unnecessary since it does not change ...
```

The rule fires in three shapes, each fixed by dropping the `!`:

- The operand is already non-nullable, so the assertion changes nothing
  (`unnecessaryAssertion`).
- The operand is the target of a plain `=` assignment (`x! = y`), where the
  assertion never affects the value's type (`contextuallyUnnecessary`).
- The operand is nullable, but the surrounding context accepts each nullable
  member it carries, so narrowing it away is redundant
  (`contextuallyUnnecessary`).

An operand typed `any` or `unknown` counts as nullable, since either could hold
`null` or `undefined`. The nullable-context check compares the operand's
`null`, `undefined` and `void` members against the contextual type, and bails
when the operand is `unknown` but the context is not.

The nullable-context check applies only where the operand has a contextual type
to compare against, matching upstream's `getContextualType`: a call or `new`
argument, the initializer of a type-annotated variable or class field, and the
right side of a plain `=`. A `!` in any other position (a property value, an
array element, a template span, a `return`) carries no contextual type, so it is
never reported `contextuallyUnnecessary` on that basis.

A variable declared without an initializer and without a definite-assignment
`!` is left alone: its value is absent at the declaration, so the assertion may
be guarding a read before assignment, and removing it could produce
use-before-assignment code. The check needs `strictNullChecks`; without it no
type reads as non-nullable and the rule stays quiet.

## Compared with ESLint

Only the non-null assertion (`!`) form is ported. typescript-eslint's rule also
reports an `as` cast or an angle-bracket `<T>` assertion that does not change
the expression's type, including double assertions and const assertions, with
its `checkLiteralConstAssertions` and `typesToIgnore` options. That path rests
on whole-type structural comparison (property-by-property equivalence, mutual
assignability, contextual typing of arbitrary nodes, phantom type arguments and
overload resolution) and is not ported yet. The non-null reporting, its fix and
the strict-null-checks gate match upstream.
