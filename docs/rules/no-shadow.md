# no-shadow

Reports a declaration whose name an enclosing scope already declares. Not in
the recommended preset.

```ts
const x = 1;
function f(x: number) {} // 'x' is already declared in the upper scope on line 1 column 7.
```

Every declaration the binder records takes part: variables, parameters,
functions, classes, imports, catch parameters, interfaces, type aliases,
enums and their members, namespaces and type parameters. A name in the scope
of a named function or class expression also shadows that expression's own
name. An enum member that shadows its enum gets the `noEnumShadow` message,
since references inside the enum resolve to the member.

## Options

- `hoist`: which later declarations a name can shadow. `"functions-and-types"`
  (default): functions, interfaces and type aliases declared below the name
  count, other declarations below it do not. `"functions"`, `"types"`,
  `"all"` (everything below counts) and `"never"` (nothing below counts).
- `builtinGlobals`: `false` by default. When `true`, a name that shadows an
  ECMAScript standard global (`Object`, `Promise`, `Map`, ...) is reported
  with `noShadowGlobal`. A script's top-level `var` merges with the global
  and is not reported.
- `allow`: names that may shadow.
- `ignoreOnInitialization`: `false` by default. When `true`, a parameter of a
  function passed to a call in the shadowed variable's own initializer is
  not reported (`const a = [].find(a => a)`).
- `ignoreTypeValueShadow`: `true` by default. A type shadowing a value is not
  reported, and neither is a value shadowing a type. Type-only imports count
  as types.
- `ignoreFunctionTypeParameterNameValueShadow`: `true` by default. A
  parameter of a function type, call or construct signature, method
  signature, overload or `declare function` that shadows a value is not
  reported.

A named function or class expression that initializes the variable of the
same name (`var f = function f() {}`, also behind `||`, `&&`, `??` or a
conditional) is not shadowing. Neither is an interface or type alias inside
`declare module 'm'` that merges with a type-only import from `'m'`, a
static method's type parameter that repeats its class's, or anything inside
`declare global`. In a `.d.ts` file, `declare`d variables, classes, enums
and namespaces are skipped.

## Fix

None.

## Compared with ESLint

Same messages and options as typescript-eslint's `no-shadow`, which extends
ESLint's. Global names come from a fixed list of ECMAScript builtins rather
than a `globals` configuration, so browser and Node globals are not known.
`this` parameters and the `arguments` object are never declarations here, so
they are never reported.
