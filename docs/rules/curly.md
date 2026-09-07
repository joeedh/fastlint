# curly

Requires or forbids braces around the bodies of `if`, `else`, `while`, `do`,
`for`, `for-in` and `for-of`. Not in the recommended preset.

```ts
if (x) y(); // Expected { after 'if' condition.
```

## Options

The first option is a mode:

- `"all"` (default): every body needs braces.
- `"multi"`: braces only around several statements. A single statement in
  braces is reported unless the braces are needed: a lexical declaration,
  or an `if` without `else` that a following `else` would capture.
- `"multi-line"`: a body on one line may go without braces; a body spanning
  lines needs them.
- `"multi-or-nest"`: a single-line body goes without braces. A body that
  spans lines needs them, and so does one with a comment before it.

The second option `"consistent"` (with any mode but `all`) makes the
branches of one `if`/`else if`/`else` chain agree: if any branch needs
braces, all get them; if none does, all lose them.

## Fix

Adding braces wraps the body in a new block, which prints on its own lines.
Removing braces moves the inner statement into the body's place; that only
happens when the statement ends in `;` or `}`, so a following token cannot
join it.

## Compared with ESLint

Same options and messages as ESLint's `curly`. Upstream's fix inserts `{`
and `}` around the text as it stands; ours reprints the new block from the
kind template, so a one-line `if` gains a three-line body.
