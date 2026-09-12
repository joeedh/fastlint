// The language server (tasks 9.2 to 9.5). It lints the open documents on the
// client's pull requests, maps each report to diagnostics, reports a status
// per document for the client's status bar, and drops its caches when a
// watched config, a tsconfig or a setting changes, and serves the code actions
// and fix-all. A native `lintrix` on the machine lints through a resident
// `lintrix serve`; otherwise the bundled WASM module does. The plugin surface
// is imported by path, so the config loader and the engines have one home
// (docs/embedding.md).

import fs from "node:fs";
import path from "node:path";
import {
  CodeActionKind,
  DidChangeConfigurationNotification,
  DocumentDiagnosticReportKind,
  ProposedFeatures,
  TextDocumentSyncKind,
  TextDocuments,
  createConnection,
  type CodeAction,
  type DocumentDiagnosticReport,
  type InitializeResult,
  type TextEdit,
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";
import { URI } from "vscode-uri";

import type { CompiledConfig } from "../../../source/fastlint/plugin/ts/compile.ts";
import { onPath, resolveBinary } from "../../../source/fastlint/plugin/ts/engine.ts";
import {
  applyFixes,
  maxFixPasses,
  nonOverlapping,
} from "../../../source/fastlint/plugin/ts/fixes.ts";
import {
  revalidateNotification,
  statusNotification,
  type Settings,
  type StatusParams,
} from "../shared/protocol.ts";
import { applyAllFixesCommand, fixAllKind, fixEdit, quickFixes } from "./actions.ts";
import { ConfigCache } from "./configs.ts";
import { runDiagnostic, source, toState, type DocumentState } from "./diagnostics.ts";
import { editsBetween } from "./diff.ts";
import { Engine, type LintResult } from "./engine.ts";
import { NativeEngines, type NativeRun } from "./native.ts";
import { SettingsCache } from "./settings.ts";

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);
const configs = new ConfigCache();
const settings = new SettingsCache(connection);
const states = new Map<string, DocumentState>();
const natives = new NativeEngines((line) => connection.console.info(line));

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

/** Everything a lint of `document` needs, resolved once per request. */
interface Run {
  engine: Engine;
  filePath: string;
  compiled: CompiledConfig | undefined;
  settings: Settings;
  /** The resident binary to lint through; undefined lints through WASM. */
  native?: NativeRun;
  /** Why the run is on WASM although the settings allowed a native binary. */
  note?: string;
}

/** The native `lintrix` the settings and config point at, or undefined; a
 * `binaryPath` that does not exist is an error. */
function binaryFor(
  current: Settings,
  compiled: CompiledConfig | undefined
): string | undefined | Error {
  if (current.binaryPath !== null) {
    if (fs.existsSync(current.binaryPath)) return current.binaryPath;
    return new Error(`lintrix.binaryPath does not exist: ${current.binaryPath}`);
  }
  return compiled === undefined ? onPath("lintrix") : resolveBinary(compiled);
}

/** Resolves the engine, settings and config for `document`, or the reason it
 * cannot be linted. Reports the failure as the document's status. */
async function resolveRun(document: TextDocument): Promise<Run | string> {
  const uri = document.uri;
  const loaded = await engine;
  if (typeof loaded === "string") {
    status({ uri, state: "error", message: loaded });
    return loaded;
  }
  const current = await settings.forDocument(uri);
  const { filePath, onDisk } = filePathFor(document);
  const lookup = onDisk ? await configs.forFile(filePath) : {};
  if (lookup.error !== undefined) {
    const message = `${lookup.configPath} did not load: ${lookup.error}`;
    status({ uri, state: "error", message });
    return message;
  }
  const run: Run = {
    engine: loaded,
    filePath,
    compiled: lookup.compiled,
    settings: current,
  };
  if (current.engine === "wasm") return run;

  const binary = binaryFor(current, lookup.compiled);
  if (binary instanceof Error) {
    status({ uri, state: "error", message: binary.message });
    return binary.message;
  }
  if (binary === undefined) {
    if (current.engine === "native") {
      const message =
        "lintrix.engine is 'native' but no lintrix binary was found; set lintrix.binaryPath";
      status({ uri, state: "error", message });
      return message;
    }
    run.note =
      "type-aware rules are off: no native lintrix was found (set lintrix.binaryPath)";
    return run;
  }
  const cwd =
    lookup.compiled?.baseDir ?? (onDisk ? path.dirname(filePath) : process.cwd());
  const client = natives.clientFor(binary, cwd, current.tsgoPath ?? undefined);
  const handoff =
    lookup.compiled !== undefined && lookup.configPath !== undefined
      ? await natives.handoffFor(lookup.configPath, lookup.compiled)
      : undefined;
  run.native = handoff === undefined ? { client } : { client, handoff };
  return run;
}

/** The status a finished lint reports: the engine, and why the type-aware
 * rules did not run when they did not. */
function statusFor(uri: string, run: Run, result: LintResult): StatusParams {
  const engine = result.engine;
  if (engine === "wasm") {
    return run.note === undefined
      ? { uri, state: "ok", engine }
      : { uri, state: "ok", engine, message: run.note };
  }
  if (result.typeError === undefined) return { uri, state: "ok", engine };
  // A query that failed mid-file leaves the rest of the file's answers intact.
  if (result.typed) {
    return {
      uri,
      state: "warning",
      engine,
      message: `a type query failed: ${result.typeError}`,
    };
  }
  // A file no tsconfig claims is ordinary; a type server that failed is not.
  const state = result.typeError === "no tsconfig resolved" ? "ok" : "warning";
  return { uri, state, engine, message: `type-aware rules are off: ${result.typeError}` };
}

/** Lints `document` and reports its status. A problem with the run itself (an
 * engine or config that did not load) is both the status and one diagnostic at
 * the top of the file, so it shows in the Problems view as well. */
async function lintDocument(document: TextDocument): Promise<DocumentState> {
  const run = await resolveRun(document);
  if (typeof run === "string") return emptyState(document, run);
  const result = await run.engine.lint(
    document.getText(),
    run.filePath,
    run.compiled,
    run.native
  );
  status(statusFor(document.uri, run, result));
  return toState(document, result.report);
}

/**
 * The edits that fix everything fixable in `document`. In `problems` mode the
 * fixes already shown are applied in one pass, which is what an on-save run
 * asks for when it must not lint again. Otherwise the text is linted and
 * fixed until a pass finds nothing fixable, and the result is diffed back to
 * edits on the document. Empty when nothing is fixable or the state is stale.
 */
async function computeAllFixes(
  document: TextDocument,
  mode: Settings["codeActionsOnSave"]["mode"]
): Promise<TextEdit[]> {
  if (mode === "problems") {
    const state = states.get(document.uri);
    if (state === undefined || state.version !== document.version) return [];
    return nonOverlapping(state.report.messages).map((fix) => fixEdit(document, fix));
  }
  const run = await resolveRun(document);
  if (typeof run === "string") return [];
  const original = document.getText();
  let text = original;
  for (let pass = 0; pass < maxFixPasses; pass++) {
    const { report } = await run.engine.lint(
      text,
      run.filePath,
      run.compiled,
      run.native
    );
    const fixes = nonOverlapping(report.messages);
    if (fixes.length === 0) break;
    text = applyFixes(text, fixes);
  }
  return editsBetween(document, original, text);
}

/** True when a code action request's `only` asks for fix-all: the kind
 * itself or a parent of it. */
function wantsFixAll(only: readonly string[] | undefined): boolean {
  return (only ?? []).some(
    (kind) => kind === fixAllKind || fixAllKind.startsWith(`${kind}.`)
  );
}

connection.onInitialize((): InitializeResult => {
  return {
    capabilities: {
      textDocumentSync      : TextDocumentSyncKind.Incremental,
      diagnosticProvider: {
        identifier           : "lintrix",
        interFileDependencies: false,
        workspaceDiagnostics : false,
      },
      codeActionProvider: {
        codeActionKinds: [CodeActionKind.QuickFix, fixAllKind],
      },
      executeCommandProvider: {
        commands: [applyAllFixesCommand],
      },
    },
  };
});

connection.onCodeAction(async (params): Promise<CodeAction[]> => {
  const document = documents.get(params.textDocument.uri);
  if (document === undefined) return [];
  const only = params.context.only;

  if (wantsFixAll(only)) {
    const mode = (await settings.forDocument(document.uri)).codeActionsOnSave.mode;
    const edits = await computeAllFixes(document, mode);
    if (edits.length === 0) return [];
    return [
      {
        title: "Fix all auto-fixable lintrix problems",
        kind : fixAllKind,
        edit: {
          documentChanges: [
            { textDocument: { uri: document.uri, version: document.version }, edits },
          ],
        },
      },
    ];
  }
  // A request for another source kind (organize imports, say) is not ours.
  if (only !== undefined && only.length > 0 && !only.includes(CodeActionKind.QuickFix)) {
    return [];
  }

  const state = states.get(document.uri);
  if (state === undefined) return [];
  const ours = params.context.diagnostics.filter((d) => d.source === source);
  const actions = quickFixes(document, state, ours);
  if (actions.length > 0 && state.report.messages.some((m) => m.fix !== undefined)) {
    actions.push({
      title  : "Fix all auto-fixable problems",
      kind   : CodeActionKind.QuickFix,
      command: {
        title    : "Fix all",
        command  : applyAllFixesCommand,
        arguments: [{ uri: document.uri, version: document.version }],
      },
    });
  }
  return actions;
});

connection.onExecuteCommand(async (params) => {
  if (params.command !== applyAllFixesCommand) return null;
  const target = params.arguments?.[0] as { uri: string; version?: number } | undefined;
  const document = target === undefined ? undefined : documents.get(target.uri);
  if (document === undefined) return null;
  if (target?.version !== undefined && target.version !== document.version) return null;
  const edits = await computeAllFixes(document, "all");
  if (edits.length === 0) return null;
  const response = await connection.workspace.applyEdit({
    documentChanges: [
      { textDocument: { uri: document.uri, version: document.version }, edits },
    ],
  });
  if (!response.applied) {
    connection.console.error(
      `fix all was not applied: ${response.failureReason ?? "unknown"}`
    );
  }
  return null;
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

/** A config or a tsconfig, whose change alters what other files lint with. */
function isConfigFile(filePath: string): boolean {
  const name = path.basename(filePath);
  return name === "tsconfig.json" || name.startsWith("lintrix.config.");
}

// The client watches lintrix.config.*, tsconfig.json and the source files. A
// config change can alter what any open document lints with, so everything is
// re-resolved; a source change only reaches the native binaries, whose type
// server would otherwise keep the old file, and the next pull sees it.
connection.onDidChangeWatchedFiles((params) => {
  const changed = params.changes
    .map((change) => URI.parse(change.uri))
    .filter((uri) => uri.scheme === "file")
    .map((uri) => uri.fsPath);
  void natives.changed(changed);
  if (changed.some(isConfigFile)) {
    configs.clear();
    refresh();
  }
});

connection.onDidChangeConfiguration(() => {
  settings.clear();
  refresh();
});

connection.onNotification(revalidateNotification, () => {
  configs.clear();
  settings.clear();
  void natives.configChanged();
  refresh();
});

documents.onDidClose((event) => {
  states.delete(event.document.uri);
  settings.forget(event.document.uri);
  const { filePath, onDisk } = filePathFor(event.document);
  if (onDisk) void natives.close(filePath);
});

connection.onShutdown(() => {
  natives.dispose();
});

documents.listen(connection);
connection.listen();
