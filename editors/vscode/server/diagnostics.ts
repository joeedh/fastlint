// Mapping a lint report to LSP diagnostics (task 9.3). The JSON shape already
// counts lines from 1 and columns in UTF-16 code units, so a message maps to a
// `Range` with an offset of one and no re-encoding. Each diagnostic carries the
// index of its message in `data`, which comes back on a code action request,
// so the message's fix and suggestions are found without a second lint.

import {
  DiagnosticSeverity,
  DiagnosticTag,
  type Diagnostic,
  type Position,
} from "vscode-languageserver/node";
import type { TextDocument } from "vscode-languageserver-textdocument";

import type {
  EslintMessage,
  FileReport,
} from "../../../source/fastlint/plugin/ts/engine.ts";

/** What every diagnostic is attributed to. */
export const source = "lintrix";

/** What `data` carries on a diagnostic, so a code action finds its message. */
export interface DiagnosticData {
  index: number;
}

/** The message an unused disable directive is reported with (lint/linter.cc). */
const unusedDirective = /^Unused (lintrix|eslint)-disable directive/;

/** A 1-based line and UTF-16 column as a `Position`, clamped to the document
 * so a stale report never yields a range past the end. */
function positionAt(document: TextDocument, line: number, column: number): Position {
  const at = { line: Math.max(0, line - 1), character: Math.max(0, column - 1) };
  return document.positionAt(document.offsetAt(at));
}

export function toDiagnostic(
  document: TextDocument,
  message: EslintMessage,
  index: number
): Diagnostic {
  const start = positionAt(document, message.line, message.column);
  const end =
    message.endLine === undefined || message.endColumn === undefined
      ? start
      : positionAt(document, message.endLine, message.endColumn);
  const diagnostic: Diagnostic = {
    range   : { start, end },
    severity:
      message.severity === 2 ? DiagnosticSeverity.Error : DiagnosticSeverity.Warning,
    message : message.message,
    source,
    data: { index } satisfies DiagnosticData,
  };
  if (message.ruleId !== null) {
    diagnostic.code = message.ruleId;
    if (message.url !== undefined) diagnostic.codeDescription = { href: message.url };
  } else if (!message.fatal && unusedDirective.test(message.message)) {
    diagnostic.tags = [DiagnosticTag.Unnecessary];
  }
  return diagnostic;
}

/** A diagnostic for a problem with the run itself rather than the source: a
 * config that did not load, or an engine that failed. Shown at the top of the
 * file, so the Problems view lists it beside the status bar's report. */
export function runDiagnostic(message: string): Diagnostic {
  return {
    range   : { start: { line: 0, character: 0 }, end: { line: 0, character: 0 } },
    severity: DiagnosticSeverity.Error,
    message,
    source,
  };
}

/** The last report of an open document, kept beside its diagnostics for the
 * code actions that refer back to it. */
export interface DocumentState {
  version: number;
  report: FileReport;
  diagnostics: Diagnostic[];
}

export function toState(document: TextDocument, report: FileReport): DocumentState {
  return {
    version: document.version,
    report,
    diagnostics: report.messages.map((message, index) =>
      toDiagnostic(document, message, index)
    ),
  };
}
