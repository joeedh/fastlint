import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { fail, step } from "./log.ts";
import { emscriptenToolchain } from "./emsdk.ts";
import { buildDir, repoRoot } from "./paths.ts";
import { run } from "./spawn.ts";
import { buildEnv, toolchain } from "./toolchain.ts";

export type WasmPreset = "wasm" | "wasm-release";

export function wasmPreset(release: boolean): WasmPreset {
  return release ? "wasm-release" : "wasm";
}

/**
 * Configures a WASM build directory. The toolchain file is passed on the
 * command line rather than named in the preset, so opening this repository in
 * an IDE that reads CMakePresets does not fail on an unset EMSDK.
 */
export async function configureWasm(preset: WasmPreset): Promise<void> {
  const tools = toolchain();
  const dir = buildDir(preset);
  step(`configure ${preset} -> ${path.relative(repoRoot, dir)}`);
  await run(
    tools.cmake,
    [
      "--preset",
      preset,
      `-DCMAKE_TOOLCHAIN_FILE=${emscriptenToolchain()}`,
      `-DCMAKE_MAKE_PROGRAM=${tools.ninja.replace(/\\/g, "/")}`,
    ],
    { cwd: repoRoot, env: buildEnv("wasm") }
  );
}

/** Builds only the WASM module, leaving the native CLI and tests alone. */
export async function buildWasm(preset: WasmPreset): Promise<void> {
  const dir = buildDir(preset);
  if (!fs.existsSync(path.join(dir, "CMakeCache.txt"))) {
    await configureWasm(preset);
  }
  const tools = toolchain();
  step(`build ${preset} -> ${path.relative(repoRoot, dir)}/bin/fastlint.js`);
  await run(tools.cmake, ["--build", dir, "--target", "fastlint_wasm"], {
    cwd: repoRoot,
    env: buildEnv("wasm"),
  });
}

/**
 * Instantiates the built module under Node and lints one line through it, which
 * is the only way an export list that drops a symbol shows up.
 */
export async function smokeWasm(preset: WasmPreset): Promise<void> {
  const module = path.join(buildDir(preset), "bin", "fastlint.js");
  if (!fs.existsSync(module)) fail(`wasm module not found at ${module}`);
  step("smoke test the wasm module");
  const script = [
    `const createFastlint = (await import(${JSON.stringify(pathToFileURL(module).href)})).default;`,
    `const mod = await createFastlint();`,
    `console.log("lintrix", mod.UTF8ToString(mod._fl_wasm_version()));`,
    `const put = (s) => mod.stringToNewUTF8(s);`,
    `const source = put("if (a == b) debugger;"), name = put("smoke.ts");`,
    `const out = mod._fl_wasm_lint(source, name);`,
    `const json = JSON.parse(mod.UTF8ToString(out));`,
    `mod._fl_wasm_free(out); mod._free(source); mod._free(name);`,
    `console.log(json[0].messages.length, "problems from the smoke source");`,
    `if (json[0].messages.length === 0) throw new Error("expected the recommended rules to report");`,
  ].join("\n");
  await run(process.execPath, ["--input-type=module", "-e", script], { cwd: repoRoot });

  // The TypeScript rule runtime over the WASM heap: the same rules as the addon
  // smoke, through the Emscripten module's accessors.
  step("smoke test the TypeScript rule runtime over WASM");
  const smoke = path.join(
    repoRoot,
    "source",
    "fastlint",
    "plugin",
    "ts",
    "wasm.smoke.ts"
  );
  await run(process.execPath, [smoke, module], { cwd: repoRoot });
}
