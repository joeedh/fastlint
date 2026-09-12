// The extension entry point (tasks 9.1, 9.2). It starts the language server
// that does the linting, decides when the server is asked to lint (the `run`
// and `validate` settings act here, on the pull side), registers the commands
// and keeps the status bar item, and stops the server on deactivate.

import path from "node:path";
import * as vscode from "vscode";
import {
  DiagnosticPullMode,
  ExecuteCommandRequest,
  LanguageClient,
  TransportKind,
  type LanguageClientOptions,
  type ServerOptions,
} from "vscode-languageclient/node";

import {
  defaultSettings,
  revalidateNotification,
  statusNotification,
  type Settings,
  type StatusParams,
} from "../shared/protocol.ts";
import { StatusBar } from "./status.ts";

const commands = {
  executeAutofix   : "lintrix.executeAutofix",
  restart          : "lintrix.restart",
  revalidate       : "lintrix.revalidate",
  showOutputChannel: "lintrix.showOutputChannel",
  /** Run by the "Show documentation" code action; not in the palette. */
  openRuleDoc      : "lintrix.openRuleDoc",
} as const;

/** The server command fix-all is routed to. */
const applyAllFixes = "lintrix.applyAllFixes";

let client: LanguageClient | undefined;

function settingsFor(document: vscode.TextDocument): Settings {
  const config = vscode.workspace.getConfiguration("lintrix", document);
  return {
    enable           : config.get("enable", defaultSettings.enable),
    run              : config.get("run", defaultSettings.run),
    validate         : config.get("validate", defaultSettings.validate),
    engine           : config.get("engine", defaultSettings.engine),
    binaryPath       : config.get("binaryPath", defaultSettings.binaryPath),
    codeActionsOnSave: {
      mode: config.get("codeActionsOnSave.mode", defaultSettings.codeActionsOnSave.mode),
    },
  };
}

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
      { scheme: "untitled", language: "javascript" },
      { scheme: "untitled", language: "javascriptreact" },
      { scheme: "untitled", language: "typescript" },
      { scheme: "untitled", language: "typescriptreact" },
    ],
    outputChannelName    : "lintrix",
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
    diagnosticPullOptions: {
      onChange: true,
      onSave  : true,
      onFocus : true,
      // True excludes the document from this pull. `run` picks which of the
      // typing and saving pulls goes through; `validate` and `enable` gate both.
      filter: (document, mode) => {
        const settings = settingsFor(document);
        if (mode === DiagnosticPullMode.onType && settings.run !== "onType") return true;
        if (mode === DiagnosticPullMode.onSave && settings.run !== "onSave") return true;
        return !settings.enable || !settings.validate.includes(document.languageId);
      },
      onTabs  : false,
    },
  };

  client = new LanguageClient("lintrix", "lintrix", serverOptions, clientOptions);
  const statusBar = new StatusBar(commands.showOutputChannel);
  context.subscriptions.push(
    client,
    statusBar,
    client.onNotification(statusNotification, (params: StatusParams) => {
      statusBar.update(params);
    }),
    vscode.commands.registerCommand(commands.showOutputChannel, () => {
      client?.outputChannel.show();
    }),
    vscode.commands.registerCommand(commands.restart, async () => {
      statusBar.reset();
      await client?.restart();
    }),
    vscode.commands.registerCommand(commands.revalidate, () => {
      void client?.sendNotification(revalidateNotification);
    }),
    vscode.commands.registerCommand(commands.openRuleDoc, (url: string) => {
      void vscode.env.openExternal(vscode.Uri.parse(url));
    }),
    vscode.commands.registerCommand(commands.executeAutofix, async () => {
      const editor = vscode.window.activeTextEditor;
      if (editor === undefined || client === undefined) return;
      const { uri, version } = editor.document;
      try {
        await client.sendRequest(ExecuteCommandRequest.type, {
          command  : applyAllFixes,
          arguments: [{ uri: uri.toString(), version }],
        });
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        void vscode.window.showErrorMessage(`lintrix: fix all failed: ${message}`);
      }
    })
  );
  await client.start();
}

export async function deactivate(): Promise<void> {
  await client?.stop();
  client = undefined;
}
