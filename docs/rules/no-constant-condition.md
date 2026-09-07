# no-constant-condition

Disallows conditions whose value never changes: literals, object and array
expressions, `void`, `typeof`, assignments of constants, `Boolean(...)` of
a constant, and logical expressions one side decides. In the recommended
preset.

```ts
if (true) {} // Unexpected constant condition.
while (x = 1) {} // Unexpected constant condition.
do {} while (`${a}`); // fine: the template depends on a
```

The report is on the test expression. `undefined` and `Boolean` count only
when they are the globals; a local of the same name is a variable.

## Options

```json
{ "checkLoops": "allExceptWhileTrue" }
```

- `"allExceptWhileTrue"` (default): loop tests are checked except the
  idiom `while (true)`.
- `"all"` (or `true`): every loop test is checked.
- `"none"` (or `false`): loop tests are not checked.

A loop in a generator whose body yields is not reported, since the yield is
the way out. A yield in a `for` head runs before the test and does not
count.

## Fix

None.

## Compared with ESLint

A port of ESLint's `isConstant` and `isLogicalIdentity`, including the
`checkLoops` option and the generator handling.
