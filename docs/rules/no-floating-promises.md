# no-floating-promises

Reports a statement whose value is a promise nothing handles. In the
recommended preset. Type-aware: runs only with `--project`.

```ts
async function run() {
  fetchUser(id); // Promises must be awaited, end with a call to .catch, ...
}
```

An expression statement is checked, and so is the `void` body of an arrow
function. The expression is followed through comma operands, both branches
of a conditional, both sides of a logical operator, and the object of a
`.finally()` call. A promise is handled when it is awaited, assigned,
returned, passed to `.catch(fn)`, or passed to `.then(ok, fn)`; a `.then`
or `.catch` whose rejection handler is not callable is reported with
`floatingUselessRejectionHandler`. An array or tuple holding a promise is
reported with `floatingPromiseArray`, which has no suggestion.

A promise is the default library's `Promise`, a class or interface deriving
from it, a union of such types, or a type parameter constrained to one. A
union with one promise member counts, so `Promise<number> | number` is
checked.

## Options

- `ignoreVoid`: `true` by default. `void promise` marks the promise as
  ignored on purpose and the messages end `...Void`, offering the `void`
  suggestion. With `false` the operand of `void` is checked like any other
  expression.
- `ignoreIIFE`: `false` by default. When `true`, an immediately invoked
  function expression statement is not checked; its body still is.
- `checkThenables`: `false` by default. When `true`, an object whose `then`
  method takes two callable parameters counts as a promise, so `PromiseLike`
  and hand-written thenables are checked.

## Suggestions

`floatingFixVoid` wraps the expression in `void`; `floatingFixAwait` wraps
it in `await`, or replaces a `void` operator with `await`. Both are
suggestions rather than fixes. Parentheses are added where the operand binds
looser than a unary operator.

## Compared with ESLint

Same messages and options as typescript-eslint's `no-floating-promises`
except `allowForKnownSafePromises` and `allowForKnownSafeCalls`, which need
the type-or-value specifier machinery and are not implemented. A suggestion
is recorded but nothing applies it yet, so the `--fix` output is unchanged.
