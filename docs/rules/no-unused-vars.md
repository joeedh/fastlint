# no-unused-vars

Reports a declared name that nothing reads. In the recommended preset.

```ts
import { Foo } from './foo'; // 'Foo' is defined but never used.
const count = 1; // 'count' is assigned a value but never used.
```

Every declaration the binder records takes part: variables, parameters,
functions, classes, imports, catch parameters, interfaces, type aliases,
enums, namespaces and type parameters. The declarations of one name in one
scope are checked together, so an interface merged with its class or a
function with its overloads counts as one variable. A name counts as used
when some read reference is not feeding the name itself. A read inside the
name's own function or class body only counts when it can reach outside, a
read that is the right side of an assignment back to the name (`a = a + 1`)
does not count, and a read inside a type alias or interface of the same name
does not count. Exported names are used; so are parameters of parameter
properties, setters, signatures and overloads, and a `for-in`/`for-of` loop
variable whose whole body is a `return`.

A name whose only reads sit under `typeof` in a type or in a type predicate
(`x is string`) gets the `usedOnlyAsType` message, unless it is an import.

## Options

- `vars`: `"all"` (default) checks every variable. `"local"` skips the top
  level of a script.
- `args`: `"after-used"` (default) reports an unused parameter only when no
  later parameter is used. `"all"` reports every unused parameter, `"none"`
  reports none.
- `caughtErrors`: `"all"` (default) checks catch parameters; `"none"` skips
  them.
- `ignoreRestSiblings`: `false` by default. When `true`, a name destructured
  beside a rest element (`const { type, ...rest } = obj`) is not reported.
- `ignoreClassWithStaticInitBlock`: `false` by default. When `true`, a class
  with a `static {}` block is not reported.
- `ignoreUsingDeclarations`: `false` by default. When `true`, `using` and
  `await using` names are not reported.
- `varsIgnorePattern`, `argsIgnorePattern`, `caughtErrorsIgnorePattern`,
  `destructuredArrayIgnorePattern`: regular expressions for names that may
  go unused. The message names the pattern that would have allowed the name.
- `reportUsedIgnorePattern`: `false` by default. When `true`, a name that
  matches an ignore pattern but is used is reported with `usedIgnoredVar`.
- `enableAutofixRemoval`: `{ "imports": true }` makes the import removal
  below a fix instead of a suggestion.

## Ambient declarations

In a `.d.ts` file, the top-level declarations describe the outside world and
are never reported, nor are declarations directly inside `declare module`
blocks, `declare namespace` blocks or namespaces nested in them. A block
that contains an `export` statement of its own drops this exemption for its
other declarations. `declare global` bodies bind into the file's scope and
follow the file's rules.

## Fix

None by default. An unused import gets a suggestion that removes the
specifier, or the whole import declaration when every specifier of it is
unused (`removeUnusedVar` and `removeUnusedImportDeclaration`). With
`enableAutofixRemoval.imports` the removal is a fix.

## Compared with ESLint

Same messages and options as typescript-eslint's `no-unused-vars`, which
extends ESLint's. There are no `/* global */` or `/* exported */` directives
and no `markVariableAsUsed`, so names other rules mark as used are still
reported. `this` parameters, enum members and a function or class
expression's own name are never reported. The reference of a `typeof`
query resolves to a type-only import, as it does in TypeScript.
