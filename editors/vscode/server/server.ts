// The language server (tasks 9.2, 9.3). It lints the open documents on the
// client's pull requests, maps each report to diagnostics, reports a status
// per document for the client's status bar, and drops its caches when a
// watched config, a tsconfig or a setting changes. Code actions land in task
// 9.4. The plugin surface is imported by path, so the config loader and the
// engines have one home (docs/embedding.md).

import path from "node:path";
import {
  DidChangeConfigurationNotification,
  DocumentDiagnosticReportKind,
  ProposedFeatures,
  TextDocumentSyncKind,
  TextDocuments,
  createConnection,
  type DocumentDiagnosticReport,
  type InitializeResult,
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";
import { URI } from "vscode-uri";

import {
  revalidateNotification,
  statusNotification,
  type StatusParams,
} from "../shared/protocol.ts";
import { ConfigCache } from "./configs.ts";
import { runDiagnostic, toState, type DocumentState } from "./diagnostics.ts";
import { Engine } from "./engine.ts";
import { SettingsCache } from "./settings.ts";

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);
const configs = new ConfigCache();
const settings = new SettingsCache(connection);
const states = new Map<string, DocumentState>();

/** Resolves once the bundled engine is loaded, or to the reason it was not. */
let engine: Promise<Engine | string>;

/** The extension a document without a path lints as, from its language id. */
const extensionFor: Readonly<Record<string, string>> = {
  javascript     : ".js",
  javascriptreact: ".jsx",
  typescript     : ".ts",
  typescriptreact: ".tsx",
};

/**
 * The path a document is linted under. A `file:` document is its own path,
 * which anchors the config search and the override globs. Anything else (an
 * untitled buffer, a virtual document) has no directory, so it gets a name
 * with the right extension and lints under the recommended preset.
 */
function filePathFor(document: TextDocument): { filePath: string; onDisk: boolean } {
  const uri = URI.parse(document.uri);
  if (uri.scheme === "file") return { filePath: uri.fsPath, onDisk: true };
  const extension = extensionFor[document.languageId] ?? ".ts";
  const base = path.basename(uri.path) || "untitled";
  return {
    filePath: base.endsWith(extension) ? base : `${base}${extension}`,
    onDisk  : false,
  };
}

function status(params: StatusParams): void {
  if (params.message !== undefined && params.state !== "ok") {
    const log =
      params.state === "error" ? connection.console.error : connection.console.warn;
    log.call(connection.console, params.message);
  }
  void connection.sendNotification(statusNotification, params);
}

function emptyState(document: TextDocument, problem: string): DocumentState {
  return {
    version    : document.version,
    report: {
      filePath           : document.uri,
      messages           : [],
      errorCount         : 0,
      warningCount       : 0,
      fixableErrorCount  : 0,
      fixableWarningCount: 0,
    },
    diagnostics: [runDiagnostic(problem)],
  };
}

/** Lints `document` and reports its status. A problem with the run itself (an
 * engine or config that did not load) is both the status and one diagnostic at
 * the top of the file, so it shows in the Problems view as well. */
async function lintDocument(document: TextDocument): Promise<DocumentState> {
  const uri = document.uri;
  const loaded = await engine;
  if (typeof loaded === "string") {
    status({ uri, state: "error", message: loaded });
    return emptyState(document, loaded);
  }
  const current = await settings.forDocument(uri);
  if (current.engine === "native") {
    const message = "lintrix.engine is 'native', which this version does not offer yet";
    status({ uri, state: "error", message });
    return emptyState(document, message);
  }

  const { filePath, onDisk } = filePathFor(document);
  const lookup = onDisk ? await configs.forFile(filePath) : {};
  if (lookup.error !== undefined) {
    const message = `${lookup.configPath} did not load: ${lookup.error}`;
    status({ uri, state: "error", engine: "wasm", message });
    return emptyState(document, message);
  }
  const report = loaded.lint(document.getText(), filePath, lookup.compiled);
  status({ uri, state: "ok", engine: "wasm" });
  return toState(document, report);
}

connection.onInitialize((): InitializeResult => {
  return {
    capabilities: {
      textDocumentSync  : TextDocumentSyncKind.Incremental,
      diagnosticProvider: {
        identifier           : "lintrix",
        interFileDependencies: false,
        workspaceDiagnostics : false,
      },
    },
  };
});

connection.onInitialized(() => {
  void connection.client.register(DidChangeConfigurationNotification.type);
  engine = Engine.load().then(
    (loaded) => {
      if (loaded === undefined) {
        const message = "no engine is bundled with the extension; nothing will be linted";
        status({ state: "error", message });
        return message;
      }
      connection.console.info(`WASM engine: ${loaded.modulePath}`);
      status({ state: "ok", engine: "wasm" });
      return loaded;
    },
    (error: unknown) => {
      const reason = error instanceof Error ? error.message : String(error);
      const message = `the WASM engine failed to load: ${reason}`;
      status({ state: "error", message });
      return message;
    }
  );
});

connection.languages.diagnostics.on(async (params): Promise<DocumentDiagnosticReport> => {
  const document = documents.get(params.textDocument.uri);
  if (document === undefined) {
    return { kind: DocumentDiagnosticReportKind.Full, items: [] };
  }
  if (!(await settings.forDocument(document.uri)).enable) {
    states.delete(document.uri);
    return { kind: DocumentDiagnosticReportKind.Full, items: [] };
  }
  try {
    const state = await lintDocument(document);
    states.set(document.uri, state);
    return { kind: DocumentDiagnosticReportKind.Full, items: state.diagnostics };
  } catch (error) {
    const message = `lintrix failed: ${error instanceof Error ? error.message : String(error)}`;
    status({ uri: document.uri, state: "error", message });
    states.delete(document.uri);
    return { kind: DocumentDiagnosticReportKind.Full, items: [runDiagnostic(message)] };
  }
});

function refresh(): void {
  connection.languages.diagnostics.refresh().catch((error: unknown) => {
    connection.console.error(`diagnostics refresh failed: ${String(error)}`);
  });
}

// The client watches lintrix.config.* and tsconfig.json. A change to either can
// alter what any open document lints with, so everything is re-resolved.
connection.onDidChangeWatchedFiles(() => {
  configs.clear();
  refresh();
});

connection.onDidChangeConfiguration(() => {
  settings.clear();
  refresh();
});

connection.onNotification(revalidateNotification, () => {
  configs.clear();
  settings.clear();
  refresh();
});

documents.onDidClose((event) => {
  states.delete(event.document.uri);
  settings.forget(event.document.uri);
});

documents.listen(connection);
connection.listen();
