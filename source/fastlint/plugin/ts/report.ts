// Merging and printing a run's problems (task 8.3). The built-in rules run in
// one engine and the plugin rules in another, so the CLI is the only place that
// sees a file's whole result: it joins the two lists, orders them by position
// and prints them the way lint/format.cc prints a native run.

import type { EslintMessage, FileReport } from "./engine.ts";
import type { FileMessages } from "./driver.ts";

/** A path compared as the two engines spell it, which differ in separator. */
function key(filePath: string): string {
  return filePath.replace(/\\/g, "/").toLowerCase();
}

/** Orders problems the way a reader reads them, earliest position first. */
function byPosition(a: EslintMessage, b: EslintMessage): number {
  return a.line - b.line || a.column - b.column;
}

/** Recounts `report` from its messages, after the merge changed them. */
function counted(report: FileReport): FileReport {
  let errorCount = 0;
  let warningCount = 0;
  let fixableErrorCount = 0;
  let fixableWarningCount = 0;
  for (const message of report.messages) {
    if (message.severity === 2) {
      errorCount++;
      if (message.fix) fixableErrorCount++;
    } else {
      warningCount++;
      if (message.fix) fixableWarningCount++;
    }
  }
  return { ...report, errorCount, warningCount, fixableErrorCount, fixableWarningCount };
}

/**
 * One report per file in `files`, holding the built-in problems `native` found
 * and the plugin problems `plugin` found, in position order. A file neither
 * engine answered for gets an empty report, so the count of files linted is the
 * count the CLI was asked for.
 */
export function mergeReports(
  files: readonly string[],
  native: readonly FileReport[],
  plugin: readonly FileMessages[]
): FileReport[] {
  const byFile = new Map<string, FileReport>();
  for (const file of files) {
    byFile.set(key(file), {
      filePath: file,
      messages: [],
      errorCount: 0,
      warningCount: 0,
      fixableErrorCount: 0,
      fixableWarningCount: 0,
    });
  }
  const reportFor = (filePath: string): FileReport => {
    const found = byFile.get(key(filePath));
    if (found) return found;
    const added = {
      filePath,
      messages: [],
      errorCount: 0,
      warningCount: 0,
      fixableErrorCount: 0,
      fixableWarningCount: 0,
    };
    byFile.set(key(filePath), added);
    return added;
  };

  for (const report of native) {
    reportFor(report.filePath).messages.push(...report.messages);
  }
  for (const file of plugin) {
    const report = reportFor(file.filename);
    if (file.error) {
      report.messages.push({
        ruleId: null,
        severity: 2,
        message: file.error,
        line: 1,
        column: 1,
        fatal: true,
      });
      continue;
    }
    for (const message of file.messages) {
      report.messages.push({
        ruleId: message.ruleId,
        severity: message.severity,
        message: message.message,
        line: message.line,
        column: message.column,
        endLine: message.endLine,
        endColumn: message.endColumn,
      });
    }
  }

  return [...byFile.values()].map((report) => {
    report.messages.sort(byPosition);
    return counted(report);
  });
}

/** Drops the warnings from every report, for `--quiet`. */
export function withoutWarnings(reports: readonly FileReport[]): FileReport[] {
  return reports.map((report) =>
    counted({ ...report, messages: report.messages.filter((m) => m.severity === 2) })
  );
}

const paint = (code: string, text: string, color: boolean): string =>
  color ? `\u001b[${code}m${text}\u001b[0m` : text;

/**
 * ESLint's stylish listing: the file path, then one aligned
 * `line:col severity message rule` row per problem, then the totals.
 */
export function formatPretty(reports: readonly FileReport[], color: boolean): string {
  const lines: string[] = [];
  let errors = 0;
  let warnings = 0;
  let fixableErrors = 0;
  let fixableWarnings = 0;

  for (const report of reports) {
    errors += report.errorCount;
    warnings += report.warningCount;
    fixableErrors += report.fixableErrorCount;
    fixableWarnings += report.fixableWarningCount;
    if (report.messages.length === 0) continue;

    const rows = report.messages.map((message) => ({
      at: `${message.line}:${message.column}`,
      severity: message.severity === 2 ? "error" : "warning",
      text: message.message,
      rule: message.ruleId ?? "",
    }));
    const atWidth = Math.max(...rows.map((row) => row.at.length));
    const severityWidth = Math.max(...rows.map((row) => row.severity.length));
    const textWidth = Math.max(...rows.map((row) => row.text.length));

    lines.push(paint("4", report.filePath, color));
    for (const row of rows) {
      const severity = paint(
        row.severity === "error" ? "31" : "33",
        row.severity.padEnd(severityWidth),
        color
      );
      const text = row.rule.length > 0 ? row.text.padEnd(textWidth) : row.text;
      const rule = row.rule.length > 0 ? `  ${paint("2", row.rule, color)}` : "";
      lines.push(`  ${row.at.padStart(atWidth)}  ${severity}  ${text}${rule}`.trimEnd());
    }
    lines.push("");
  }

  const problems = errors + warnings;
  if (problems > 0) {
    const plural = (n: number, word: string): string =>
      `${n} ${word}${n === 1 ? "" : "s"}`;
    lines.push(
      paint(
        errors > 0 ? "31" : "33",
        `${plural(problems, "problem")} (${plural(errors, "error")}, ${plural(warnings, "warning")})`,
        color
      )
    );
    if (fixableErrors + fixableWarnings > 0) {
      lines.push(
        `  ${plural(fixableErrors, "error")} and ${plural(fixableWarnings, "warning")} potentially fixable with the --fix option.`
      );
    }
    lines.push("");
  }
  return lines.join("\n");
}

/** The ESLint JSON formatter: the reports as they stand. */
export function formatJson(reports: readonly FileReport[]): string {
  return `${JSON.stringify(reports, undefined, 2)}\n`;
}
