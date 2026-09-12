// Launches the smoke test (task 9.6): downloads a VS Code into .vscode-test/
// on first use, writes a fixture workspace, and runs test/suite.ts inside the
// extension development host over this directory. `node make.ts vsix --smoke`
// runs it after the bundles are built.

import fs from "node:fs";
import path from "node:path";
import { runTests } from "@vscode/test-electron";

const extensionDevelopmentPath = path.resolve(import.meta.dirname, "..");
const repoRoot = path.resolve(extensionDevelopmentPath, "..", "..");
const extensionTestsPath = path.join(extensionDevelopmentPath, "out", "test", "suite.js");
if (!fs.existsSync(extensionTestsPath)) {
  console.error(`${extensionTestsPath} is not built; run \`node esbuild.mts\` first`);
  process.exit(2);
}

// A built `lintrix` turns the native serve mode on (task 9.5) through the
// workspace's own settings; without one the run stays on WASM. The fixture
// sits under the repository so the binary's type server finds `tsc` in the
// repository's node_modules, walking up from the fixture's tsconfig.
const exe = process.platform === "win32" ? "lintrix.exe" : "lintrix";
const binary = ["release", "relwithdebinfo", "debug"]
  .map((preset) => path.join(repoRoot, "build", preset, "bin", exe))
  .find((candidate) => fs.existsSync(candidate));

const fixture = path.join(repoRoot, "build", "vsix-smoke");
fs.rmSync(fixture, { recursive: true, force: true });
fs.mkdirSync(path.join(fixture, ".vscode"), { recursive: true });
fs.writeFileSync(
  path.join(fixture, "lintrix.config.json"),
  '{ "extends": "lintrix:recommended", "rules": { "curly": "warn" } }\n'
);
fs.writeFileSync(
  path.join(fixture, "tsconfig.json"),
  '{ "compilerOptions": { "strict": true } }\n'
);
fs.writeFileSync(
  path.join(fixture, "a.ts"),
  "declare function foo(): number;\nexport const n = foo()!;\nif (n) foo();\ndebugger;\n"
);
if (binary !== undefined) {
  console.log(`native engine: ${binary}`);
  fs.writeFileSync(
    path.join(fixture, ".vscode", "settings.json"),
    `${JSON.stringify({ "lintrix.binaryPath": binary }, undefined, 2)}\n`
  );
} else {
  console.log("no lintrix binary is built; the smoke test runs on WASM only");
}

try {
  await runTests({
    extensionDevelopmentPath,
    extensionTestsPath,
    extensionTestsEnv: { LINTRIX_SMOKE_NATIVE: binary === undefined ? "" : "1" },
    // The fixture opens as the workspace. Other extensions stay out, and
    // workspace trust is off since the extension refuses untrusted folders.
    launchArgs       : [fixture, "--disable-extensions", "--disable-workspace-trust"],
  });
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
} finally {
  fs.rmSync(fixture, { recursive: true, force: true, maxRetries: 5 });
}
