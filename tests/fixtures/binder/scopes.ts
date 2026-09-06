import def, { named, type OnlyType } from "mod";
import * as ns from "mod";
import type { T1 } from "mod";

var hoisted = 1;
let counter = 0;
const fixed = "x";

function outer(a: number, b = a, { c, d: [e] }: Obj, ...rest: string[]): void {
  var inner = a + hoisted;
  counter++;
  if (inner) {
    let scoped = inner;
    var alsoHoisted = scoped;
  }
  for (let i = 0; i < rest.length; i++) {
    inner += i;
  }
  for (const item of rest) {
    use(item, alsoHoisted);
  }
  for (key in ns) {}
  switch (a) {
    case 1:
      let inCase = b;
      break;
  }
  try {
    throw e;
  } catch (err) {
    inner = err;
  }
  label: while (true) break label;
  return;
}

const arrow = (x: number) => x + counter;
const named2 = function self(n: number): number { return n ? self(n - 1) : def; };

class Base<TParam> {
  static count = 0;
  #secret = 1;
  field: TParam;
  constructor(private readonly opt: TParam, public pub?: number) {}
  method(): TParam { return this.field; }
  static { Base.count = counter; }
}
class Derived extends Base<string> implements Iface {
  override method() { return fixed; }
}
const Expr = class Named { m() { return Named; } };

interface Iface<U = string> extends Iface2<U> { prop: U; call(v: U): OnlyType; }
type Alias<V> = V extends Array<infer Item> ? Item : Derived;
type Mapped = { [K in keyof Alias<T1>]: Alias<T1>[K] };
type Fn = (p: number) => typeof counter;
type Qualified = ns.Thing;

enum Color { Red, Green = Red + 1 }
namespace NS.Inner { export const v = Color.Red; }
declare module "amb" { export const w: number; }
declare global { interface Window { fl: number; } }

export { counter, fixed as renamed, type Alias as A };
export type { Fn };
export default outer;

unknownName = 2;
[counter, { fixed: hoisted }] = pair;
