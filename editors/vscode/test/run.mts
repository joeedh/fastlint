// Launches the smoke test (task 9.6): downloads a VS Code into .vscode-test/
// on first use, writes a fixture workspace, and runs test/suite.ts inside the
// extension development host over this directory. `node make.ts vsix --smoke`
// runs it after the bundles are built.

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { runTests } from "@vscode/test-electron";

const extensionDevelopmentPath = path.resolve(import.meta.dirname, "..");
const extensionTestsPath = path.join(extensionDevelopmentPath, "out", "test", "suite.js");
if (!fs.existsSync(extensionTestsPath)) {
  console.error(`${extensionTestsPath} is not built; run \`node esbuild.mts\` first`);
  process.exit(2);
}

const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "lintrix-vscode-"));
fs.writeFileSync(
  path.join(fixture, "lintrix.config.json"),
  '{ "extends": "lintrix:recommended", "rules": { "curly": "warn" } }\n'
);
fs.writeFileSync(path.join(fixture, "a.ts"), "if (a) b();\ndebugger;\n");

try {
  await runTests({
    extensionDevelopmentPath,
    extensionTestsPath,
    // The fixture opens as the workspace. Other extensions stay out, and
    // workspace trust is off since the extension refuses untrusted folders.
    launchArgs: [fixture, "--disable-extensions", "--disable-workspace-trust"],
  });
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
} finally {
  fs.rmSync(fixture, { recursive: true, force: true });
}
