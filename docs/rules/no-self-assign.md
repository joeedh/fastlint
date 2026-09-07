# no-self-assign

Disallows assigning a value to itself: `a = a`, `[a, b] = [a, b]`,
`({a} = {a})`, `a.b = a.b`, and the logical assignments `&&=`, `||=` and
`??=`. In the recommended preset.

```ts
a = a; // 'a' is assigned to itself.
[x, y] = [x, z]; // 'x' is assigned to itself.
```

The report is on the right-hand occurrence. The name in the message is the
right-hand text with its spaces removed.

## Options

```json
{ "props": true }
```

With `props` (the default) member expressions are compared as well:
`a.b = a.b`, `a[b] = a[b]`, `this.x = this.x`. Set it to `false` to compare
identifiers and patterns only.

## Fix

None.

## Compared with ESLint

Same reports, messages and option as ESLint's `no-self-assign`, including
the treatment of spread and rest elements and of properties after an object
spread.
