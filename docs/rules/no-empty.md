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

None. An empty block statement carries a suggestion (`suggestComment`) that
inserts `/* empty */` between the braces, which an editor applies; `--fix`
never does. An empty `switch` is reported without a suggestion.

## Compared with ESLint

Same reports, messages, option, and the block-statement `suggestComment`
suggestion as ESLint's `no-empty`.
