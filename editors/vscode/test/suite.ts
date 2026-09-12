// The smoke test (task 9.6), run inside the extension development host by
// test/run.mts. It drives the real extension through the VS Code API: open
// the fixture file, wait for lintrix diagnostics, run fix-all, and wait for
// them to clear. `run` rejects on the first failed assertion, which is what
// test-electron turns into a non-zero exit.

import assert from "node:assert/strict";
import * as vscode from "vscode";

const extensionId = "joeedh.lintrix-vscode";

/** Polls `probe` until it returns a value, or fails after `timeoutMs`. */
async function waitFor<T>(
  what: string,
  probe: () => T | undefined,
  timeoutMs = 15000
): Promise<T> {
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const value = probe();
    if (value !== undefined) return value;
    if (Date.now() > deadline) throw new Error(`timed out waiting for ${what}`);
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
}

/** A diagnostic's rule name. VS Code turns an LSP code with a description
 * into `{value, target}`. */
function codeOf(diagnostic: vscode.Diagnostic): string | undefined {
  const code = diagnostic.code;
  if (code === undefined) return undefined;
  return typeof code === "object" ? String(code.value) : String(code);
}

function lintrixDiagnostics(uri: vscode.Uri): vscode.Diagnostic[] {
  return vscode.languages.getDiagnostics(uri).filter((d) => d.source === "lintrix");
}

export async function run(): Promise<void> {
  const extension = vscode.extensions.getExtension(extensionId);
  assert.ok(extension, `extension ${extensionId} is not loaded`);
  await extension.activate();

  const folder = vscode.workspace.workspaceFolders?.[0];
  assert.ok(folder, "no workspace folder; run.mts passes the fixture directory");
  const uri = vscode.Uri.joinPath(folder.uri, "a.ts");
  const document = await vscode.workspace.openTextDocument(uri);
  await vscode.window.showTextDocument(document);

  // With a native binary (run.mts sets `lintrix.binaryPath`) the type-aware
  // rules run too, so the unnecessary `!` is reported and fixed with the rest.
  const native = process.env["LINTRIX_SMOKE_NATIVE"] === "1";
  const expected = ["curly", "no-debugger"];
  if (native) expected.push("no-unnecessary-type-assertion");
  const diagnostics = await waitFor("lintrix diagnostics", () => {
    const found = lintrixDiagnostics(uri);
    return found.length >= expected.length ? found : undefined;
  });
  assert.deepEqual(diagnostics.map(codeOf).sort(), expected.sort());
  const debuggerProblem = diagnostics.find((d) => codeOf(d) === "no-debugger")!;
  assert.equal(debuggerProblem.severity, vscode.DiagnosticSeverity.Error);
  assert.equal(debuggerProblem.range.start.line, 3);

  const actions = await vscode.commands.executeCommand<vscode.CodeAction[]>(
    "vscode.executeCodeActionProvider",
    uri,
    debuggerProblem.range
  );
  const titles = actions.map((action) => action.title);
  assert.ok(
    titles.includes("Fix this no-debugger problem"),
    `quick fixes offered: ${titles.join(", ")}`
  );

  await vscode.commands.executeCommand("lintrix.executeAutofix");
  await waitFor("the fix-all edit", () =>
    document.getText().includes("debugger") ? undefined : true
  );
  assert.ok(document.getText().includes("{"), "curly's fix should have braced the if");
  if (native) {
    assert.ok(
      !document.getText().includes("!"),
      "the unnecessary assertion should be gone"
    );
  }
  await waitFor("the diagnostics to clear", () =>
    lintrixDiagnostics(uri).length === 0 ? true : undefined
  );
}
