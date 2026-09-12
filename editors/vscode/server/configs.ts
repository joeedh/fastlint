// The configs the open documents lint with (task 9.3). A document's config is
// the nearest `lintrix.config.*` walking up from its directory, loaded once and
// kept until a watched config or tsconfig changes, when everything is dropped
// and the open documents are linted again.

import path from "node:path";

import type { CompiledConfig } from "../../../source/fastlint/plugin/ts/compile.ts";
import {
  findConfig,
  loadCompiledConfig,
} from "../../../source/fastlint/plugin/ts/config.ts";

/** What a file resolved to. A file above every config has no `configPath`. */
export interface ConfigLookup {
  configPath?: string;
  compiled?: CompiledConfig;
  /** Why `compiled` is missing when `configPath` is not. */
  error?: string;
}

export class ConfigCache {
  private readonly byDir = new Map<string, string | undefined>();
  private readonly loads = new Map<string, Promise<ConfigLookup>>();

  /** The config `filePath` lints with. Directory walks and loads are cached. */
  async forFile(filePath: string): Promise<ConfigLookup> {
    const dir = path.dirname(filePath);
    let configPath = this.byDir.get(dir);
    if (!this.byDir.has(dir)) {
      configPath = findConfig(dir);
      this.byDir.set(dir, configPath);
    }
    if (configPath === undefined) return {};

    let load = this.loads.get(configPath);
    if (load === undefined) {
      load = this.load(configPath);
      this.loads.set(configPath, load);
    }
    return load;
  }

  /** Forgets every lookup and every loaded config. */
  clear(): void {
    this.byDir.clear();
    this.loads.clear();
  }

  private async load(configPath: string): Promise<ConfigLookup> {
    try {
      // `fresh`, because this process outlives edits to a module config and
      // Node's import cache would hand back the first version forever.
      const compiled = await loadCompiledConfig(configPath, { fresh: true });
      return { configPath, compiled };
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      return { configPath, error: message };
    }
  }
}
