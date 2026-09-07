# no-unnecessary-condition

Reports a condition whose type makes it always truthy, always falsy or `never`,
and a `??` whose left side is always or never nullish. Not in the recommended
preset (upstream places it in strict). Type-aware: runs only with `--project`.

```ts
declare const b: object;
if (b) {
} // Unnecessary conditional, value is always truthy.
```

Conditions are the tests of `if`, `while`, `for`, `do`/`while` and the ternary,
the left operand of `&&`/`||` (and the right when the whole expression feeds a
condition), and the `!` argument. A `??` left operand and a `??=` target are
checked for nullishness instead.

- `alwaysTruthy` / `alwaysFalsy` — the type can only be truthy, or only falsy.
  Literal members are weighed by value, so `'' | false` is always falsy while
  `object` is always truthy. Under `!` the two messages swap.
- `never` — the type is `never`.
- `neverNullish` — the left of `??` can never be `null` or `undefined`.
- `alwaysNullish` — the left of `??` is always `null` or `undefined`.

A condition whose type includes `any`, `unknown` or a bare type variable is
always considered necessary. Indexing into an array or a tuple with a
non-literal index is exempt, since the element type omits the out-of-bounds
`undefined`. The rule needs `strictNullChecks`; without it, and without the
escape-hatch option, it reports `noStrictNullCheck` once for the file.

## Options

- `allowConstantLoopConditions` (default `never`): a loop test that is a
  constant. `always` (or the legacy `true`) allows a `true` literal; `never`
  (or `false`) allows none; `only-allowed-literals` allows `true`, `false`, `0`
  and `1`.
- `allowRuleToRunWithoutStrictNullChecksIKnowWhatIAmDoing` (default off):
  suppresses the `noStrictNullCheck` report.

## Compared with ESLint

The truthiness (`alwaysTruthy`/`alwaysFalsy`/`never`) and nullish
(`neverNullish`/`alwaysNullish`) checks, the array-index exemption, the
constant-loop option and the strict-null gate match upstream. Not ported: the
literal-comparison checks (`comparisonBetweenLiteralTypes`,
`noOverlapBooleanExpression`) on binary and `switch` expressions; the
unnecessary-optional-chain check (`neverOptionalChain`), which needs
nullable-origin analysis of the chain; the array-predicate callback checks
(`alwaysTruthyFunc`, `alwaysFalsyFunc`); the type-predicate check
(`typeGuardAlreadyIsType`); and the `isNullableMemberExpression` refinement for
optional properties, along with the `checkTypePredicates` option.
