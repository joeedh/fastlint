# restrict-template-expressions

Reports an interpolated expression in a template literal whose type is not
string-like. In the recommended preset. Type-aware: runs only with
`--project`.

```ts
declare const arg: number;
const msg = `arg = ${arg}`; // Invalid type "number" of template literal expression.
```

Each interpolation of a plain template literal is checked; a tagged template
is left alone. The expression's constrained type decides, so a type parameter
stands for its base constraint. A union passes when every member passes; an
intersection passes when some member passes. A string-like type always
passes.

The report names the type as the compiler prints it. Its span is the
interpolated expression, not the whole literal.

## Options

Each `allow<Kind>` flag admits one more kind of value. The defaults match the
recommended preset, which is permissive; the strict preset turns them all
off.

- `allowNumber`: `true` by default. A number or bigint passes.
- `allowBoolean`: `true` by default. A boolean passes.
- `allowAny`: `true` by default. An `any` passes.
- `allowNullish`: `true` by default. `null` and `undefined` pass.
- `allowRegExp`: `true` by default. A `RegExp` passes.
- `allowArray`: `false` by default. An array or tuple passes when every
  element type passes, checked through the `number` index type.
- `allowNever`: `false` by default. A `never` passes, so an exhaustiveness
  `throw` can interpolate the narrowed value.
- `allow`: a list of type specifiers whose types, and subtypes, pass.
  Defaults to the library `Error`, `URL` and `URLSearchParams`.

## Compared with ESLint

Same message as typescript-eslint's `restrict-template-expressions`. The
`allow` list supports only library specifiers (`{ from: 'lib', name }`),
matched by name over a type and its base types. A `file` or `package`
specifier is not supported yet, the same gap the type-or-value specifiers
leave in no-floating-promises. No fix or suggestion is offered, matching
upstream.
