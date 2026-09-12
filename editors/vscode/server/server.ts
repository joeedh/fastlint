// The language server (task 9.1). This is the skeleton: it answers initialize,
// tracks open documents and reports which engine it found. Diagnostics arrive
// in task 9.3 and code actions in 9.4. The plugin surface is imported by path,
// so the config loader and the engines have one home (docs/embedding.md).

import {
  ProposedFeatures,
  TextDocumentSyncKind,
  TextDocuments,
  createConnection,
  type InitializeResult,
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";

import { configNames } from "../../../source/fastlint/plugin/ts/config.ts";
import { findWasmModule } from "../../../source/fastlint/plugin/ts/engine.ts";

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);

connection.onInitialize((): InitializeResult => {
  return {
    capabilities: {
      textDocumentSync: TextDocumentSyncKind.Incremental,
    },
  };
});

connection.onInitialized(() => {
  const wasm = findWasmModule();
  connection.console.info(
    wasm === undefined
      ? "no WASM engine bundled; nothing will be linted"
      : `WASM engine: ${wasm}`
  );
  connection.console.info(`config files: ${configNames.join(", ")}`);
});

documents.listen(connection);
connection.listen();
