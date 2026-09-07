# no-console

Disallows member access on the global `console`. Not in the recommended
preset.

```ts
console.log(x); // Unexpected console statement.
```

A `console` that resolves to a local declaration (a parameter, an import, a
`var console = ...`) is not the global and is not reported.

## Options

```json
{ "allow": ["warn", "error"] }
```

`allow` lists method names that are fine. With a non-empty list the message
names them: "Unexpected console statement. Only these console methods are
allowed: warn, error."

## Fix

None. Each report on a call statement (`console.log(...)` as a statement in
a block) carries a suggestion that removes the statement.

## Compared with ESLint

Same reports, messages and options as ESLint's `no-console`. The global
check uses the binder rather than the scope manager; a computed access such
as `console["log"]` is reported like a static one, and its suggestion is
"Remove the console method call."
