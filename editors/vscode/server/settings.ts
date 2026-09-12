// The settings a document is linted under (task 9.2), fetched from the client
// per document scope and cached until the client reports a change.

import type { Connection } from "vscode-languageserver/node";

import { defaultSettings, type Settings } from "../shared/protocol.ts";

export class SettingsCache {
  private readonly byUri = new Map<string, Promise<Settings>>();
  private readonly connection: Connection;

  constructor(connection: Connection) {
    this.connection = connection;
  }

  forDocument(uri: string): Promise<Settings> {
    let settings = this.byUri.get(uri);
    if (settings === undefined) {
      settings = this.fetch(uri);
      this.byUri.set(uri, settings);
    }
    return settings;
  }

  forget(uri: string): void {
    this.byUri.delete(uri);
  }

  clear(): void {
    this.byUri.clear();
  }

  /** The client's `lintrix` section for `uri`, over the defaults so a setting
   * the client omits still has a value. */
  private async fetch(uri: string): Promise<Settings> {
    const fetched = (await this.connection.workspace.getConfiguration({
      scopeUri: uri,
      section : "lintrix",
    })) as Partial<Settings> | null;
    return {
      ...defaultSettings,
      ...fetched,
      codeActionsOnSave: {
        ...defaultSettings.codeActionsOnSave,
        ...fetched?.codeActionsOnSave,
      },
    };
  }
}
