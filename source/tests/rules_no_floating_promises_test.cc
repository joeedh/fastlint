#include "fastlint/rules/rules.h"
#include "testing/rule_tester.h"
#include "testing/test.h"

using namespace fastlint;

TEST_TAGGED(rules_no_floating_promises, cases, "integration")
{
  test::runTypedRuleTests(rules::kNoFloatingPromises,
                          {
                              {R"(
async function test() {
  await Promise.resolve('value');
  Promise.resolve('value').then(
    () => {},
    () => {},
  );
  Promise.resolve('value')
    .then(() => {})
    .catch(() => {});
  Promise.resolve('value')
    .then(() => {})
    .catch(() => {})
    .finally(() => {});
  Promise.resolve('value').catch(() => {});
  return Promise.resolve('value');
}
)"},
                              {R"(
async function test() {
  void Promise.resolve('value');
}
)",
                               R"([{"ignoreVoid": true}])"},
                              {R"(
async function test() {
  await (async () => true)();
  (async () => true)().then(
    () => {},
    () => {},
  );
  (async () => true)().catch(() => {});
  return (async () => true)();
}
)"},
                              {R"(
async function test() {
  const x = Promise.resolve();
  const y = x.then(() => {});
  y.catch(() => {});
}
)"},
                              {R"(
async function test() {
  Math.random() > 0.5 ? Promise.resolve().catch(() => {}) : null;
}
)"},
                              {R"(
async function test() {
  (Promise.resolve().catch(() => {}), 123);
  (123,
    Promise.resolve().then(
      () => {},
      () => {},
    ));
}
)"},
                              {R"(
async function test() {
  Promise.resolve().catch(() => {}) ||
    Promise.resolve().then(
      () => {},
      () => {},
    );
}
)"},
                              {R"(
declare const promiseUnion: Promise<number> | number;
async function test() {
  await promiseUnion;
  promiseUnion.then(
    () => {},
    () => {},
  );
  promiseUnion.then(() => {}).catch(() => {});
  promiseUnion.catch(() => {});
  return promiseUnion;
}
)"},
                              {R"(
async function test() {
  class CanThen extends Promise<number> {}
  const canThen: CanThen = CanThen.resolve(2);

  await canThen;
  canThen.then(
    () => {},
    () => {},
  );
  canThen.catch(() => {});
  return canThen;
}
)"},
                              {R"(
async function test() {
  class Thenable {
    then(callback: () => void): Thenable {
      return new Thenable();
    }
  }
  const thenable = new Thenable();

  await thenable;
  thenable;
  thenable.then(() => {});
  return thenable;
}
)"},
                              {R"(
async function test() {
  class CatchableThenable {
    then(callback: () => void, callback: () => void): CatchableThenable {
      return new CatchableThenable();
    }
  }
  const thenable = new CatchableThenable();

  await thenable;
  return thenable;
}
)"},
                              {R"(
declare const returnsPromise: () => Promise<void> | null;
async function test() {
  await returnsPromise?.();
  returnsPromise()?.then(
    () => {},
    () => {},
  );
  returnsPromise()
    ?.then(() => {})
    ?.catch(() => {});
  returnsPromise()?.catch(() => {});
  return returnsPromise();
}
)"},
                              {R"(
(async () => {
  await something();
})();
)",
                               R"([{"ignoreIIFE": true}])"},
                              {R"(
function foo() {
  (async function bar() {})();
}
)",
                               R"([{"ignoreIIFE": true}])"},
                              {R"(
declare const notPromiseArray: number[];
notPromiseArray;
)"},
                          },
                          {
                              {R"(
async function test() {
  Promise.resolve('value');
  Promise.resolve('value').then(() => {});
  Promise.resolve('value').catch();
  Promise.resolve('value').finally();
}
)",
                               {{"floatingVoid", 3, 3, 3, 28},
                                {"floatingVoid", 4, 3, 4, 43},
                                {"floatingVoid", 5, 3, 5, 36},
                                {"floatingVoid", 6, 3, 6, 38}}},
                              {R"(
async function test() {
  void Promise.resolve('value');
}
)",
                               {{"floating", 3, 3, 3, 33}},
                               nullptr,
                               R"([{"ignoreVoid": false}])"},
                              {R"(
async function test() {
  Promise.resolve('value').catch(123);
}
)",
                               {{"floatingUselessRejectionHandlerVoid", 3, 3, 3, 39}}},
                              {R"(
async function test() {
  Promise.resolve('value').then(() => {}, 'nope');
}
)",
                               {{"floatingUselessRejectionHandler", 3, 3, 3, 51}},
                               nullptr,
                               R"([{"ignoreVoid": false}])"},
                              {R"(
declare const promiseArray: Array<Promise<unknown>>;
promiseArray;
)",
                               {{"floatingPromiseArrayVoid", 3, 1, 3, 14}}},
                              {R"(
declare const promiseTuple: [Promise<unknown>, number];
promiseTuple;
)",
                               {{"floatingPromiseArray", 3, 1, 3, 14}},
                               nullptr,
                               R"([{"ignoreVoid": false}])"},
                              {R"(
async function test() {
  Math.random() > 0.5 ? Promise.resolve() : null;
}
)",
                               {{"floatingVoid", 3, 3, 3, 50}}},
                              {R"(
async function test() {
  Promise.resolve() || Promise.resolve().catch(() => {});
}
)",
                               {{"floatingVoid", 3, 3, 3, 58}}},
                              {R"(
(async function () {
  await res(1);
})();
)",
                               {{"floatingVoid", 2, 1, 4, 6}}},
                              {R"(
(async function () {
  Promise.resolve();
})();
)",
                               {{"floatingVoid", 3, 3, 3, 21}},
                               nullptr,
                               R"([{"ignoreIIFE": true}])"},
                              {R"(
declare const createPromise: () => PromiseLike<number>;
createPromise();
)",
                               {{"floatingVoid", 3, 1, 3, 17}},
                               nullptr,
                               R"([{"checkThenables": true}])"},
                              {R"(
interface MyThenable {
  then(onFulfilled: () => void, onRejected: () => void): MyThenable;
}

declare function createMyThenable(): MyThenable;

createMyThenable();
)",
                               {{"floatingVoid", 8, 1, 8, 20}},
                               nullptr,
                               R"([{"checkThenables": true}])"},
                              {R"(
declare const promiseValue: Promise<number>;
async function test() {
  promiseValue.finally(() => {});
}
)",
                               {{"floatingVoid", 4, 3, 4, 34}}},
                              {R"(
const doSomething = async () => void Promise.resolve();
)",
                               {{"floating", 2, 33, 2, 55}},
                               nullptr,
                               R"([{"ignoreVoid": false}])"},
                          });
}
