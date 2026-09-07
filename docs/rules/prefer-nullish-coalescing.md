# prefer-nullish-coalescing

Reports a `||` or `||=` whose left side is nullable, where `??` or `??=` would
be safer. Not in the recommended preset (upstream places it in stylistic).
Type-aware: runs only with `--project`.

```ts
declare const a: string | undefined;
const b = a || 'default'; // Prefer using nullish coalescing operator (`??`) ...
```

The left operand's type decides. A type that includes `null`, `undefined`,
`void`, `any` or `unknown` is nullable, taking a union's members together. A
report lands on the operator token and carries a `suggestNullish`
suggestion that swaps the operator for its nullish form.

The rule needs `strictNullChecks`. Without it, and without the escape-hatch
option, the rule reports `noStrictNullCheck` once for the file and otherwise
stays quiet, since no type reads as nullable.

## Options

- `ignoreConditionalTests`: `true` by default. A `||` used as the test of an
  `if`, a loop or a ternary is left alone, reached through logical, ternary
  branch, sequence and `!` parents.
- `ignoreMixedLogicalExpressions`: `false` by default. When `true`, a `||`
  sitting next to a `&&` is left alone, since switching to `??` would change
  how the two bind.
- `ignoreBooleanCoercion`: `false` by default. When `true`, a `||` coerced to
  boolean by an enclosing `Boolean(...)` call is left alone.
- `ignorePrimitives`: admits a nullable union that also contains a given
  primitive. `true` ignores all of `bigint`, `boolean`, `number` and
  `string`; an object turns them on one at a time. An `any` or `unknown`
  left side is ignored whenever any primitive is.
- `allowRuleToRunWithoutStrictNullChecksIKnowWhatIAmDoing`: `false` by
  default. When `true`, the `noStrictNullCheck` report is suppressed.

## Suggestions

`suggestNullish` swaps `||` for `??`, or `||=` for `??=`. It does not add the
parentheses upstream inserts when the operator sits in a mixed `&&`/`??`
expression, and suggestions are not applied anywhere yet.

## Compared with ESLint

Only the logical-or forms are ported. typescript-eslint's rule also rewrites
a ternary that reimplements a nullish check (`a ? a : b`, `a != null ? a :
b`) with `preferNullishOverTernary`, and an `if` that guards an assignment
with `preferNullishOverAssignment`; neither is ported yet, and their
`ignoreTernaryTests` and `ignoreIfStatements` options are absent. The `||`
reporting, its options and the strict-null-checks gate match upstream.
