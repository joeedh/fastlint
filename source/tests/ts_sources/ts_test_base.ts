import * as util from "../../util/util";
import { ToolPropertyIF, PropTypes, PropFlags } from "../toolprop_abstract";
import nstructjs from "../../util/struct";
import type { StructReader } from "../../util/nstructjs";
import type { JSONAny } from "../../controller";

export const TOOLPROP_SCHEMA_VERSION = 2;

declare global {
  interface SymbolConstructor {
    readonly dispose: symbol;
  }
}

export type EnumDef = Record<string, number | string>;
export type FlagsDef = Record<string, number>;
export type IconMap = Record<string, number>;
export type DescriptionMap = Record<string, string>;
export type UINameMap = Record<string, string>;

export type NumberConstraintBase =
  | "range"
  | "expRate"
  | "step"
  | "uiRange"
  | "displayUnit"
  | "baseUnit"
  | "stepIsRelative"
  | "slideSpeed"
  | "sliderDisplayExp";
export type IntegerConstraint = NumberConstraintBase | "radix";
export type FloatConstraint = NumberConstraintBase | "decimalPlaces";
export type NumberConstraint = IntegerConstraint | FloatConstraint;

export const NumberConstraintsBase: Set<NumberConstraintBase> = new Set<NumberConstraintBase>([
  "range",
  "expRate",
  "step",
  "uiRange",
  "baseUnit",
  "displayUnit",
  "stepIsRelative",
  "slideSpeed",
  "sliderDisplayExp",
]);

export const IntegerConstraints: Set<IntegerConstraint> = new Set<IntegerConstraint>(
  (["radix"] as IntegerConstraint[]).concat(util.list(NumberConstraintsBase) as IntegerConstraint[])
);

export const FloatConstrinats: Set<FloatConstraint> = new Set<FloatConstraint>(
  (["decimalPlaces"] as FloatConstraint[]).concat(
    util.list(NumberConstraintsBase) as FloatConstraint[]
  )
);

export const NumberConstraints: Set<NumberConstraint> = new Set<NumberConstraint>(
  (util.list(IntegerConstraints) as NumberConstraint[]).concat(
    util.list(FloatConstrinats) as NumberConstraint[]
  )
);

export const PropSubTypes: Record<string, number> = {
  COLOR: 1,
};

export type CallbackFn = (this: ToolProperty<unknown>, arg1?: unknown, arg2?: unknown) => void;

export type UtilStringSet = util.set<any>;

//set PropTypes to custom type integers
export function setPropTypes(types: Record<string, number>): void {
  for (const k in types) {
    (PropTypes as any)[k] = types[k];
  }
}

export interface ToolPropertyConstructor {
  PROP_TYPE_ID?: number;
}

export const customPropertyTypes: ToolPropertyConstructor[] = [];
export const PropClasses: Record<number, ToolPropertyConstructor> = {};

let customPropTypeBase: number = 17;

export const MakeUINameWordMap: Record<string, string> = {
  sel  : "select",
  unsel: "deselect",
  eid  : "id",
  props: "properties",
  res  : "resource",
};

// XX todo: investivate (i.e. test) dynamic binding of var vs let
// eslint-disable-next-line no-var
export var defaultRadix: number = 10;
// eslint-disable-next-line no-var
export var defaultDecimalPlaces: number = 4;

export class OnceTag {
  cb: CallbackFn;

  constructor(cb: CallbackFn) {
    this.cb = cb;
  }
}

export interface DataAPIExecScope {
  ctx?: unknown;
  dataref?: unknown;
  datapath?: string;
}

export class ExecScopeUsing {
  private oldScope: DataAPIExecScope = {};
  private prop?: DataAPIExecScope;

  init(prop: DataAPIExecScope) {
    this.prop = prop;
    this.oldScope.ctx = prop.ctx;
    this.oldScope.datapath = prop.datapath;
    this.oldScope.dataref = prop.dataref;
    return this;
  }

  public get ctx() {
    return this.prop!.ctx;
  }
  public set ctx(v) {
    this.prop!.ctx = v;
  }

  public get dataref() {
    return this.prop!.dataref;
  }
  public set dataref(v) {
    this.prop!.dataref = v;
  }

  public get datapath() {
    return this.prop!.datapath;
  }
  public set datapath(v) {
    this.prop!.datapath = v;
  }

  [Symbol.dispose]() {
    const prop = this.prop!;
    const oldScope = this.oldScope!;

    prop.ctx = oldScope.ctx;
    prop.datapath = oldScope.datapath;
    prop.dataref = oldScope.dataref;

    execScopeUsingStack._popStack();
  }
}

export class ExecScopeUsingStack extends Array<ExecScopeUsing> {
  depth = 0;

  constructor(size: number) {
    super(size);
    this.length = size;
    for (let i = 0; i < size; i++) {
      this[i] = new ExecScopeUsing();
    }
  }

  withScope(prop: DataAPIExecScope) {
    return this.pushStack().init(prop);
  }

  private pushStack() {
    return this[this.depth++];
  }
  public _popStack() {
    this.depth--;
  }
}
// eslint-disable-next-line no-var
export var execScopeUsingStack = new ExecScopeUsingStack(512);

/* Both are declared on a merged interface rather than in the class body; see
   ToolPropertyIF.  A bare `ctx?: unknown` field emits an own `ctx = undefined`
   under this tsconfig, which shadowed the get/set ctx accessors on
   subclasses. */
export interface ToolProperty<T = unknown, TYPE extends number = number> {
  data: T;

  ctx?: unknown;
}

export class ToolProperty<T = unknown, TYPE extends number = number>
  extends ToolPropertyIF<TYPE>
  implements DataAPIExecScope
{
  static STRUCT = nstructjs.inlineRegister(
    this,
    `
ToolProperty {
  apiname        : string | ""+this.apiname;
  type           : int;
  flag           : int;
  subtype       ?: int | this.subtype ? this.subtype : 0;
  icon           : int;
  icon2          : int;
  baseUnit       : string | ""+this.baseUnit;
  displayUnit    : string | ""+this.displayUnit;
  range          : array(float) | this.range ? this.range : [-1e17, 1e17];
  uiRange        : array(float) | this.uiRange ? this.uiRange : [-1e17, 1e17];
  description    : string;
  stepIsRelative : bool;
  step           : float;
  expRate        : float;
  radix          : float;
  decimalPlaces  : int;
  uiname         : string | this.uiname || this.apiname || "";
  wasSet         : bool;
  schemaVersion  : int;        
}`
  );

  static PROP_TYPE_ID: number;

  declare subtype: number | undefined;
  wasSet: boolean;
  declare apiname: string | undefined;
  declare uiname: string | undefined;
  declare description: string | undefined;
  declare flag: number;
  declare icon: number;
  icon2: number;

  schemaVersion = TOOLPROP_SCHEMA_VERSION;

  decimalPlaces: number;
  radix: number;
  step: number;

  range: [number, number] | undefined;
  uiRange: [number, number] | undefined;
  baseUnit: string | undefined;
  displayUnit: string | undefined;
  stepIsRelative: boolean;
  expRate: number;
  slideSpeed: number;

  callbacks: Record<string, (CallbackFn | OnceTag)[]>;

  /* These are used in subclasses but accessed generically */
  update?: Function;
  api_update?: Function;

  // these fields are used by the data api system
  dataref?: unknown;
  datapath?: string;

  constructor(
    type?: TYPE,
    subtype?: number,
    apiname?: string,
    uiname: string = "",
    description: string = "",
    flag: number = 0,
    icon: number = -1
  ) {
    super(type);

    this.type = type!;
    this.subtype = subtype;

    //is false if this property still has its default value,
    //i.e. it hasn't been set by the user or anyone else
    this.wasSet = false;

    this.apiname = apiname;
    this.uiname = uiname !== undefined ? uiname : apiname;
    this.description = description;
    this.flag = flag | PropFlags.SAVE_LAST_VALUE;
    this.icon = icon;
    this.icon2 = icon; //another icon, e.g. unchecked state

    //remember to update NumberConstraintsBase et al when adding new number
    //constraints

    this.decimalPlaces = defaultDecimalPlaces;
    this.radix = defaultRadix;
    this.step = 0.05;
    this.stepIsRelative = false;
    this.expRate = 1.33;
    this.slideSpeed = 1.0;

    this.callbacks = {};
  }

  /**
   * Validates and optionally transforms arg, see parseArgs in ToolOp.
   * This is used for e.g. enum/flag property parsing.
   */
  parseArg(arg: unknown): unknown {
    // base class
    return arg;
  }

  getUIName() {
    return this.uiname ?? ToolProperty.makeUIName(this.apiname ?? "error");
  }

  /** Get a data api execution context stack ( for use with the using keyword) */
  execWithContext() {
    return execScopeUsingStack.withScope(this);
  }
  static internalRegister(cls: any): void {
    PropClasses[new cls().type] = cls;
  }

  static getClass(type: number) {
    return PropClasses[type];
  }

  static setDefaultRadix(n: number): void {
    defaultRadix = n;
  }

  static setDefaultDecimalPlaces(n: number): void {
    defaultDecimalPlaces = n;
  }

  static makeUIName(name: string): string {
    const parts: string[] = [""];
    let lastc: string | undefined = undefined;

    const ischar = (c: string): boolean => {
      const code = c.charCodeAt(0);

      let upper = code >= "A".charCodeAt(0);
      upper = upper && code <= "Z".charCodeAt(0);

      let lower = code >= "a".charCodeAt(0);
      lower = lower && code <= "z".charCodeAt(0);

      return upper || lower;
    };

    for (let i = 0; i < name.length; i++) {
      const c = name[i];

      if (c === "_" || c === "-" || c === "$") {
        lastc = c;
        parts.push("");
        continue;
      }

      if (i > 0 && c === c.toUpperCase() && lastc !== lastc!.toUpperCase()) {
        if (ischar(c) && ischar(lastc!)) {
          parts.push("");
        }
      }

      parts[parts.length - 1] += c;
      lastc = c;
    }

    const subst = (word: string): string => {
      if (word in MakeUINameWordMap) {
        return MakeUINameWordMap[word];
      } else {
        return word;
      }
    };

    const result = parts
      .filter((f) => f.trim().length > 0)
      .map((f) => subst(f))
      .map((f) => f[0].toUpperCase() + f.slice(1, f.length).toLowerCase())
      .join(" ")
      .trim();
    return result;
  }

  static register(cls: any): number {
    cls.PROP_TYPE_ID = 1 << customPropTypeBase;
    (PropTypes as any)[cls.name] = cls.PROP_TYPE_ID;

    customPropTypeBase++;
    customPropertyTypes.push(cls);

    PropClasses[new cls().type] = cls;

    return cls.PROP_TYPE_ID;
  }

  static calcRelativeStep(step: number, value: number, logBase: number = 1.5): number {
    value = Math.log(Math.abs(value) + 1.0) / Math.log(logBase);
    value = Math.max(value, step);

    console.warn(util.termColor("STEP", "red"), value);
    return value;
  }

  setDescription(s: string): this {
    this.description = s;
    return this;
  }

  setUIName(s: string): this {
    this.uiname = s;
    return this;
  }

  calcMemSize(): number {
    function strlen(s: string | undefined): number {
      //length of string plus an assumed member pointer
      return s !== undefined ? s.length + 8 : 8;
    }

    let tot = 0;

    tot += strlen(this.apiname) + strlen(this.uiname);
    tot += strlen(this.description);

    tot += 11 * 8; //assumed member pointers
    tot += Object.keys(this.callbacks).length * 24;

    return tot;
  }

  equals(b: this): boolean {
    throw new Error("implement me");
  }

  setReadOnly() {
    this.flag |= PropFlags.READ_ONLY;
    this.flag &= ~PropFlags.SAVE_LAST_VALUE;
    return this;
  }

  private(): this {
    this.flag |= PropFlags.PRIVATE;
    this.flag &= ~PropFlags.SAVE_LAST_VALUE;
    return this;
  }

  /** Save property in last value cache.  Now set by default,
   *  to disable use .ignoreLastValue().
   */
  saveLastValue(): this {
    this.flag |= PropFlags.SAVE_LAST_VALUE;
    return this;
  }

  ignoreLastValue(): this {
    this.flag &= ~PropFlags.SAVE_LAST_VALUE;
    return this;
  }

  report(...args: unknown[]): void {
    console.warn(...args);
  }

  _fire(type: string, arg1?: unknown, arg2?: unknown): this {
    if (this.callbacks[type] === undefined) {
      return this;
    }

    let stack = this.callbacks[type];
    stack = stack.concat([]); //copy

    for (let i = 0; i < stack.length; i++) {
      const cb = stack[i];

      if (cb instanceof OnceTag) {
        let j = i;

        //remove callback;
        while (j < stack.length - 1) {
          stack[j] = stack[j + 1];
          j++;
        }

        (stack as (CallbackFn | OnceTag | undefined)[])[j] = undefined;
        stack.length--;

        i--;

        cb.cb.call(this, arg1, arg2);
      } else {
        cb.call(this, arg1, arg2);
      }
    }

    return this;
  }

  clearEventCallbacks(): this {
    this.callbacks = {};
    return this;
  }

  once(type: string, cb: CallbackFn): this {
    if (this.callbacks[type] === undefined) {
      this.callbacks[type] = [];
    }

    //check if cb is already in callback list inside a OnceTag
    for (const cb2 of this.callbacks[type]) {
      if (cb2 instanceof OnceTag && cb2.cb === cb) {
        return this;
      }
    }

    const tag = new OnceTag(cb);

    this.callbacks[type].push(tag);

    return this;
  }

  on(type: string, cb: CallbackFn): this {
    if (this.callbacks[type] === undefined) {
      this.callbacks[type] = [];
    }

    this.callbacks[type].push(cb);
    return this;
  }

  off(type: string, cb: CallbackFn): this {
    (this.callbacks[type] as unknown as CallbackFn[]).remove(cb);
    return this;
  }

  toJSON(): JSONAny {
    return {
      type       : this.type,
      subtype    : this.subtype,
      apiname    : this.apiname,
      uiname     : this.uiname,
      description: this.description,
      flag       : this.flag,
      icon       : this.icon,
      data       : this.data,
      range      : this.range,
      uiRange    : this.uiRange,
      step       : this.step,
    };
  }

  loadJSON(obj: Record<string, unknown>): this {
    this.type = obj.type as TYPE;
    this.subtype = obj.subtype as number | undefined;
    this.apiname = obj.apiname as string | undefined;
    this.uiname = obj.uiname as string | undefined;
    this.description = obj.description as string | undefined;
    this.flag = obj.flag as number;
    this.icon = obj.icon as number;
    this.data = obj.data as T;

    return this;
  }

  getValue(): T {
    return this.data as T;
  }

  /** Marks the property set and fires `change`. Every subclass stores the value before
   * chaining here, so the argument a listener receives is read back through `getValue`. */
  setValue(val?: T): void {
    if (this.constructor === ToolProperty) {
      throw new Error("implement me!");
    }

    this.wasSet = true;
    this._fire("change", this.getValue());
  }

  copyTo(b: ToolProperty<T, TYPE>): void {
    b.apiname = this.apiname;

    b.uiname = this.uiname;
    b.description = this.description;
    b.icon = this.icon;
    b.icon2 = this.icon2;

    b.baseUnit = this.baseUnit;
    b.subtype = this.subtype;
    b.displayUnit = this.displayUnit;

    b.flag = this.flag;

    for (const k in this.callbacks) {
      b.callbacks[k] = this.callbacks[k];
    }
  }

  copy(): ToolProperty<T, TYPE> {
    //default copy method
    const ret = new (this.constructor as any)();

    this.copyTo(ret as this);

    return ret as this;
  }

  setStep(step: number): this {
    this.step = step;
    return this;
  }

  getStep(value: number = 1.0): number {
    if (this.stepIsRelative) {
      return ToolProperty.calcRelativeStep(this.step, value);
    } else {
      return this.step;
    }
  }

  setRelativeStep(step: number): this {
    this.step = step;
    this.stepIsRelative = true;
    return this;
  }

  setRange(min: number, max: number): this {
    if (min === undefined || max === undefined) {
      throw new Error("min and/or max cannot be undefined");
    }

    this.range = [min, max] as [number, number];
    return this;
  }

  noUnits(): this {
    this.baseUnit = this.displayUnit = "none";
    return this;
  }

  setBaseUnit(unit: string): this {
    this.baseUnit = unit;
    return this;
  }

  setDisplayUnit(unit: string): this {
    this.displayUnit = unit;
    return this;
  }

  setUnit(unit: string): this {
    this.baseUnit = this.displayUnit = unit;
    return this;
  }

  setFlag(f: number, combine: boolean = false): this {
    this.flag = combine ? this.flag | f : f;
    return this;
  }

  setUIRange(min: number, max: number): this {
    if (min === undefined || max === undefined) {
      throw new Error("min and/or max cannot be undefined");
    }

    this.uiRange = [min, max] as [number, number];
    return this;
  }

  setIcon(icon: number): this {
    this.icon = icon;

    return this;
  }

  setIcon2(icon: number): this {
    this.icon2 = icon;

    return this;
  }

  /** Sets whether sliders/textboxes/etc send updates in real time or wait for editing to stop. */
  setRealtime(realtime: boolean): this {
    if (!realtime) {
      this.flag |= PropFlags.NO_REALTIME;
    } else {
      this.flag &= ~PropFlags.NO_REALTIME;
    }
    return this;
  }

  setOptional(state = true): this {
    if (state) {
      this.flag |= PropFlags.OPTIONAL;
    } else {
      this.flag &= ~PropFlags.OPTIONAL;
    }
    return this;
  }

  loadSTRUCT(reader: StructReader<this>): void {
    // catch old files without a schemaVersion
    // (this can bite on binary files).
    this.schemaVersion = 0;
    reader(this);

    if (this.uiRange?.[0] === -1e17 && this.uiRange[1] === 1e17) {
      this.uiRange = undefined;
    }

    if (this.baseUnit === "undefined") {
      this.baseUnit = undefined;
    }

    if (this.displayUnit === "undefined") {
      this.displayUnit = undefined;
    }

    // An unset name is written through ""+, so it arrives as the string "undefined". A widget
    // labelling itself from uiname would print that word.
    if (this.apiname === "undefined") {
      this.apiname = undefined;
    }

    if (this.uiname === "undefined") {
      this.uiname = undefined;
    }
  }

  static getVersionSTRUCT(jsonOrProp: JSONAny): number {
    return (jsonOrProp.schemaVersion as number) ?? 0;
  }
  static migrateSTRUCT(
    schemaVersion: number,
    jsonOrProp: JSONAny,
    migrate: nstructjs.StructMigrateFinisher
  ) {
    if (!jsonOrProp.schemaVersion) {
      jsonOrProp.schemaVersion = schemaVersion;
    }
    migrate();
    jsonOrProp.schemaVersion = TOOLPROP_SCHEMA_VERSION;
  }
}
