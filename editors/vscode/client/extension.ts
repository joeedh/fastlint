// The extension entry point (task 9.1). It starts the language server that
// does the linting and stops it on deactivate; the settings, commands and the
// status bar item land in task 9.2.

import path from "node:path";
import * as vscode from "vscode";
import {
  LanguageClient,
  TransportKind,
  type LanguageClientOptions,
  type ServerOptions,
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;

export async function activate(context: vscode.ExtensionContext): Promise<void> {
  const serverModule = context.asAbsolutePath(path.join("out", "server.js"));
  const serverOptions: ServerOptions = {
    run  : { module: serverModule, transport: TransportKind.ipc },
    debug: {
      module   : serverModule,
      transport: TransportKind.ipc,
      options  : { execArgv: ["--nolazy", "--inspect=6011"] },
    },
  };
  const clientOptions: LanguageClientOptions = {
    documentSelector: [
      { scheme: "file", language: "javascript" },
      { scheme: "file", language: "javascriptreact" },
      { scheme: "file", language: "typescript" },
      { scheme: "file", language: "typescriptreact" },
    ],
    outputChannelName: "lintrix",
    synchronize: {
      // A config or tsconfig change can alter what any open document lints
      // with; the server drops its config cache and re-pulls on either.
      fileEvents: [
        vscode.workspace.createFileSystemWatcher(
          "**/lintrix.config.{ts,mts,js,mjs,json}"
        ),
        vscode.workspace.createFileSystemWatcher("**/tsconfig.json"),
      ],
    },
  };

  client = new LanguageClient("lintrix", "lintrix", serverOptions, clientOptions);
  context.subscriptions.push(client);
  await client.start();
}

export async function deactivate(): Promise<void> {
  await client?.stop();
  client = undefined;
}
