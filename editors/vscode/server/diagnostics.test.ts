import assert from "node:assert/strict";
import { test } from "node:test";
import { DiagnosticSeverity, DiagnosticTag } from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";

import type { EslintMessage } from "../../../source/fastlint/plugin/ts/engine.ts";
import { toDiagnostic, toState, type DiagnosticData } from "./diagnostics.ts";

const doc = (text: string): TextDocument =>
  TextDocument.create("file:///p/a.ts", "typescript", 1, text);

const message = (over: Partial<EslintMessage> = {}): EslintMessage => ({
  ruleId   : "no-debugger",
  url      : "https://example.test/no-debugger",
  severity : 2,
  message  : "Unexpected 'debugger' statement.",
  line     : 2,
  column   : 1,
  endLine  : 2,
  endColumn: 10,
  ...over,
});

test("a message maps to a zero-based range with its rule as the code", () => {
  const d = toDiagnostic(doc("x;\ndebugger;\n"), message(), 3);
  assert.deepEqual(d.range, {
    start: { line: 1, character: 0 },
    end  : { line: 1, character: 9 },
  });
  assert.equal(d.severity, DiagnosticSeverity.Error);
  assert.equal(d.code, "no-debugger");
  assert.equal(d.codeDescription?.href, "https://example.test/no-debugger");
  assert.equal(d.source, "lintrix");
  assert.deepEqual(d.data, { index: 3 } satisfies DiagnosticData);
});

test("a warning without an end collapses to its start", () => {
  const d = toDiagnostic(
    doc("x;\n"),
    message({
      severity : 1,
      endLine  : undefined,
      endColumn: undefined,
      line     : 1,
      column   : 2,
    }),
    0
  );
  assert.equal(d.severity, DiagnosticSeverity.Warning);
  assert.deepEqual(d.range.start, d.range.end);
  assert.deepEqual(d.range.start, { line: 0, character: 1 });
});

test("a position past the document clamps to its end", () => {
  const d = toDiagnostic(
    doc("x;\n"),
    message({ line: 9, column: 9, endLine: 9, endColumn: 12 }),
    0
  );
  assert.deepEqual(d.range.start, { line: 1, character: 0 });
  assert.deepEqual(d.range.end, { line: 1, character: 0 });
});

test("a syntax error has no code and no tag", () => {
  const d = toDiagnostic(
    doc("(\n"),
    message({ ruleId: null, url: undefined, fatal: true, message: "Expected ')'" }),
    0
  );
  assert.equal(d.code, undefined);
  assert.equal(d.codeDescription, undefined);
  assert.equal(d.tags, undefined);
});

test("an unused directive is tagged unnecessary", () => {
  const d = toDiagnostic(
    doc("// lintrix-disable-next-line\nx;\n"),
    message({
      ruleId : null,
      url    : undefined,
      message: "Unused lintrix-disable directive (no problems were reported).",
      line   : 1,
      column : 1,
    }),
    0
  );
  assert.deepEqual(d.tags, [DiagnosticTag.Unnecessary]);
  assert.equal(d.code, undefined);
});

test("state keeps the report and indexes each diagnostic into it", () => {
  const document = doc("debugger;\ndebugger;\n");
  const report = {
    filePath           : "/p/a.ts",
    messages           : [message({ line: 1 }), message({ line: 2 })],
    errorCount         : 2,
    warningCount       : 0,
    fixableErrorCount  : 0,
    fixableWarningCount: 0,
  };
  const state = toState(document, report);
  assert.equal(state.version, 1);
  assert.equal(state.report, report);
  assert.deepEqual(
    state.diagnostics.map((d) => (d.data as DiagnosticData).index),
    [0, 1]
  );
});
