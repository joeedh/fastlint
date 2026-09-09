// Presents the Emscripten module as the `Addon` the rule runtime consumes (task
// 7.3), so `lint` runs unchanged over the WASM heap. Handles are heap pointers,
// which cross to JS as numbers; a node stays valid until its session is freed.

import { pathToFileURL } from "node:url";

import type { Addon } from "./runtime.ts";

/** The Emscripten module surface this shim reads. Heap views are re-read on
 * every use, because `ALLOW_MEMORY_GROWTH` can swap the backing buffer. */
interface WasmModule {
  _fl_wasm_parse(source: number, filename: number): number;
  _fl_wasm_session_free(session: number): void;
  _fl_wasm_root(session: number): number;
  _fl_wasm_kind(node: number): number;
  _fl_wasm_flags(node: number): number;
  _fl_wasm_parent(node: number): number;
  _fl_wasm_child_count(node: number): number;
  _fl_wasm_child(node: number, index: number): number;
  _fl_wasm_text_ptr(node: number): number;
  _fl_wasm_text_len(node: number): number;
  _fl_wasm_data_byte(node: number, byte: number): number;
  _fl_wasm_start(node: number): number;
  _fl_wasm_end(node: number): number;
  _fl_wasm_descendants(
    session: number,
    node: number,
    kind: number,
    outCount: number
  ): number;
  _fl_wasm_free(ptr: number): void;
  _malloc(size: number): number;
  _free(ptr: number): void;
  stringToNewUTF8(text: string): number;
  readonly HEAPU8: Uint8Array;
  readonly HEAPU32: Uint32Array;
  readonly HEAP32: Int32Array;
}

const decoder = new TextDecoder();

/** The addon a WASM handle is null when its pointer is zero. */
function orNull(pointer: number): number | null {
  return pointer === 0 ? null : pointer;
}

/** Wraps a loaded module as an `Addon`. Sessions and nodes are numbers. */
function addonFor(wasm: WasmModule): Addon {
  const num = (handle: unknown): number => handle as number;
  return {
    parse(source, filename) {
      const sourcePtr = wasm.stringToNewUTF8(source);
      const filenamePtr = wasm.stringToNewUTF8(filename ?? "input.ts");
      const session = wasm._fl_wasm_parse(sourcePtr, filenamePtr);
      wasm._free(sourcePtr);
      wasm._free(filenamePtr);
      return session;
    },
    root: (session) => wasm._fl_wasm_root(num(session)),
    kind: (handle) => wasm._fl_wasm_kind(num(handle)),
    flags: (handle) => wasm._fl_wasm_flags(num(handle)) >>> 0,
    parent: (handle) => orNull(wasm._fl_wasm_parent(num(handle))),
    childCount: (handle) => wasm._fl_wasm_child_count(num(handle)),
    child: (handle, index) => orNull(wasm._fl_wasm_child(num(handle), index)),
    text(handle) {
      const ptr = wasm._fl_wasm_text_ptr(num(handle));
      const len = wasm._fl_wasm_text_len(num(handle));
      if (ptr === 0 || len === 0) return "";
      return decoder.decode(wasm.HEAPU8.subarray(ptr, ptr + len));
    },
    dataByte: (handle, byte) => wasm._fl_wasm_data_byte(num(handle), byte),
    start: (handle) => wasm._fl_wasm_start(num(handle)) >>> 0,
    end: (handle) => wasm._fl_wasm_end(num(handle)) >>> 0,
    descendants(session, handle, kind) {
      const countPtr = wasm._malloc(4);
      const array = wasm._fl_wasm_descendants(num(session), num(handle), kind, countPtr);
      const count = wasm.HEAP32[countPtr >> 2] ?? 0;
      wasm._free(countPtr);
      const out: number[] = [];
      for (let i = 0; i < count; i++) {
        out.push(wasm.HEAPU32[(array >> 2) + i]!);
      }
      if (array !== 0) wasm._fl_wasm_free(array);
      return out;
    },
    freeSession: (session) => wasm._fl_wasm_session_free(num(session)),
  };
}

/**
 * Loads the module built at `modulePath` (the generated `fastlint.js`) and
 * returns it as an `Addon`. Its `freeSession` releases a parsed tree, which
 * `lint` calls for it; the WASM heap has no finalizer to do so on its own.
 */
export async function loadWasmAddon(modulePath: string): Promise<Addon> {
  const factory = (await import(pathToFileURL(modulePath).href)) as {
    default: () => Promise<WasmModule>;
  };
  const wasm = await factory.default();
  return addonFor(wasm);
}
