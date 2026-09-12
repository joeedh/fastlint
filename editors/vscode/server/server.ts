// The language server (task 9.3). It lints the open documents on the client's
// pull requests, maps each report to diagnostics, and drops its config cache
// when a watched config or tsconfig changes. Code actions land in task 9.4.
// The plugin surface is imported by path, so the config loader and the engines
// have one home (docs/embedding.md).

import path from "node:path";
import {
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

import { ConfigCache } from "./configs.ts";
import { runDiagnostic, toState, type DocumentState } from "./diagnostics.ts";
import { Engine } from "./engine.ts";

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);
const configs = new ConfigCache();
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

async function lintDocument(document: TextDocument): Promise<DocumentState> {
  const loaded = await engine;
  if (typeof loaded === "string") {
    return {
      version: document.version,
      report: emptyReport(document.uri),
      diagnostics: [runDiagnostic(loaded)],
    };
  }
  const { filePath, onDisk } = filePathFor(document);
  const lookup = onDisk ? await configs.forFile(filePath) : {};
  if (lookup.error !== undefined) {
    return {
      version    : document.version,
      report     : emptyReport(filePath),
      diagnostics: [runDiagnostic(`${lookup.configPath} did not load: ${lookup.error}`)],
    };
  }
  const report = loaded.lint(document.getText(), filePath, lookup.compiled);
  return toState(document, report);
}

function emptyReport(filePath: string): DocumentState["report"] {
  return {
    filePath,
    messages           : [],
    errorCount         : 0,
    warningCount       : 0,
    fixableErrorCount  : 0,
    fixableWarningCount: 0,
  };
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
  engine = Engine.load().then(
    (loaded) => {
      if (loaded === undefined) {
        connection.console.error("no WASM engine bundled; nothing will be linted");
        return "lintrix: no engine bundled with the extension";
      }
      connection.console.info(`WASM engine: ${loaded.modulePath}`);
      return loaded;
    },
    (error: unknown) => {
      const message = error instanceof Error ? error.message : String(error);
      connection.console.error(`the WASM engine failed to load: ${message}`);
      return `lintrix: the engine failed to load: ${message}`;
    }
  );
});

connection.languages.diagnostics.on(async (params): Promise<DocumentDiagnosticReport> => {
  const document = documents.get(params.textDocument.uri);
  if (document === undefined) {
    return { kind: DocumentDiagnosticReportKind.Full, items: [] };
  }
  try {
    const state = await lintDocument(document);
    states.set(document.uri, state);
    return { kind: DocumentDiagnosticReportKind.Full, items: state.diagnostics };
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    connection.console.error(`${document.uri}: ${message}`);
    states.delete(document.uri);
    return {
      kind : DocumentDiagnosticReportKind.Full,
      items: [runDiagnostic(`lintrix failed: ${message}`)],
    };
  }
});

// The client watches lintrix.config.* and tsconfig.json. A change to either can
// alter what any open document lints with, so everything is re-resolved.
connection.onDidChangeWatchedFiles(() => {
  configs.clear();
  connection.languages.diagnostics.refresh().catch((error: unknown) => {
    connection.console.error(`diagnostics refresh failed: ${String(error)}`);
  });
});

documents.onDidClose((event) => {
  states.delete(event.document.uri);
});

documents.listen(connection);
connection.listen();
