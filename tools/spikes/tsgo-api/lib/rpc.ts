import { spawn, type ChildProcess } from "node:child_process";

export type Json = unknown;

export interface RpcStats {
  calls: number;
  bytesSent: number;
  bytesReceived: number;
}

/** A connection to one `tsc --api` server process. */
export interface Rpc {
  readonly transport: string;
  readonly stats: RpcStats;
  call(method: string, params?: Json): Promise<Json>;
  close(): Promise<void>;
}

export class RpcError extends Error {}

export interface SpawnOptions {
  exe: string;
  cwd: string;
  extraArgs?: readonly string[];
  onCallback?: (method: string, params: Json) => Json;
}

function spawnServer(opts: SpawnOptions, args: readonly string[]): ChildProcess {
  return spawn(
    opts.exe,
    ["--api", `--cwd=${opts.cwd}`, ...args, ...(opts.extraArgs ?? [])],
    {
      stdio: ["pipe", "pipe", "inherit"],
    }
  );
}

/**
 * JSON-RPC 2.0 over LSP `Content-Length` framing, as served by `tsc --api --async`.
 * Requests carry integer ids, so many may be in flight at once.
 */
export class JsonRpcClient implements Rpc {
  readonly transport = "jsonrpc";
  readonly stats: RpcStats = { calls: 0, bytesSent: 0, bytesReceived: 0 };

  private child: ChildProcess;
  private buf: Buffer<ArrayBufferLike> = Buffer.alloc(0);
  private nextId = 1;
  private pending = new Map<
    number,
    { resolve: (v: Json) => void; reject: (e: Error) => void }
  >();
  private onCallback: SpawnOptions["onCallback"];

  constructor(opts: SpawnOptions) {
    this.onCallback = opts.onCallback;
    this.child = spawnServer(opts, ["--async"]);
    this.child.stdout!.on("data", (chunk: Buffer) => this.onData(chunk));
  }

  private onData(chunk: Buffer): void {
    this.stats.bytesReceived += chunk.length;
    this.buf = this.buf.length === 0 ? chunk : Buffer.concat([this.buf, chunk]);
    for (;;) {
      const headerEnd = this.buf.indexOf("\r\n\r\n");
      if (headerEnd < 0) return;
      const header = this.buf.subarray(0, headerEnd).toString("ascii");
      const match = /Content-Length: *(\d+)/i.exec(header);
      if (!match)
        throw new RpcError(`no Content-Length in header ${JSON.stringify(header)}`);
      const len = Number(match[1]);
      const start = headerEnd + 4;
      if (this.buf.length < start + len) return;
      const body = this.buf.subarray(start, start + len).toString("utf8");
      this.buf = this.buf.subarray(start + len);
      this.dispatch(JSON.parse(body));
    }
  }

  private dispatch(msg: any): void {
    if (msg.method !== undefined && msg.id !== undefined) {
      // Server-to-client request, i.e. an FS callback.
      const result = this.onCallback ? this.onCallback(msg.method, msg.params) : null;
      this.write({ jsonrpc: "2.0", id: msg.id, result: result ?? null });
      return;
    }
    const entry = this.pending.get(msg.id);
    if (!entry) return;
    this.pending.delete(msg.id);
    if (msg.error)
      entry.reject(new RpcError(`${msg.error.message} (code ${msg.error.code})`));
    else entry.resolve(msg.result);
  }

  private write(msg: unknown): void {
    const body = Buffer.from(JSON.stringify(msg), "utf8");
    const head = Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, "ascii");
    this.stats.bytesSent += head.length + body.length;
    this.child.stdin!.write(Buffer.concat([head, body]));
  }

  call(method: string, params: Json = null): Promise<Json> {
    const id = this.nextId++;
    this.stats.calls++;
    return new Promise<Json>((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.write({ jsonrpc: "2.0", id, method, params });
    });
  }

  async close(): Promise<void> {
    this.child.stdin!.end();
    await new Promise<void>((resolve) => this.child.once("exit", () => resolve()));
  }
}

const MSG_REQUEST = 1;
const MSG_CALL_RESPONSE = 2;
const MSG_CALL_ERROR = 3;
const MSG_RESPONSE = 4;
const MSG_ERROR = 5;
const MSG_CALL = 6;

/**
 * The default `tsc --api` transport: a msgpack 3-tuple `[type, method, payload]`
 * whose payload is a JSON document. The method string doubles as the
 * correlation id and the server answers in order, so only one request per
 * method may be in flight.
 */
export class MsgpackClient implements Rpc {
  readonly transport = "msgpack";
  readonly stats: RpcStats = { calls: 0, bytesSent: 0, bytesReceived: 0 };

  private child: ChildProcess;
  private buf: Buffer<ArrayBufferLike> = Buffer.alloc(0);
  private queue: {
    method: string;
    resolve: (v: Json) => void;
    reject: (e: Error) => void;
  }[] = [];
  private onCallback: SpawnOptions["onCallback"];

  constructor(opts: SpawnOptions) {
    this.onCallback = opts.onCallback;
    this.child = spawnServer(opts, []);
    this.child.stdout!.on("data", (chunk: Buffer) => this.onData(chunk));
  }

  private onData(chunk: Buffer): void {
    this.stats.bytesReceived += chunk.length;
    this.buf = this.buf.length === 0 ? chunk : Buffer.concat([this.buf, chunk]);
    for (;;) {
      const frame = this.readFrame();
      if (!frame) return;
      this.dispatch(frame.type, frame.method, frame.payload);
    }
  }

  /** Decodes one tuple, or returns undefined while the frame is still incomplete. */
  private readFrame(): { type: number; method: string; payload: Buffer } | undefined {
    const b = this.buf;
    if (b.length < 2) return undefined;
    if (b[0] !== 0x93) {
      throw new RpcError(
        `expected msgpack fixarray-3 (0x93), got 0x${b[0]!.toString(16)}`
      );
    }
    let at = 1;
    const typeByte = b[at]!;
    let type: number;
    if (typeByte <= 0x7f) {
      type = typeByte;
      at += 1;
    } else if (typeByte === 0xcc) {
      if (b.length < at + 2) return undefined;
      type = b[at + 1]!;
      at += 2;
    } else {
      throw new RpcError(
        `expected fixint or uint8 message type, got 0x${typeByte.toString(16)}`
      );
    }
    const method = readBin(b, at);
    if (!method) return undefined;
    const payload = readBin(b, method.end);
    if (!payload) return undefined;
    this.buf = b.subarray(payload.end);
    return { type, method: method.data.toString("utf8"), payload: payload.data };
  }

  private dispatch(type: number, method: string, payload: Buffer): void {
    if (type === MSG_CALL) {
      const params = payload.length ? JSON.parse(payload.toString("utf8")) : null;
      const result = this.onCallback ? this.onCallback(method, params) : null;
      this.writeFrame(
        MSG_CALL_RESPONSE,
        method,
        Buffer.from(JSON.stringify(result ?? null), "utf8")
      );
      return;
    }
    const entry = this.queue.shift();
    if (!entry) throw new RpcError(`unsolicited response for ${method}`);
    if (type === MSG_ERROR || type === MSG_CALL_ERROR) {
      entry.reject(new RpcError(payload.toString("utf8")));
      return;
    }
    if (type !== MSG_RESPONSE && type !== MSG_CALL_RESPONSE) {
      entry.reject(new RpcError(`unexpected message type ${type}`));
      return;
    }
    // getSourceFile and echo answer with raw bytes rather than JSON.
    entry.resolve(payload.length === 0 ? null : tryParseJson(payload));
  }

  private writeFrame(type: number, method: string, payload: Buffer): void {
    const m = Buffer.from(method, "utf8");
    const frame = Buffer.concat([
      Buffer.from([0x93, type]),
      writeBin(m),
      writeBin(payload),
    ]);
    this.stats.bytesSent += frame.length;
    this.child.stdin!.write(frame);
  }

  call(method: string, params: Json = null): Promise<Json> {
    this.stats.calls++;
    return new Promise<Json>((resolve, reject) => {
      this.queue.push({ method, resolve, reject });
      this.writeFrame(
        MSG_REQUEST,
        method,
        Buffer.from(JSON.stringify(params ?? null), "utf8")
      );
    });
  }

  async close(): Promise<void> {
    this.child.stdin!.end();
    await new Promise<void>((resolve) => this.child.once("exit", () => resolve()));
  }
}

function readBin(b: Buffer, at: number): { data: Buffer; end: number } | undefined {
  if (b.length < at + 1) return undefined;
  const marker = b[at]!;
  let size: number;
  let start: number;
  if (marker === 0xc4) {
    if (b.length < at + 2) return undefined;
    size = b[at + 1]!;
    start = at + 2;
  } else if (marker === 0xc5) {
    if (b.length < at + 3) return undefined;
    size = b.readUInt16BE(at + 1);
    start = at + 3;
  } else if (marker === 0xc6) {
    if (b.length < at + 5) return undefined;
    size = b.readUInt32BE(at + 1);
    start = at + 5;
  } else {
    throw new RpcError(`expected msgpack bin8/16/32, got 0x${marker.toString(16)}`);
  }
  if (b.length < start + size) return undefined;
  return { data: b.subarray(start, start + size), end: start + size };
}

function writeBin(data: Buffer): Buffer {
  if (data.length < 256) return Buffer.concat([Buffer.from([0xc4, data.length]), data]);
  if (data.length < 1 << 16) {
    const head = Buffer.alloc(3);
    head[0] = 0xc5;
    head.writeUInt16BE(data.length, 1);
    return Buffer.concat([head, data]);
  }
  const head = Buffer.alloc(5);
  head[0] = 0xc6;
  head.writeUInt32BE(data.length, 1);
  return Buffer.concat([head, data]);
}

function tryParseJson(payload: Buffer): Json {
  const text = payload.toString("utf8");
  try {
    return JSON.parse(text);
  } catch {
    return { rawBytes: payload.length };
  }
}
