#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST_TAGGED(rules_await_thenable, cases, "integration")
{
  test::runTypedRuleTests(rules::kAwaitThenable,
                          {
                              {R"(
async function test() {
  await Promise.resolve('value');
  await Promise.reject(new Error('message'));
}
)"},
                              {R"(
async function test() {
  await (async () => true)();
}
)"},
                              {R"(
async function test() {
  function returnsPromise() {
    return Promise.resolve('value');
  }
  await returnsPromise();
}
)"},
                              {R"(
async function test() {
  let anyValue: any;
  await anyValue;
}
)"},
                              {R"(
async function test() {
  let unknownValue: unknown;
  await unknownValue;
}
)"},
                              {R"(
async function test() {
  class Foo extends Promise<number> {}
  const foo: Foo = Foo.resolve(2);
  await foo;

  class Bar extends Foo {}
  const bar: Bar = Bar.resolve(2);
  await bar;
}
)"},
                              {R"(
declare const numberPromise: Promise<number>;
async function test() {
  await (Math.random() > 0.5 ? numberPromise : 0);

  const intersectionPromise: Promise<number> & number = null!;
  await intersectionPromise;
}
)"},
                              {R"(
async function test() {
  class Thenable {
    then(callback: () => {}) {}
  }
  const thenable = new Thenable();

  await thenable;
}
)"},
                              {R"(
async function* asyncYieldNumbers() {
  yield 1;
  yield 2;
  yield 3;
}
for await (const value of asyncYieldNumbers()) {
  console.log(value);
}
)"},
                              {R"(
declare const anee: any;
async function forAwait() {
  for await (const value of anee) {
    console.log(value);
  }
}
)"},
                              {R"(
declare const asyncIter: AsyncIterable<string> | Iterable<string>;
for await (const s of asyncIter) {
}
)"},
                              {R"(
async function wrapper<T>(value: T) {
  return await value;
}
)"},
                              {R"(
async function wrapper<T extends unknown>(value: T) {
  return await value;
}
)"},
                              {R"(
async function wrapper<T extends Promise<unknown>>(value: T) {
  return await value;
}
)"},
                              {R"(
async function wrapper<T extends number | Promise<unknown>>(value: T) {
  return await value;
}
)"},
                              {R"(
class C<R> {
  async wrapper<T extends R>(value: T) {
    return await value;
  }
}
)"},
                              {R"(
Promise.all([,]);
)"},
                              {R"(
declare const x: any;
Promise.all(x);
)"},
                              {R"(
declare const x: Array<Promise<unknown>>;
Promise.all(x);
)"},
                              {R"(
declare const x: Array<Promise<number>> | Array<Promise<string>>;
Promise.all(x);
)"},
                              {R"(
function f<T extends Promise<unknown>>(x: Array<T>) {
  Promise.all(x);
}
)"},
                              {R"(
declare const x: Array<unknown>;
Promise.all(x);
)"},
                              {R"(
declare const x: [Promise<unknown>, Promise<void>];
Promise.all(x);
)"},
                              {R"(
function* x() {
  yield Promise.resolve(1);
}

Promise.all(x());
)"},
                              {R"(
declare const x: ReadonlyArray<Promise<number>>;
Promise.all(x);
)"},
                              {R"(
declare const _unknown_: unknown;

Promise.all([
  _unknown_,
  Promise.resolve(1),
  Promise.resolve(2),
  Promise.resolve(3),
]);
)"},
                              {R"(
Promise.all([
  Promise.resolve(1),
  ...[Promise.resolve(4), Promise.resolve(5), Promise.resolve(6)],
]);
)"},
                              {R"(
declare const maybePromise: Promise<number> | number;

Promise.all([maybePromise, Promise.resolve(1)]);
)"},
                              {R"(
class MyPromise extends Promise<number> {}
declare const x: Array<number>;
MyPromise.all(x);
)"},
                          },
                          {
                              {"await 0;", {{"await", 1, 1, 1, 8}}},
                              {"await 'value';", {{"await", 1, 1, 1, 14}}},
                              {"async () => await (Math.random() > 0.5 ? '' : 0);",
                               {{"await", 1, 13, 1, 49}}},
                              {R"(
class NonPromise extends Array {}
await new NonPromise();
)",
                               {{"await", 3, 1, 3, 23}}},
                              {R"(
async function test() {
  class IncorrectThenable {
    then() {}
  }
  const thenable = new IncorrectThenable();

  await thenable;
}
)",
                               {{"await", 8, 3, 8, 17}}},
                              {R"(
declare const callback: (() => void) | undefined;
await callback?.();
)",
                               {{"await", 3, 1, 3, 19}}},
                              {R"(
declare const obj: { a: { b: { c?: () => void } } } | undefined;
await obj?.a.b.c?.();
)",
                               {{"await", 3, 1, 3, 21}}},
                              {R"(
function* yieldNumbers() {
  yield 1;
  yield 2;
  yield 3;
}
for await (const value of yieldNumbers()) {
  console.log(value);
}
)",
                               {{"forAwaitOfNonAsyncIterable", 7, 1, 7, 42}}},
                              {R"(
function* yieldNumberPromises() {
  yield Promise.resolve(1);
}
for await (const value of yieldNumberPromises()) {
  console.log(value);
}
)",
                               {{"forAwaitOfNonAsyncIterable", 5, 1, 5, 49}}},
                              {R"(
async function wrapper<T extends number>(value: T) {
  return await value;
}
)",
                               {{"await", 3, 10, 3, 21}}},
                              {R"(
class C<R extends number> {
  async wrapper<T extends R>(value: T) {
    return await value;
  }
}
)",
                               {{"await", 4, 12, 4, 23}}},
                              {R"(
declare const x: Array<number>;
Promise.all(x);
)",
                               {{"invalidPromiseAggregatorInput", 3, 13, 3, 14}}},
                              {R"(
declare const x: Array<number> | Array<Promise<number>>;
Promise.race(x);
)",
                               {{"invalidPromiseAggregatorInput", 3, 14, 3, 15}}},
                              {R"(
declare const x: Array<number> | Array<string>;
Promise.allSettled(x);
)",
                               {{"invalidPromiseAggregatorInput", 3, 20, 3, 21}}},
                              {R"(
declare const x: [number, Promise<number>];
Promise.any(x);
)",
                               {{"invalidPromiseAggregatorInput", 3, 13, 3, 14}}},
                              {R"(
Promise.all([0, Promise.resolve(1), 'two']);
)",
                               {{"invalidPromiseAggregatorInput", 2, 14, 2, 15},
                                {"invalidPromiseAggregatorInput", 2, 37, 2, 42}}},
                          });
}
