# no-fallthrough

Reports a `case` (or `default`) that the previous non-empty case runs into
without a comment saying so. In the recommended preset.

```ts
switch (x) {
  case 1:
    a();
  case 2: // Expected a 'break' statement before 'case'.
    b();
}
```

A case falls through when its body can complete normally, meaning control
reaches the end of the case rather than leaving through a `return`, `throw`,
`break`, or `continue` (the same completion analysis no-unreachable uses).
The comment that permits it
is the last comment before the next clause, or the last comment before the
closing brace when the case body is a single block, and it must match the
pattern `falls?\s?through` (case-insensitive): `// falls through`,
`/* fallthrough */`, `// FALL THROUGH`. Directive comments (`eslint-*`,
`fastlint-*`, `globals`, `exported`) never count.

An empty case that stacks directly on the next one (`case 1: case 2:`) is
grouping, not fallthrough; a blank line between them makes it fallthrough.

## Options

```json
{
  "allowEmptyCase": false,
  "commentPattern": "break[\\s\\w]+omitted",
  "reportUnusedFallthroughComment": false
}
```

- `allowEmptyCase`: empty cases never report, blank lines or not.
- `commentPattern`: a regular expression that replaces the default. The
  engine covers the usual ECMAScript syntax (docs/rules.md "Pattern
  options"); an unsupported pattern falls back to the default.
- `reportUnusedFallthroughComment`: reports a fallthrough comment on a
  non-empty case that exits, since the comment cannot take effect. The report
  is placed on the comment (`unusedFallthroughComment`).

## Fix

None.

## Compared with ESLint

Same messages and options as ESLint's `no-fallthrough`, with the same
comment placement rules, including `reportUnusedFallthroughComment`. A case
counts as exiting when its body cannot complete normally, using the same
completion analysis as no-unreachable (rules/flow.h), so an exhaustive inner
`switch` or an `if` whose branches both exit ends a case just as a bare
`break` does.
