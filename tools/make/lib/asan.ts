import fs from "node:fs";
import path from "node:path";
import { warn } from "./log.ts";
import { buildEnv, isWindows, toolchain } from "./toolchain.ts";

const asanPresets = new Set(["asan", "clang-asan"]);

export function isAsan(preset: string): boolean {
  return asanPresets.has(preset);
}

/**
 * The build environment plus the ASAN runtime defaults, and on Windows the
 * directory holding `clang_rt.asan_dynamic-x86_64.dll` on PATH — the MSVC
 * sanitizer links against it dynamically and does not put it anywhere the
 * loader looks.
 */
export function asanEnv(preset: string): NodeJS.ProcessEnv {
  const env = { ...buildEnv() };
  if (!isAsan(preset)) return env;

  const options = ["abort_on_error=0", "detect_leaks=0", "print_stats=0", "symbolize=1"];
  if (isWindows) options.push("windows_hook_rtl_allocators=1");
  env["ASAN_OPTIONS"] = options.join(":");

  if (!isWindows) return env;

  const runtime = findAsanRuntime(preset);
  if (runtime) {
    env["PATH"] = [path.dirname(runtime), env["PATH"] ?? ""].join(path.delimiter);
  } else {
    warn("clang_rt.asan_dynamic-x86_64.dll not found; the ASAN build may fail to start");
  }
  return env;
}

/**
 * Locates the ASAN runtime DLL. MSVC and clang-cl ship their own copies and a
 * binary loads only the one its compiler was built against, so the preset
 * decides which directory is searched first.
 */
export function findAsanRuntime(preset = "asan"): string | undefined {
  const tools = toolchain();
  if (!tools.vsInstallPath) return undefined;
  const name = "clang_rt.asan_dynamic-x86_64.dll";
  const llvm = path.join(
    tools.vsInstallPath,
    "VC",
    "Tools",
    "Llvm",
    "x64",
    "lib",
    "clang"
  );
  const msvc = path.join(tools.vsInstallPath, "VC", "Tools", "MSVC");
  const roots = preset === "clang-asan" ? [llvm, msvc] : [msvc, llvm];
  for (const root of roots) {
    const found = findFile(root, name, 6);
    if (found) return found;
  }
  return undefined;
}

function findFile(dir: string, name: string, depth: number): string | undefined {
  if (depth < 0 || !fs.existsSync(dir)) return undefined;
  let entries: fs.Dirent[];
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return undefined;
  }
  for (const entry of entries) {
    if (entry.isFile() && entry.name === name) return path.join(dir, entry.name);
  }
  for (const entry of entries) {
    if (entry.isDirectory()) {
      const found = findFile(path.join(dir, entry.name), name, depth - 1);
      if (found) return found;
    }
  }
  return undefined;
}
