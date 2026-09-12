// The status bar item (task 9.2). It follows the active editor: the state the
// server last reported for that document, or the server-wide state when the
// document has none yet. Clicking it opens the output channel.

import * as vscode from "vscode";

import type { StatusParams } from "../shared/protocol.ts";

const icons = { ok: "check", warning: "warning", error: "error" } as const;

export class StatusBar {
  private readonly item: vscode.StatusBarItem;
  private readonly byUri = new Map<string, StatusParams>();
  private server: StatusParams = { state: "ok" };
  private readonly subscriptions: vscode.Disposable[] = [];

  constructor(showOutputCommand: string) {
    this.item = vscode.window.createStatusBarItem(
      "lintrix.status",
      vscode.StatusBarAlignment.Right,
      0
    );
    this.item.name = "lintrix";
    this.item.command = showOutputCommand;
    this.subscriptions.push(
      this.item,
      vscode.window.onDidChangeActiveTextEditor(() => this.render()),
      vscode.workspace.onDidCloseTextDocument((document) => {
        this.byUri.delete(document.uri.toString());
      })
    );
    this.render();
  }

  update(params: StatusParams): void {
    if (params.uri === undefined) this.server = params;
    else this.byUri.set(params.uri, params);
    this.render();
  }

  /** Forgets every reported state, for a server restart. */
  reset(): void {
    this.byUri.clear();
    this.server = { state: "ok" };
    this.render();
  }

  dispose(): void {
    for (const subscription of this.subscriptions) subscription.dispose();
  }

  /** Shown for a document the server reported on, and for every editor while
   * the server as a whole is failing; hidden for the rest. */
  private render(): void {
    const uri = vscode.window.activeTextEditor?.document.uri.toString();
    const forDocument = uri === undefined ? undefined : this.byUri.get(uri);
    const current = forDocument ?? (this.server.state === "ok" ? undefined : this.server);
    if (current === undefined) {
      this.item.hide();
      return;
    }
    const engine = current.engine !== undefined ? ` (${current.engine})` : "";
    this.item.text = `$(${icons[current.state]}) lintrix`;
    this.item.tooltip = current.message ?? `lintrix${engine}`;
    this.item.backgroundColor =
      current.state === "error"
        ? new vscode.ThemeColor("statusBarItem.errorBackground")
        : current.state === "warning"
          ? new vscode.ThemeColor("statusBarItem.warningBackground")
          : undefined;
    this.item.show();
  }
}
