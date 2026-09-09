// Merging the two engines' results (task 8.3). The built-in rules and the plugin
// rules find problems separately, so what the reader sees depends on this join
// putting them in one list, in position order, with the counts recomputed.

import assert from "node:assert";
import { test } from "node:test";

import type { FileReport } from "./engine.ts";
import type { FileMessages } from "./driver.ts";
import { formatPretty, mergeReports, withoutWarnings } from "./report.ts";

const nativeReport: FileReport = {
  filePath: "C:/p/a.ts",
  messages: [
    {
      ruleId: "no-debugger",
      severity: 2,
      message: "Unexpected 'debugger' statement.",
      line: 3,
      column: 3,
      fix: { range: [10, 20], text: "" },
    },
  ],
  errorCount: 1,
  warningCount: 0,
  fixableErrorCount: 1,
  fixableWarningCount: 0,
};

const pluginMessages: FileMessages = {
  filename: "C:/p/a.ts",
  messages: [
    {
      ruleId: "local/no-let",
      severity: 1,
      message: "Use const.",
      line: 1,
      column: 1,
      endLine: 1,
      endColumn: 4,
      nodeType: "VariableDeclaration",
    },
  ],
};

test("both engines' problems land in one report, in position order", () => {
  const merged = mergeReports(["C:/p/a.ts"], [nativeReport], [pluginMessages]);
  assert.strictEqual(merged.length, 1);
  assert.deepStrictEqual(
    merged[0]!.messages.map((m) => m.ruleId),
    ["local/no-let", "no-debugger"]
  );
  assert.strictEqual(merged[0]!.errorCount, 1);
  assert.strictEqual(merged[0]!.warningCount, 1);
  assert.strictEqual(merged[0]!.fixableErrorCount, 1);
});

test("a file neither engine answered for still gets a report", () => {
  const merged = mergeReports(["C:/p/a.ts", "C:/p/clean.ts"], [], []);
  assert.deepStrictEqual(
    merged.map((report) => report.filePath),
    ["C:/p/a.ts", "C:/p/clean.ts"]
  );
  assert.deepStrictEqual(merged[1]!.messages, []);
});

test("the two engines' spelling of one path is the same file", () => {
  const merged = mergeReports(
    ["C:/p/a.ts"],
    [{ ...nativeReport, filePath: "C:\\p\\a.ts" }],
    [pluginMessages]
  );
  assert.strictEqual(merged.length, 1);
  assert.strictEqual(merged[0]!.messages.length, 2);
});

test("a file the driver could not read is reported as fatal", () => {
  const merged = mergeReports(
    ["C:/p/a.ts"],
    [],
    [{ filename: "C:/p/a.ts", messages: [], error: "ENOENT" }]
  );
  assert.strictEqual(merged[0]!.messages[0]!.fatal, true);
  assert.strictEqual(merged[0]!.errorCount, 1);
});

test("quiet drops the warnings and recounts", () => {
  const merged = withoutWarnings(
    mergeReports(["C:/p/a.ts"], [nativeReport], [pluginMessages])
  );
  assert.deepStrictEqual(
    merged[0]!.messages.map((m) => m.ruleId),
    ["no-debugger"]
  );
  assert.strictEqual(merged[0]!.warningCount, 0);
});

test("the stylish listing aligns its columns and totals the run", () => {
  const merged = mergeReports(["C:/p/a.ts"], [nativeReport], [pluginMessages]);
  const text = formatPretty(merged, false);
  assert.match(text, /^C:\/p\/a\.ts$/m);
  assert.match(text, /^ {2}1:1 {2}warning {2}Use const\. {24}local\/no-let$/m);
  assert.match(text, /^ {2}3:3 {2}error {4}Unexpected 'debugger' statement\. {2}no-debugger$/m);
  assert.match(text, /^2 problems \(1 error, 1 warning\)$/m);
  assert.match(text, /1 error and 0 warnings potentially fixable/);
  // A clean run prints nothing at all, as the native formatter does.
  assert.strictEqual(formatPretty(mergeReports(["C:/p/a.ts"], [], []), false), "");
});
