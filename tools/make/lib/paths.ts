import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

/** Repository root, derived from this file's location rather than the cwd. */
export const repoRoot = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "..",
  "..",
  ".."
);

export const cacheDir = path.join(repoRoot, ".cache");
export const buildRoot = path.join(repoRoot, "build");
export const sourceDir = path.join(repoRoot, "source");
export const vendorDir = path.join(repoRoot, "vendor");

export const presets = [
  "debug",
  "release",
  "relwithdebinfo",
  "asan",
  "clang-asan",
] as const;
export type Preset = (typeof presets)[number];

export function buildDir(preset: string): string {
  return path.join(buildRoot, preset);
}

export function readJson<T>(file: string): T | undefined {
  if (!fs.existsSync(file)) return undefined;
  try {
    return JSON.parse(fs.readFileSync(file, "utf8")) as T;
  } catch {
    return undefined;
  }
}

export function writeJson(file: string, value: unknown): void {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, `${JSON.stringify(value, null, 2)}\n`);
}

/** Every `.cc`/`.h` under `source/`, in sorted order. */
export function sourceFiles(): string[] {
  const out: string[] = [];
  const walk = (dir: string): void => {
    if (!fs.existsSync(dir)) return;
    for (const entry of fs
      .readdirSync(dir, { withFileTypes: true })
      .sort((a, b) => (a.name < b.name ? -1 : 1))) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) walk(full);
      else if (entry.name.endsWith(".cc") || entry.name.endsWith(".h")) out.push(full);
    }
  };
  walk(sourceDir);
  return out;
}
