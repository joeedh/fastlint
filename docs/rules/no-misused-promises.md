# no-misused-promises

Reports a Promise used where a non-Promise value is expected. In the
recommended preset. Type-aware: runs only with `--project`.

```ts
if (Promise.resolve()) {
} // Expected non-Promise value in a boolean conditional.
```

Four misuse shapes are checked:

- **Conditionals** (`conditional`) — a Promise as the test of an `if`, `while`,
  `for`, `do`/`while` or ternary, the argument of `!`, or a condition operand
  of `&&`/`||`. A value is flagged when every member of its type is thenable, so
  a `Promise<T> | undefined` union used as a nullish check is left alone.
- **Spreads** (`spread`) — a value spread with `...` when any member of its type
  is thenable.
- **Void-return arguments** (`voidReturnArgument`) — an async or
  Promise-returning function passed as a call argument whose parameter expects a
  function returning `void`.
- **Void-return variables** (`voidReturnVariable`) — a Promise-returning
  function assigned to, or used to initialize, a variable whose type is a
  `void`-returning function. The variable declaration is checked through the
  initializer's contextual type, since a binding name carries no type of its
  own.

A parameter or variable whose function type also admits a thenable return is
not flagged, since a promise is valid there.

## Options

- `checksConditionals` (default on): a boolean, or `{ flagUnions }`. `flagUnions`
  is `none` by default (flag only an always-thenable value); `all` also flags a
  value that is sometimes thenable. The `strict` setting is not ported.
- `checksSpreads` (default on).
- `checksVoidReturn` (default on): a boolean, or an object whose `arguments` and
  `variables` sub-flags (both default on) turn off those two checks. The other
  sub-flags name checks that are not ported.

## Compared with ESLint

The conditional, spread, void-return-argument and void-return-variable checks
match upstream. Not ported: the `voidReturnProperty`, `voidReturnReturnValue`,
`voidReturnAttribute` (JSX) and `voidReturnInheritedMethod` checks, which need
contextual typing of object properties and return positions and a heritage-type
walk; the `predicate` check on array-method callbacks; the `strict` `flagUnions`
mode, which compares a union's non-promise members against the promise's awaited
type; the `using`/dispose well-known-symbol cases; and rest-parameter void
spreading and the overload preference `getResolvedSignature` avoids (this port
uses the resolved signature, so an overloaded call that mixes `() => void` and
`() => Promise<void>` may differ).
