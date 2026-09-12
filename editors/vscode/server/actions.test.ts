import assert from "node:assert/strict";
import { test } from "node:test";
import { CodeActionKind, type TextDocumentEdit } from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";

import type { EslintMessage } from "../../../source/fastlint/plugin/ts/engine.ts";
import {
  applyFixes,
  disableFileEdit,
  disableLineEdit,
  fixEdit,
  nonOverlapping,
  quickFixes,
} from "./actions.ts";
import { toState } from "./diagnostics.ts";

const doc = (text: string, version = 1): TextDocument =>
  TextDocument.create("file:///p/a.ts", "typescript", version, text);

const message = (over: Partial<EslintMessage> = {}): EslintMessage => ({
  ruleId   : "no-debugger",
  url      : "https://example.test/no-debugger",
  severity : 2,
  message  : "Unexpected 'debugger' statement.",
  line     : 1,
  column   : 1,
  endLine  : 1,
  endColumn: 10,
  ...over,
});

const report = (messages: EslintMessage[]) => ({
  filePath: "/p/a.ts",
  messages,
  errorCount         : messages.length,
  warningCount       : 0,
  fixableErrorCount  : 0,
  fixableWarningCount: 0,
});

/** The edits of an action's inline workspace edit. */
const editsOf = (action: { edit?: { documentChanges?: unknown[] } }) =>
  (action.edit!.documentChanges![0] as TextDocumentEdit).edits;

test("a fix maps its UTF-16 range to positions", () => {
  const d = doc("x;\ndebugger;\n");
  const edit = fixEdit(d, { range: [3, 12], text: "" });
  assert.deepEqual(edit.range, {
    start: { line: 1, character: 0 },
    end  : { line: 1, character: 9 },
  });
  assert.equal(edit.newText, "");
});

test("overlapping fixes keep the first in source order", () => {
  const fixes = nonOverlapping([
    message({ fix: { range: [10, 20], text: "b" } }),
    message({ fix: { range: [0, 5], text: "a" } }),
    message({ fix: { range: [4, 8], text: "c" } }),
    message({ fix: { range: [20, 21], text: "d" } }),
    message(),
  ]);
  assert.deepEqual(
    fixes.map((f) => f.range),
    [
      [0, 5],
      [10, 20],
      [20, 21],
    ]
  );
});

test("applying fixes splices the text", () => {
  const text = "debugger;\nif (a) b();\n";
  const fixed = applyFixes(text, [
    { range: [0, 9], text: "" },
    { range: [17, 21], text: "{ b(); }" },
  ]);
  assert.equal(fixed, "\nif (a) { b(); }\n");
});

test("disable for the line inserts a directive with the line's indentation", () => {
  const d = doc("function f() {\n  debugger;\n}\n");
  const edit = disableLineEdit(d, message({ line: 2, column: 3 }), "no-debugger");
  assert.deepEqual(edit.range.start, { line: 1, character: 0 });
  assert.equal(edit.newText, "  // lintrix-disable-next-line no-debugger\n");
});

test("disable for the line extends a directive already above, in its spelling", () => {
  const cases: [string, number, string][] = [
    ["// eslint-disable-next-line curly\ndebugger;\n", 33, ", no-debugger"],
    ["// lintrix-disable-next-line curly -- why\ndebugger;\n", 34, ", no-debugger"],
    ["/* lintrix-disable-next-line curly */\ndebugger;\n", 34, ", no-debugger"],
  ];
  for (const [text, character, newText] of cases) {
    const edit = disableLineEdit(doc(text), message({ line: 2 }), "no-debugger");
    assert.deepEqual(edit.range.start, { line: 0, character }, text);
    assert.equal(edit.newText, newText);
  }
});

test("disable for the line uses the document's line endings", () => {
  const edit = disableLineEdit(
    doc("x;\r\ndebugger;\r\n"),
    message({ line: 2 }),
    "no-debugger"
  );
  assert.equal(edit.newText, "// lintrix-disable-next-line no-debugger\r\n");
});

test("disable for the file goes at the top, below a shebang", () => {
  assert.deepEqual(disableFileEdit(doc("debugger;\n"), "no-debugger"), {
    range  : { start: { line: 0, character: 0 }, end: { line: 0, character: 0 } },
    newText: "/* lintrix-disable no-debugger */\n",
  });
  const below = disableFileEdit(doc("#!/usr/bin/env node\ndebugger;\n"), "no-debugger");
  assert.equal(below.range.start.line, 1);
});

test("quick fixes for a fixable problem with suggestions and a sibling", () => {
  const text = "debugger;\ndebugger;\n";
  const d = doc(text);
  const messages = [
    message({
      fix        : { range: [0, 9], text: "" },
      suggestions: [{ desc: "Remove it", fix: { range: [0, 10], text: "" } }],
    }),
    message({ line: 2, fix: { range: [10, 19], text: "" } }),
  ];
  const state = toState(d, report(messages));
  const actions = quickFixes(d, state, [state.diagnostics[0]!]);
  assert.deepEqual(
    actions.map((a) => a.title),
    [
      "Fix this no-debugger problem",
      "Remove it",
      "Fix all no-debugger problems",
      "Disable no-debugger for this line",
      "Disable no-debugger for the entire file",
      "Show documentation for no-debugger",
    ]
  );
  assert.equal(actions[0]!.isPreferred, true);
  assert.equal(actions[0]!.kind, CodeActionKind.QuickFix);
  assert.deepEqual(actions[0]!.diagnostics, [state.diagnostics[0]]);
  assert.equal(editsOf(actions[0]!).length, 1);
  assert.equal(editsOf(actions[2]!).length, 2);
  assert.deepEqual(actions[5]!.command?.arguments, ["https://example.test/no-debugger"]);
  const versioned = actions[0]!.edit!.documentChanges![0] as TextDocumentEdit;
  assert.equal(versioned.textDocument.version, 1);
});

test("a problem without a rule, or from an older version, gets no actions", () => {
  const d = doc("(\n");
  const state = toState(d, report([message({ ruleId: null, fatal: true })]));
  assert.deepEqual(quickFixes(d, state, state.diagnostics), []);

  const stale = toState(
    doc("debugger;\n"),
    report([message({ fix: { range: [0, 9], text: "" } })])
  );
  assert.deepEqual(quickFixes(doc("debugger;;\n", 2), stale, stale.diagnostics), []);
});
