// Code actions and the edits behind them (task 9.4). A message's `fix` is a
// UTF-16 range and text over the linted source, which is the document's own
// coordinate space, so it maps to a `TextEdit` by `positionAt` alone. Every
// action carries its edit inline, versioned to the document it was computed
// for, so VS Code applies it without a round trip and refuses it once the
// document has moved on.

import {
  CodeActionKind,
  type CodeAction,
  type Diagnostic,
  type TextEdit,
  type WorkspaceEdit,
} from "vscode-languageserver/node";
import type { TextDocument } from "vscode-languageserver-textdocument";

import type {
  EslintFix,
  EslintMessage,
} from "../../../source/fastlint/plugin/ts/engine.ts";
import type { DiagnosticData, DocumentState } from "./diagnostics.ts";

/** The kind fix-all registers under, so `editor.codeActionsOnSave` can name
 * it and a bare `source.fixAll` also reaches it. */
export const fixAllKind = `${CodeActionKind.SourceFixAll}.lintrix`;

/** The command a code action opens a rule's page through; the client
 * registers it, since only the client can open a browser. */
export const openRuleDocCommand = "lintrix.openRuleDoc";

/** The command fix-all runs through when it is asked for as a command rather
 * than as an on-save action; the server registers it. */
export const applyAllFixesCommand = "lintrix.applyAllFixes";

export function fixEdit(document: TextDocument, fix: EslintFix): TextEdit {
  return {
    range: {
      start: document.positionAt(fix.range[0]),
      end  : document.positionAt(fix.range[1]),
    },
    newText: fix.text,
  };
}

/** The fixes of `messages` that can apply together: in source order, each
 * starting at or after the previous one ended. */
export function nonOverlapping(messages: readonly EslintMessage[]): EslintFix[] {
  const fixes = messages
    .filter((message) => message.fix !== undefined)
    .map((message) => message.fix!)
    .sort((a, b) => a.range[0] - b.range[0] || a.range[1] - b.range[1]);
  const out: EslintFix[] = [];
  let end = -1;
  for (const fix of fixes) {
    if (fix.range[0] < end) continue;
    out.push(fix);
    end = fix.range[1];
  }
  return out;
}

/** `text` with `fixes` applied, which must be non-overlapping and in order. */
export function applyFixes(text: string, fixes: readonly EslintFix[]): string {
  let out = "";
  let at = 0;
  for (const fix of fixes) {
    out += text.slice(at, fix.range[0]) + fix.text;
    at = fix.range[1];
  }
  return out + text.slice(at);
}

function eolOf(document: TextDocument): string {
  return document.getText().includes("\r\n") ? "\r\n" : "\n";
}

function lineText(document: TextDocument, line: number): string {
  const start = document.offsetAt({ line, character: 0 });
  const end = document.offsetAt({ line: line + 1, character: 0 });
  return document
    .getText()
    .slice(start, end)
    .replace(/\r?\n$/, "");
}

/** A `disable-next-line` directive on `line`, in either spelling and either
 * comment form. */
const nextLineDirective = /(\/\/|\/\*)\s*(lintrix|eslint)-disable-next-line\b/;

// Where a rule name is appended to an existing directive line: before the
// ` -- justification` tail if there is one, otherwise before the closing of a
// block comment, otherwise at the end.
function appendIndex(line: string, block: boolean): number {
  let at = line.indexOf("--");
  if (at < 0) {
    if (!block) return line.length;
    at = line.indexOf("*/");
    if (at < 0) return line.length;
  }
  while (at > 0 && line[at - 1] === " ") at--;
  return at;
}

/**
 * Disables `ruleId` on the line of `message`: appended to a
 * `disable-next-line` directive already above it, in that directive's own
 * spelling, or as a new `// lintrix-disable-next-line` above it with the
 * line's indentation.
 */
export function disableLineEdit(
  document: TextDocument,
  message: EslintMessage,
  ruleId: string
): TextEdit {
  const line = message.line - 1;
  if (line > 0) {
    const above = lineText(document, line - 1);
    const directive = nextLineDirective.exec(above);
    if (directive) {
      const at = appendIndex(above, directive[1] === "/*");
      return {
        range: {
          start: { line: line - 1, character: at },
          end  : { line: line - 1, character: at },
        },
        newText: `, ${ruleId}`,
      };
    }
  }
  const indent = /^[ \t]*/.exec(lineText(document, line))![0];
  return {
    range  : { start: { line, character: 0 }, end: { line, character: 0 } },
    newText: `${indent}// lintrix-disable-next-line ${ruleId}${eolOf(document)}`,
  };
}

/** Disables `ruleId` for the whole file with a block directive at the top,
 * below a shebang when there is one. */
export function disableFileEdit(document: TextDocument, ruleId: string): TextEdit {
  const line = document.getText().startsWith("#!") ? 1 : 0;
  return {
    range  : { start: { line, character: 0 }, end: { line, character: 0 } },
    newText: `/* lintrix-disable ${ruleId} */${eolOf(document)}`,
  };
}

function versionedEdit(document: TextDocument, edits: TextEdit[]): WorkspaceEdit {
  return {
    documentChanges: [
      { textDocument: { uri: document.uri, version: document.version }, edits },
    ],
  };
}

function action(
  title: string,
  kind: string,
  document: TextDocument,
  edits: TextEdit[],
  diagnostic?: Diagnostic
): CodeAction {
  const out: CodeAction = { title, kind, edit: versionedEdit(document, edits) };
  if (diagnostic) out.diagnostics = [diagnostic];
  return out;
}

/**
 * Builds the quick fixes for the diagnostics VS Code asked about. Each one
 * gets the message's own fix (preferred), each suggestion, a fix for every
 * same-rule problem when there is more than one, the two disable directives
 * and the rule's page. A diagnostic without a rule gets none, and so does one
 * from another version of the document.
 */
export function quickFixes(
  document: TextDocument,
  state: DocumentState,
  diagnostics: readonly Diagnostic[],
  kind: string = CodeActionKind.QuickFix
): CodeAction[] {
  const out: CodeAction[] = [];
  const seenRules = new Set<string>();
  const messages = state.report.messages;

  for (const diagnostic of diagnostics) {
    const data = diagnostic.data as DiagnosticData | undefined;
    if (data === undefined || state.version !== document.version) continue;
    const message = messages[data.index];
    if (message === undefined || message.ruleId === null) continue;
    const ruleId = message.ruleId;

    if (message.fix) {
      const fix = action(
        `Fix this ${ruleId} problem`,
        kind,
        document,
        [fixEdit(document, message.fix)],
        diagnostic
      );
      fix.isPreferred = true;
      out.push(fix);
    }
    for (const suggestion of message.suggestions ?? []) {
      if (!suggestion.fix) continue;
      out.push(
        action(
          suggestion.desc,
          kind,
          document,
          [fixEdit(document, suggestion.fix)],
          diagnostic
        )
      );
    }
    if (seenRules.has(ruleId)) continue;
    seenRules.add(ruleId);

    const same = nonOverlapping(messages.filter((m) => m.ruleId === ruleId));
    if (same.length > 1) {
      out.push(
        action(
          `Fix all ${ruleId} problems`,
          kind,
          document,
          same.map((fix) => fixEdit(document, fix))
        )
      );
    }
    out.push(
      action(`Disable ${ruleId} for this line`, kind, document, [
        disableLineEdit(document, message, ruleId),
      ]),
      action(`Disable ${ruleId} for the entire file`, kind, document, [
        disableFileEdit(document, ruleId),
      ])
    );
    if (message.url !== undefined) {
      out.push({
        title: `Show documentation for ${ruleId}`,
        kind,
        command: { title: "Open", command: openRuleDocCommand, arguments: [message.url] },
      });
    }
  }
  return out;
}
