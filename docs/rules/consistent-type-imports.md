# consistent-type-imports

Requires `import type` for an import whose names are only used as types, or
forbids `import type` altogether. Not in the recommended preset.

```ts
import { Foo, bar } from './foo'; // Imports "Foo" are only used as type.
let x: Foo = bar();
```

An imported name is only used as a type when every reference to it sits in
a type position, under `typeof` in a type, or as the computed key of a
property signature (`{ [ns.key]: string }`). `export { Foo }`, `export
default Foo` and `export = Foo` keep the import's kind, so a value import
that is re-exported stays a value import. A name with no references does
not count either way. A `React` import is a value in a file with JSX, since
the elements compile to calls on it.

When every used name of a value import is a type, the whole import is
reported with `typeOverValue`. When only some are, the import is reported
with `someImportsAreOnlyTypes` naming them. An import with attributes
(`with { type: 'json' }`) is never converted whole.

## Options

- `prefer`: `"type-imports"` (default) reports value imports as above.
  `"no-type-imports"` reports every `import type` declaration and every
  inline `type` specifier with `avoidImportType`.
- `fixStyle`: `"separate-type-imports"` (default) moves type-only names into
  an `import type` declaration. `"inline-type-imports"` marks them `type`
  inside the existing named import instead, where the shape allows it.
- `disallowTypeAnnotations`: `true` by default. Reports `import('foo')` in a
  type position with `noImportTypeAnnotations`.

## Fix

A whole-type import gains `type` after `import`; inline `type` modifiers it
carried are dropped. A mixed import is split: the type-only named specifiers
move into an existing `import type { ... }` of the same module, or into a new
declaration inserted before the import, and a type-only default or namespace
specifier gets its own `import type` declaration. With
`"inline-type-imports"` the named specifiers stay and gain `type`. In
`"no-type-imports"` mode the `type` keyword is removed.

## Compared with ESLint

Same messages, options and fix shapes as typescript-eslint's
`consistent-type-imports`. A rewritten import prints from the printer's
template, so `import {A,B}` comes back as `import { A, B }` and a comment
between `import` and `{` lands inside the braces. The decorator-metadata
caveat (ignoring files with decorators under `experimentalDecorators` plus
`emitDecoratorMetadata`) is not implemented, since the rule reads no
tsconfig. There is no `jsxPragma` option; the pragma is always `React`.
