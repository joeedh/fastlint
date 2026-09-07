# no-empty

Disallows empty block statements and empty `switch` bodies. In the
recommended preset.

```ts
if (x) {} // Empty block statement.
switch (x) {} // Empty switch statement.
```

Function bodies are never reported. A block that holds a comment is
deliberate and is not reported either.

## Options

```json
{ "allowEmptyCatch": true }
```

`allowEmptyCatch` exempts the body of a `catch` clause.

## Fix

None.

## Compared with ESLint

Same reports, messages and option as ESLint's `no-empty`. Upstream offers a
suggestion that inserts a comment; there is none here yet.
