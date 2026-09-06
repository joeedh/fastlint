f<T>(x);
a?.b<T>(x);
export default expr;
export type { TT };
export { x as default };
import dx, * as ns2 from "m";
import { default as dd } from "m";
({ [k]: v } = o);
[a, b = 1, ...c] = d;
({ a = 1, b: { c } = {} } = o);
for ({ a } of list) {}
for ([a] in obj) {}
class C2 { "str"() {} 42() {} static #p = 1; get [k]() { return 1; } }
type T1 = A extends B ? C : D;
type T2 = { readonly [K in keyof T as `p${K}`]?: T[K] };
type T3 = { -readonly [K in T]-?: T[K] };
type T4 = [a: string, b?: number, ...rest: boolean[]];
type T5 = [string, number?, ...boolean[]];
type T6 = typeof x;
type T7 = keyof T;
type T8 = unique symbol;
type T9 = readonly string[];
type T10 = T["k"];
type T11 = (a: number, ...r: string[]) => void;
type T12 = new (a: number) => T;
type T13 = abstract new () => T;
type T14 = T extends (infer U extends string) ? U : never;
type T15 = (x: any) => x is string;
type T16 = (x: any) => asserts x;
type T17 = (x: any) => asserts x is string;
type T18 = this;
type T19 = import("mod").X<T>;
type T20 = typeof import("mod");
type T21 = "lit" | 1 | -1 | true | null | undefined | 1n;
type T22 = A.B.C<D>;
type T23 = (A | B)[];
type T24 = { a: string; b(): void; readonly [k: string]: any; new (): T; (): void; get x(): number; set x(v: number) };
type T25 = `a${string}b`;
type T26 = typeof x.y;
type T27 = A & B;
type T28<in out T, const U> = T;
function fo(this: void, x?: number, y = 1, { z }: Z, [w]: W, ...rest) {}
const arrow = async <T,>(a: T): Promise<T> => a;
const single = x => x;
const body = () => { return 1; };
const obj2 = { async m() {}, *g() {}, async *ag() {}, get: 1, set: 2, async: 3 };
label: { break label; }
x = y ? z : w;
x ||= 1; x &&= 2; x ??= 3; x >>>= 1;
let tpl = `plain`;
let tpl2 = `${a}`;
let nested = `a${`b${c}`}`;
abstract class AC { abstract get g(): number; abstract set s(v: number); abstract accessor ap: number; }
declare function df(): void;
declare module M2 { export function inner(): void; }
enum EE { A = "a", B = A + 1 }
class CD { constructor(); constructor(x?: any) {} m(): void; m(x?: any) {} }
