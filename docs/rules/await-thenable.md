# await-thenable

Reports an `await` whose operand can never be a promise, and the related
misuses of `for await`, `await using` and the `Promise` aggregators. In the
recommended preset. Type-aware: runs only with `--project`.

```ts
await 0; // Unexpected `await` of a non-Promise (non-"Thenable") value.
```

The operand's type decides. A type with a `then` method whose first
parameter is callable is a thenable and must be awaited. `any`, `unknown`
and an unconstrained type parameter may be promises and pass. A type
parameter is judged by its base constraint. Anything else is reported with
`await`, with the `removeAwait` suggestion.

- `for await (... of x)` is reported with `forAwaitOfNonAsyncIterable` when
  no union member of `x`'s type has a `[Symbol.asyncIterator]` property;
  `any` passes. The report covers the loop head, and the
  `convertToOrdinaryFor` suggestion drops the `await`.
- `await using x = init` is reported at `init` with
  `awaitUsingOfNonAsyncDisposable` when no union member of its type has a
  `[Symbol.asyncDispose]` property; `any` passes. With a single declarator
  the `removeAwait` suggestion turns the declaration into `using`.
- `Promise.all`, `allSettled`, `race` and `any` (on the default library's
  `PromiseConstructor` or a class deriving from it) are checked for
  `invalidPromiseAggregatorInput`. An array literal argument reports each
  element whose type is never awaitable. Any other argument is reported when
  it is iterable and some union member's element type contains a
  never-awaitable member: tuple elements, the `number` index type of an
  array-like, or the first type argument of another generic.

## Options

None.

## Compared with ESLint

Same messages as typescript-eslint's `await-thenable`. The suggestions are
recorded but nothing applies them yet, so `removeAwait` prints `0;` for
`await 0;` where upstream keeps the space. A parameter with a rest
element is treated like any other in the thenable check, where upstream
looks at its element type.
