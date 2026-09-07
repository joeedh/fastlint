# strict-boolean-expressions

Reports a value used in a boolean context whose type is not a plain boolean.
Not in the recommended preset (upstream places it in strict). Type-aware:
runs only with `--project`.

```ts
declare const x: string | null;
if (x) {
} // Unexpected nullable string value in conditional ...
```

Boolean contexts are the tests of `if`, `while`, `do`/`while`, `for` and the
ternary `? :`, the argument of `!`, and each operand of `&&` and `||` that
feeds a condition. For a bare `&&`/`||` used as a statement, the left operand
is a condition but the right runs for its side effects only and is left
unchecked.

The condition's type is split into union members and each member sorted into a
variant kind — nullish, boolean, string, number, object, enum, `any` and the
truthy-literal forms. The set of kinds is matched against a table that decides
which message, if any, applies:

- `conditionErrorObject` / `conditionErrorNullish` — an object is always
  truthy, a nullish value always falsy.
- `conditionErrorString` / `conditionErrorNumber` — a bare string or number,
  unless allowed by `allowString` (on by default) or `allowNumber` (on by
  default).
- `conditionErrorNullableBoolean` / `Number` / `String` / `Object` / `Enum` —
  a nullable primitive, each gated by its `allowNullable*` option.
- `conditionErrorAny` — an `any`, `unknown` or bare type parameter, unless
  `allowAny`.
- `conditionErrorOther` — any remaining mixed type.

The rule needs `strictNullChecks`. Without it, and without the escape-hatch
option, it reports `noStrictNullCheck` once for the file.

## Options

Each option is a boolean. Defaults: `allowString` and `allowNumber` on;
`allowNullableObject` on; `allowAny`, `allowNullableBoolean`,
`allowNullableNumber`, `allowNullableString`, `allowNullableEnum` off. The
`allowRuleToRunWithoutStrictNullChecksIKnowWhatIAmDoing` option suppresses the
`noStrictNullCheck` report.

## Compared with ESLint

The condition detection, the variant table and the messages match upstream.
Not ported: the suggestion fixes (each message carries fixes such as
`Boolean(value)` or `value != null` upstream; suggestions are not applied
anywhere in fastlint yet), the array-method-predicate path
(`array.some(x => x)` reporting on the predicate return type, with its
`predicateCannotBeAsync` and `explicitBooleanReturnType` messages), and the
truthiness-assertion-function argument path.
