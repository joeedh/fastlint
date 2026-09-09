import fs from "node:fs";
import path from "node:path";
import { fail, step } from "./log.ts";
import { napiDir, repoRoot } from "./paths.ts";
import { run } from "./spawn.ts";
import { buildEnv, isWindows, toolchain } from "./toolchain.ts";

/**
 * The runtimes the addon can be built against. Node is the default; Electron
 * covers the editor integrations, whose host is Electron rather than Node.
 */
export const napiRuntimes = ["node", "electron"] as const;
export type NapiRuntime = (typeof napiRuntimes)[number];

/** The addon as it lands in the build directory. `napi/CMakeLists.txt` pins the
 * output directory, so this does not depend on how cmake-js lays one out. */
export function addonPath(): string {
  return path.join(napiDir, "fastlint.node");
}

/**
 * The version to build against: the running Node for `node`, or the pinned
 * `electron` devDependency. cmake-js downloads that runtime's headers and, on
 * Windows, its import library.
 */
function runtimeVersion(runtime: NapiRuntime, given: string | undefined): string {
  if (given) return given;
  if (runtime === "node") return process.versions.node;
  const pkg = JSON.parse(
    fs.readFileSync(path.join(repoRoot, "package.json"), "utf8")
  ) as {
    devDependencies?: Record<string, string>;
    dependencies?: Record<string, string>;
  };
  const spec = pkg.devDependencies?.["electron"] ?? pkg.dependencies?.["electron"] ?? "";
  const match = /\d+\.\d+\.\d+/.exec(spec);
  if (!match) {
    fail("no electron version to build against; pass --runtime-version, or add electron");
  }
  return match[0];
}

function cmakeJs(): string[] {
  const bin = path.join(repoRoot, "node_modules", "cmake-js", "bin", "cmake-js");
  if (!fs.existsSync(bin)) {
    fail("cmake-js is not installed; run `pnpm install`");
  }
  // Invoked through this Node rather than the .cmd shim, so the addon is built
  // against the same Node that runs the tooling.
  return [process.execPath, bin];
}

export interface NapiOptions {
  runtime?: NapiRuntime;
  runtimeVersion?: string;
}

/**
 * Configures build/napi through cmake-js, which downloads the runtime's headers
 * and import library and injects CMAKE_JS_INC/LIB/SRC. Only the configure step
 * goes through cmake-js; the build below is an ordinary `cmake --build`, so the
 * addon compiles with the same generator and flags as the rest of the tree.
 */
export async function configureNapi(options: NapiOptions = {}): Promise<void> {
  const runtime = options.runtime ?? "node";
  const version = runtimeVersion(runtime, options.runtimeVersion);
  const [node, script] = cmakeJs() as [string, string];
  const tools = toolchain();

  fs.mkdirSync(napiDir, { recursive: true });
  step(`configure napi for ${runtime} ${version} -> ${path.relative(repoRoot, napiDir)}`);

  const args = [
    script,
    "configure",
    "--out",
    napiDir,
    "--generator",
    "Ninja",
    "--runtime",
    runtime,
    "--runtime-version",
    version,
    "--arch",
    process.arch === "arm64" ? "arm64" : "x64",
    "--config=Release",
    `--CDCMAKE_MAKE_PROGRAM=${tools.ninja.replace(/\\/g, "/")}`,
    // cmake-js is not preset-aware, so the settings the presets carry are
    // passed explicitly here.
    "--CDCMAKE_EXPORT_COMPILE_COMMANDS=ON",
  ];
  if (isWindows) {
    // cmake-js defaults to the static CRT; the addon has to match the runtime
    // that loads it, or the CRT's dllimport symbols go unresolved at link.
    args.push("--CDCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL");
    // A long include list blows past cmd's 8191-character limit once the
    // runtime headers are added to it.
    args.push("--CDCMAKE_NINJA_FORCE_RESPONSE_FILE=ON");
  }

  await run(node, args, { cwd: repoRoot, env: buildEnv() });
}

/** Builds only the addon target, leaving the CLI and the tests alone. */
export async function buildNapi(options: NapiOptions = {}): Promise<void> {
  if (!fs.existsSync(path.join(napiDir, "CMakeCache.txt"))) {
    await configureNapi(options);
  }
  const tools = toolchain();
  step(`build napi -> ${path.relative(repoRoot, addonPath())}`);
  await run(tools.cmake, ["--build", napiDir, "--target", "fastlint_node"], {
    cwd: repoRoot,
    env: buildEnv(),
  });
}

/**
 * Loads the freshly built addon in a child process and lints one line of
 * TypeScript through it. A load failure is the usual way an ABI or CRT mismatch
 * shows up, and it only shows up at load time.
 */
export async function smokeNapi(): Promise<void> {
  const addon = addonPath();
  if (!fs.existsSync(addon)) fail(`addon not found at ${addon}`);
  step("smoke test the addon");
  // `node -e` runs as CommonJS, so the addon loads through a plain require.
  const script = [
    `const addon = require(${JSON.stringify(addon.replace(/\\/g, "/"))});`,
    `console.log("lintrix", addon.version());`,
    `const out = JSON.parse(addon.lintText("if (a == b) debugger;", "smoke.ts"));`,
    `console.log(out[0].messages.length, "problems from the smoke source");`,
    `if (out[0].messages.length === 0) throw new Error("expected the recommended rules to report");`,
  ].join("\n");
  await run(process.execPath, ["-e", script], { cwd: repoRoot, env: buildEnv() });

  // The rule-loading runtime: two TypeScript rules over the addon's accessors.
  const tsDir = path.join(repoRoot, "source", "fastlint", "plugin", "ts");
  step("smoke test the TypeScript rule runtime");
  await run(process.execPath, [path.join(tsDir, "runtime.smoke.ts"), addon], {
    cwd: repoRoot,
    env: buildEnv(),
  });

  // The config loader and the worker-pool file driver.
  step("smoke test the config loader and file driver");
  await run(process.execPath, [path.join(tsDir, "driver.smoke.ts"), addon], {
    cwd: repoRoot,
    env: buildEnv(),
  });
}
