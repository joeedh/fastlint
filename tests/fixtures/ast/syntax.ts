"use strict";
import def, { a as b, type C } from "mod" with { type: "json" };
import * as ns from "ns";
import type { T } from "t";
import eq = require("x");
import eqn = A.B;
export { b as c, def };
export * as all from "all";
export * from "star";
export default function fd<T extends U = V>(this: X, a?: number, ...rest: string[]): void {}
export const k = 1, l: number;
export type Alias<T> = T | null;
export = foo;
export as namespace NS;
let x = 1;
var y;
const { p, q: r = 2, ...s } = obj, [u, , w = 3, ...v] = arr;
using res = get();
await using ares = get();
declare const dc: number;
x = y += 2 ** 3;
a?.b?.[c]?.(d);
new Foo<T>(1, ...args);
new Bar;
import("mod", { with: {} });
import.meta.url;
new.target;
-x; +x; !x; ~x; typeof x; void x; delete x.y; ++x; x--; await p;
a ? b : c;
(a, b);
a && b || c ?? d;
a instanceof B; a in b;
`t${x}m${y}e`;
tag<T>`x`;
[1, , ...s];
({ a, b: 1, [c]: 2, ...s, m() {}, get g() {}, set g(v) {}, async *ag() {} });
o.#priv;
super.x; this.y; null; true; false; 12n; /re/g; "str";
x as T; x satisfies T; x!; <T>x; f<T>;
function* gen() { yield; yield* a; }
async (a) => a;
(a: number): string => "";
a => a;
class K<T> extends B<T> implements I, J {
  @dec static readonly f: number = 1;
  declare g?: string;
  private h!: number;
  #priv = 1;
  constructor(public a: number, private readonly b?: string) { super(); }
  static { init(); }
  @d() async *m<T>(@p x: T): Promise<void> {}
  get acc(): number { return 1; }
  set acc(v) {}
  abstract am(): void;
  accessor ap = 1;
  override om() {}
  [computed]() {}
  protected static async pm?(): Promise<void> {}
}
abstract class AK { abstract x: number; }
const ce = class Named {};
interface I<T> extends A, B<T> {
  p: number;
  q?: string;
  readonly r: T;
  m<U>(a: U): void;
  new (x: number): I;
  (y: string): void;
  [k: string]: any;
  get g(): number;
  set g(v: number);
}
enum E { A, B = 2, "C" = 3 }
const enum CE { X }
declare enum DE { Y }
namespace N.M { export const z = 1; }
module Mod { }
declare module "amb" { }
declare global { }
if (a) b; else if (c) d; else { e; }
for (let i = 0; i < 10; i++) {}
for (;;) break;
for (const k in o) continue;
for await (const v of it) ;
lbl: for (const v of it) break lbl;
while (x) {}
do {} while (x);
switch (x) { case 1: case 2: f(); break; default: g(); }
try { a } catch (e: unknown) { b } finally { c }
try {} catch {}
throw new Error();
return;
with (o) {}
debugger;
;
