// The client of `lintrix serve` (task 9.5): one resident native binary, driven
// over its stdio with JSON-RPC 2.0 in LSP's Content-Length framing, so a lint
// of an open buffer costs a message rather than a process and tsgo startup.

import { spawn, type ChildProcess } from "node:child_process";

import type { FileReport } from "./engine.ts";

/** What `lint` answers: the `--format json` array for the one file (empty when
 * the config ignores it), whether type-aware rules ran, and why not. */
export interface ServeLintResult {
  results: FileReport[];
  typed: boolean;
  typeError?: string;
}

export interface ServeOptions {
  /** Where `tsc` is resolved from when a project has none beside it. */
  cwd?: string;
  cacheDir?: string;
  noCache?: boolean;
  /** Receives the binary's stderr, line by line. */
  onStderr?: (line: string) => void;
}

interface Pending {
  resolve: (value: unknown) => void;
  reject: (error: Error) => void;
}

interface Response {
  id?: number;
  result?: unknown;
  error?: { code: number; message: string };
}

/** Where a header ends and how long the body after it is; undefined until the
 * whole header has arrived. */
function frameOf(buffer: Buffer): { bodyStart: number; length: number } | undefined {
  const headerEnd = buffer.indexOf("\r\n\r\n");
  if (headerEnd < 0) return undefined;
  const header = buffer.subarray(0, headerEnd).toString("latin1");
  const match = /Content-Length:\s*(\d+)/i.exec(header);
  if (match === null) throw new Error(`lintrix serve: a message without Content-Length`);
  return { bodyStart: headerEnd + 4, length: Number(match[1]) };
}

export class ServeClient {
  private readonly child: ChildProcess;
  private readonly pending = new Map<number, Pending>();
  private inbox: Buffer = Buffer.alloc(0);
  private nextId = 1;
  private exitReason: Error | undefined;
  /** The binary's path, for the log. */
  readonly binary: string;
  /** Resolves with the exit code once the binary has exited. */
  readonly exited: Promise<number | null>;

  constructor(binary: string, options: ServeOptions = {}) {
    this.binary = binary;
    const args = ["serve"];
    if (options.noCache) args.push("--no-cache");
    if (options.cacheDir !== undefined) args.push("--cache-dir", options.cacheDir);
    this.child = spawn(binary, args, {
      cwd  : options.cwd,
      stdio: ["pipe", "pipe", "pipe"],
    });
    this.child.stdout!.on("data", (chunk: Buffer) => this.receive(chunk));
    let rest = "";
    this.child.stderr!.on("data", (chunk: Buffer) => {
      rest += chunk.toString("utf8");
      const lines = rest.split(/\r?\n/);
      rest = lines.pop() ?? "";
      for (const line of lines) if (line.length > 0) options.onStderr?.(line);
    });
    this.exited = new Promise((resolve) => {
      this.child.on("error", (error) => this.fail(error));
      this.child.on("exit", (code, signal) => {
        this.fail(new Error(`lintrix serve exited (${signal ?? code})`));
        resolve(code);
      });
    });
    this.child.stdin!.on("error", (error: Error) => this.fail(error));
  }

  /** True until the binary has exited or its pipes failed. */
  get alive(): boolean {
    return this.exitReason === undefined;
  }

  lint(file: string, text?: string, config?: string): Promise<ServeLintResult> {
    return this.request("lint", { file, text, config }) as Promise<ServeLintResult>;
  }

  /** Drops the overlay `lint` kept for `file`. */
  close(file: string): Promise<void> {
    return this.request("close", { file }).then(() => undefined);
  }

  /** Reports files changed on disk. A config or tsconfig among them reloads. */
  changed(files: readonly string[]): Promise<void> {
    if (files.length === 0) return Promise.resolve();
    return this.request("changed", { files }).then(() => undefined);
  }

  /** Reloads the config or tsconfig at `path`, or everything when omitted. */
  configChanged(path?: string): Promise<void> {
    return this.request("configChanged", { path }).then(() => undefined);
  }

  /** Asks the binary to exit and waits for it. */
  async shutdown(): Promise<void> {
    if (!this.alive) return;
    try {
      await this.request("shutdown", null);
    } catch {
      // The binary may exit before the response is read.
    }
    this.child.stdin?.end();
    await this.exited;
  }

  /** Ends the binary without waiting. */
  dispose(): void {
    if (this.alive) this.child.kill();
    this.fail(new Error("lintrix serve was disposed"));
  }

  request(method: string, params: unknown): Promise<unknown> {
    if (this.exitReason !== undefined) return Promise.reject(this.exitReason);
    const id = this.nextId++;
    const body = JSON.stringify({ jsonrpc: "2.0", id, method, params });
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.child.stdin!.write(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
    });
  }

  private receive(chunk: Buffer): void {
    this.inbox = this.inbox.length === 0 ? chunk : Buffer.concat([this.inbox, chunk]);
    for (;;) {
      let frame: { bodyStart: number; length: number } | undefined;
      try {
        frame = frameOf(this.inbox);
      } catch (error) {
        this.fail(error instanceof Error ? error : new Error(String(error)));
        this.child.kill();
        return;
      }
      if (frame === undefined || this.inbox.length < frame.bodyStart + frame.length) return;
      const body = this.inbox.subarray(frame.bodyStart, frame.bodyStart + frame.length);
      this.inbox = this.inbox.subarray(frame.bodyStart + frame.length);
      const message = JSON.parse(body.toString("utf8")) as Response;
      if (message.id === undefined) continue;
      const waiting = this.pending.get(message.id);
      this.pending.delete(message.id);
      if (waiting === undefined) continue;
      if (message.error !== undefined) {
        waiting.reject(new Error(`lintrix serve: ${message.error.message}`));
      } else {
        waiting.resolve(message.result);
      }
    }
  }

  /** Rejects everything in flight and everything to come. */
  private fail(reason: Error): void {
    if (this.exitReason !== undefined) return;
    this.exitReason = reason;
    for (const waiting of this.pending.values()) waiting.reject(reason);
    this.pending.clear();
  }
}
