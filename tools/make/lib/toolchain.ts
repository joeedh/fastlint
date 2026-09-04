import fs from "node:fs";
import path from "node:path";
import { cacheDir, readJson, writeJson } from "./paths.ts";
import { capture } from "./spawn.ts";
import { fail, warn } from "./log.ts";

/**
 * The located build tools. The Visual Studio fields are set only on Windows;
 * elsewhere the tools come off PATH and `clangFormat` may be missing entirely,
 * which only `format` treats as an error.
 */
export interface Toolchain {
  vsInstallPath?: string;
  vsVersion?: string;
  vsDisplayName?: string;
  cmake: string;
  ctest: string;
  ninja: string;
  clangFormat: string | undefined;
  clangCl: string | undefined;
  vcvarsall?: string;
  node: string;
  probedAt: string;
}

export const isWindows = process.platform === "win32";

const envFile = path.join(cacheDir, "env.json");

const vswhereCandidates = [
  "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe",
  "C:/Program Files/Microsoft Visual Studio/Installer/vswhere.exe",
];

interface VsInstance {
  installationPath: string;
  installationVersion: string;
  displayName: string;
}

function findVs(): VsInstance {
  const vswhere = vswhereCandidates.find((p) => fs.existsSync(p));
  if (!vswhere)
    fail(
      "vswhere.exe not found; install Visual Studio or set the paths in .cache/env.json"
    );
  const out = capture(vswhere, [
    "-latest",
    "-prerelease",
    "-products",
    "*",
    "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
    "-format",
    "json",
  ]);
  const instances = out ? (JSON.parse(out) as VsInstance[]) : [];
  const instance = instances[0];
  if (!instance) fail("vswhere found no Visual Studio install with the C++ toolset");
  return instance;
}

/** The first candidate under `root` that exists, or undefined. */
function firstExisting(root: string, candidates: string[]): string | undefined {
  for (const rel of candidates) {
    const full = path.join(root, rel);
    if (fs.existsSync(full)) return full;
  }
  return undefined;
}

function probe(): Toolchain {
  const vs = findVs();
  const root = vs.installationPath;
  const cmakeBin = "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin";
  const ninjaBin = "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja";
  const llvmBin = "VC/Tools/Llvm/x64/bin";

  const required = (name: string, found: string | undefined): string => {
    if (!found) fail(`${name} not found under ${root}`);
    return found;
  };

  return {
    vsInstallPath: root,
    vsVersion    : vs.installationVersion,
    vsDisplayName: vs.displayName,
    cmake        : required("cmake.exe", firstExisting(root, [`${cmakeBin}/cmake.exe`])),
    ctest        : required("ctest.exe", firstExisting(root, [`${cmakeBin}/ctest.exe`])),
    ninja        : required("ninja.exe", firstExisting(root, [`${ninjaBin}/ninja.exe`])),
    clangFormat: required(
      "clang-format.exe",
      firstExisting(root, [`${llvmBin}/clang-format.exe`])
    ),
    clangCl      : firstExisting(root, [`${llvmBin}/clang-cl.exe`]),
    vcvarsall: required(
      "vcvarsall.bat",
      firstExisting(root, ["VC/Auxiliary/Build/vcvarsall.bat"])
    ),
    node         : process.execPath,
    probedAt     : new Date().toISOString(),
  };
}

/** The first name that resolves to an executable on PATH, or undefined. */
function onPath(names: string[]): string | undefined {
  const dirs = (process.env["PATH"] ?? "").split(path.delimiter).filter(Boolean);
  for (const name of names) {
    for (const dir of dirs) {
      const full = path.join(dir, name);
      if (fs.existsSync(full)) return full;
    }
  }
  return undefined;
}

/**
 * The lowest clang-format that lays the sources out the way they are committed.
 * 19 and older expand a requires-expression across three lines and break before
 * the brace of a wrapped `if`, so it would rewrite files the Windows toolchain
 * considers formatted.
 */
export const minClangFormatMajor = 20;

/** The major version `clang-format --version` reports, or 0 if it will not run. */
export function clangFormatMajor(exe: string): number {
  const out = capture(exe, ["--version"]);
  const match = out ? /version (\d+)/.exec(out) : undefined;
  return match?.[1] ? Number(match[1]) : 0;
}

/**
 * The newest clang-format on PATH. Distributions ship it under a version suffix
 * as often as not, and the unsuffixed name often points at an older major than
 * the suffixed ones installed alongside it.
 */
function findClangFormat(): string | undefined {
  const names = [
    "clang-format",
    ...Array.from({ length: 12 }, (_, i) => `clang-format-${30 - i}`),
  ];
  let best: string | undefined;
  let bestMajor = 0;
  for (const name of names) {
    const exe = onPath([name]);
    if (!exe) continue;
    const major = clangFormatMajor(exe);
    if (major > bestMajor) {
      best = exe;
      bestMajor = major;
    }
  }
  return best;
}

function probePosix(): Toolchain {
  const required = (name: string): string => {
    const found = onPath([name]);
    if (!found) fail(`${name} not found on PATH`);
    return found;
  };

  return {
    cmake      : required("cmake"),
    ctest      : required("ctest"),
    ninja      : required("ninja"),
    clangFormat: findClangFormat(),
    clangCl    : undefined,
    node       : process.execPath,
    probedAt   : new Date().toISOString(),
  };
}

/**
 * The cached toolchain, re-probed when the cache is missing, stale against the
 * installed tools, or `refresh` is set.
 */
export function toolchain(refresh = false): Toolchain {
  const cached = refresh ? undefined : readJson<Toolchain>(envFile);
  const stillThere = (tool: string | undefined): boolean =>
    tool === undefined || fs.existsSync(tool);
  if (cached && fs.existsSync(cached.cmake) && stillThere(cached.vcvarsall)) {
    return cached;
  }
  if (cached) warn("cached toolchain is stale, re-probing");
  const found = isWindows ? probe() : probePosix();
  writeJson(envFile, found);
  return found;
}

// -------------------------------------------------------------- vcvars

const vcvarsFile = (arch: string) => path.join(cacheDir, `vcvars-${arch}.json`);

export interface VcvarsCache {
  vsVersion: string;
  arch: string;
  /** Only the variables vcvarsall added or changed. */
  changed: Record<string, string>;
  capturedAt: string;
}

/**
 * Runs vcvarsall.bat and keeps the variables it changed. Storing the delta
 * rather than the whole environment keeps a stale cache from pinning things
 * like the user's PATH entries or TEMP.
 */
export function captureVcvars(arch = "x64", refresh = false): VcvarsCache {
  if (!isWindows) fail("vcvarsall is Windows-only");
  const tools = toolchain();
  const file = vcvarsFile(arch);
  const cached = refresh ? undefined : readJson<VcvarsCache>(file);
  if (cached && cached.vsVersion === tools.vsVersion) return cached;

  const comspec = process.env["COMSPEC"] ?? "cmd.exe";
  const before = capture(comspec, ["/d", "/c", "set"]);

  // cmd mangles a quoted path passed as one /c argument, so the call goes
  // through a script file instead.
  const script = path.join(cacheDir, `vcvars-${arch}.bat`);
  fs.mkdirSync(cacheDir, { recursive: true });
  fs.writeFileSync(
    script,
    `@echo off\r\ncall "${tools.vcvarsall}" ${arch} >nul\r\nset\r\n`
  );
  const after = capture(comspec, ["/d", "/c", script]);
  fs.rmSync(script, { force: true });

  if (!before || !after) fail(`could not run ${tools.vcvarsall} ${arch}`);

  const parse = (text: string): Map<string, string> => {
    const map = new Map<string, string>();
    for (const line of text.split(/\r?\n/)) {
      const eq = line.indexOf("=");
      if (eq > 0) map.set(line.slice(0, eq).toUpperCase(), line.slice(eq + 1));
    }
    return map;
  };

  const base = parse(before);
  const changed: Record<string, string> = {};
  for (const [key, value] of parse(after)) {
    if (base.get(key) !== value) changed[key] = value;
  }

  const result: VcvarsCache = {
    vsVersion: tools.vsVersion ?? "",
    arch,
    changed,
    capturedAt: new Date().toISOString(),
  };
  writeJson(file, result);
  return result;
}

/** The process environment with the MSVC variables layered on top. */
export function buildEnv(arch = "x64"): NodeJS.ProcessEnv {
  if (!isWindows) return process.env;
  const vcvars = captureVcvars(arch);
  const tools = toolchain();
  const env: NodeJS.ProcessEnv = { ...process.env, ...vcvars.changed };
  const extraPath = [path.dirname(tools.cmake), path.dirname(tools.ninja)];
  env["PATH"] = [...extraPath, env["PATH"] ?? ""].join(path.delimiter);
  return env;
}
