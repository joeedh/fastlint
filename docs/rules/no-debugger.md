# no-debugger

Disallows `debugger` statements. In the `fastlint:recommended` preset.

```ts
debugger; // Unexpected 'debugger' statement.
```

## Options

None.

## Fix

Removes the statement. A `debugger` that is the sole body of an `if`, a loop
or a label (`if (x) debugger;`) is reported but left alone, since removing a
required child would change the shape of the parent.

## Compared with ESLint

Same report and message as ESLint's `no-debugger`. ESLint has no fix for
this rule; ours removes the statement.
