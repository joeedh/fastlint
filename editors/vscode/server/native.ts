// The resident native binaries the server lints through (task 9.5): one
// `lintrix serve` per binary path, started on first use and again after it
// exits, handed each config as the handoff document the CLI writes.

import fs from "node:fs";

import type { CompiledConfig } from "../../../source/fastlint/plugin/ts/compile.ts";
import {
  nativeConfig,
  nativeConfigPath,
} from "../../../source/fastlint/plugin/ts/compile.ts";
import { ServeClient } from "../../../source/fastlint/plugin/ts/serve.ts";

/** What one lint sends to a native binary. */
export interface NativeRun {
  client: ServeClient;
  /** The handoff config's path, or undefined for a document with no config. */
  handoff?: string;
}

export class NativeEngines {
  private readonly clients = new Map<string, ServeClient>();
  private readonly handoffs = new WeakMap<CompiledConfig, string>();
  private readonly log: (line: string) => void;

  constructor(log: (line: string) => void) {
    this.log = log;
  }

  /** The client for `binary` typing with `tsgoPath` (or the `tsc` it finds),
   * spawned on first use and after an exit. `cwd` is where the first spawn
   * resolves `tsc` from when a project has none. */
  clientFor(binary: string, cwd: string, tsgoPath?: string): ServeClient {
    const key = `${binary}\0${tsgoPath ?? ""}`;
    let client = this.clients.get(key);
    if (client === undefined || !client.alive) {
      const options = { cwd, onStderr: this.log };
      client = new ServeClient(
        binary,
        tsgoPath === undefined ? options : { ...options, tsgoPath }
      );
      this.log(
        `lintrix serve: ${binary}${tsgoPath === undefined ? "" : ` (tsc: ${tsgoPath})`}`
      );
      void client.exited.then((code) => {
        if (code !== 0) this.log(`lintrix serve exited with ${code}`);
      });
      this.clients.set(key, client);
    }
    return client;
  }

  /**
   * The handoff path for `compiled`, written beside `configPath` on first sight
   * (docs/embedding.md). A config loaded again is a new object, so the file is
   * rewritten and every running binary reloads it.
   */
  async handoffFor(configPath: string, compiled: CompiledConfig): Promise<string> {
    let handoff = this.handoffs.get(compiled);
    if (handoff === undefined) {
      handoff = nativeConfigPath(configPath);
      fs.writeFileSync(
        handoff,
        `${JSON.stringify(nativeConfig(compiled), undefined, 2)}\n`
      );
      this.handoffs.set(compiled, handoff);
      await this.each((client) => client.configChanged(handoff));
    }
    return handoff;
  }

  /** Drops the overlay every binary holds for `file`. */
  close(file: string): Promise<void> {
    return this.each((client) => client.close(file));
  }

  changed(files: readonly string[]): Promise<void> {
    return this.each((client) => client.changed(files));
  }

  /** Every binary reloads every config and tsconfig. */
  configChanged(): Promise<void> {
    return this.each((client) => client.configChanged());
  }

  dispose(): void {
    for (const client of this.clients.values()) client.dispose();
    this.clients.clear();
  }

  private async each(action: (client: ServeClient) => Promise<void>): Promise<void> {
    for (const client of this.clients.values()) {
      if (!client.alive) continue;
      try {
        await action(client);
      } catch (error) {
        this.log(
          `lintrix serve: ${error instanceof Error ? error.message : String(error)}`
        );
      }
    }
  }
}
