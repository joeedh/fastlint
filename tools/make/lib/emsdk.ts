import fs from "node:fs";
import path from "node:path";
import { cacheDir, readJson, repoRoot, vendorDir, writeJson } from "./paths.ts";
import { capture, run } from "./spawn.ts";
import { fail, step, warn } from "./log.ts";

/**
 * The pinned Emscripten SDK. `version` is what `emsdk install` resolves;
 * `commit` pins the emsdk repository itself, so the installer scripts are the
 * same ones this pin was tested against.
 */
export const emsdk = {
  version: "6.0.9",
  commit : "5eb0bde7585670252e8ba05e9d361627bffd08b5",
  repo   : "https://github.com/emscripten-core/emsdk.git",
};

/**
 * Where the SDK is installed. FASTLINT_EMSDK_DIR lets a worktree borrow the
 * main checkout's install rather than repeating the download; `activate` writes
 * absolute paths, so the borrowed tree resolves emcc back to wherever it lives.
 */
export function emsdkDir(): string {
  const override = process.env["FASTLINT_EMSDK_DIR"];
  if (override) return path.resolve(override);
  return path.join(vendorDir, "emsdk");
}

export function emsdkInstalled(): boolean {
  return fs.existsSync(path.join(emsdkDir(), "emsdk.py"));
}

/** The python emsdk's own scripts run under, or undefined when none is found. */
export function findPython(): string | undefined {
  for (const name of ["python3", "python"]) {
    // The Windows Store stub answers --version by opening the Store, so the
    // probe asks for something it has to actually execute python to print.
    const out = capture(name, ["-c", "import sys; print(sys.version_info[0])"]);
    if (out && out.trim() === "3") return name;
  }
  return undefined;
}

function requirePython(): string {
  const python = findPython();
  if (!python) fail("python 3 not found on PATH; emsdk's installer needs it");
  return python;
}

// ------------------------------------------------------------------ install

/** Clones the pinned emsdk and installs and activates the pinned SDK. */
export async function installEmsdk(force = false): Promise<void> {
  const dir = emsdkDir();
  if (process.env["FASTLINT_EMSDK_DIR"] && !fs.existsSync(dir)) {
    fail(`FASTLINT_EMSDK_DIR does not exist: ${dir}`);
  }
  const python = requirePython();

  if (!fs.existsSync(path.join(dir, ".git"))) {
    step(`clone ${emsdk.repo} -> ${path.relative(repoRoot, dir)}`);
    fs.mkdirSync(path.dirname(dir), { recursive: true });
    await run("git", ["clone", "--quiet", emsdk.repo, dir], { cwd: repoRoot });
  }
  step(`checkout emsdk @ ${emsdk.commit.slice(0, 10)}`);
  await run("git", ["fetch", "--quiet", "origin", emsdk.commit], {
    cwd         : dir,
    allowFailure: true,
  });
  await run("git", ["checkout", "--quiet", "--detach", emsdk.commit], { cwd: dir });

  // The SDK itself is a separate, much larger download, so a second run skips
  // it unless the pin moved or --force was given.
  const stamp = path.join(dir, ".fastlint-version");
  const current = fs.existsSync(stamp) ? fs.readFileSync(stamp, "utf8").trim() : "";
  if (!force && current === emsdk.version && readEmsdkEnvFile(dir)) {
    step(`emsdk ${emsdk.version} is already installed`);
  } else {
    step(`emsdk install ${emsdk.version} (this downloads over a gigabyte)`);
    await run(python, ["emsdk.py", "install", emsdk.version], { cwd: dir });
    step(`emsdk activate ${emsdk.version}`);
    // No --permanent: the environment is captured below instead of being
    // written into the user's registry or shell profile.
    await run(python, ["emsdk.py", "activate", emsdk.version], { cwd: dir });
    fs.writeFileSync(stamp, `${emsdk.version}\n`);
  }
  emsdkEnv(true);
}

// -------------------------------------------------------------------- env

export interface EmsdkEnv {
  version: string;
  dir: string;
  /** Everything construct_env sets except PATH. */
  vars: Record<string, string>;
  /** The directories construct_env prepends to PATH, in order. */
  pathAdditions: string[];
  capturedAt: string;
}

const emsdkEnvFile = path.join(cacheDir, "emsdk.json");

/** The env script `emsdk activate` leaves behind, or undefined. */
function readEmsdkEnvFile(dir: string): string | undefined {
  for (const name of ["emsdk_set_env.bat", "emsdk_set_env.sh"]) {
    const file = path.join(dir, name);
    if (fs.existsSync(file)) return fs.readFileSync(file, "utf8");
  }
  return undefined;
}

/**
 * Turns construct_env's output into name/value pairs. The Windows script writes
 * `SET NAME=value` and the POSIX one `export NAME="value"`, and both forms turn
 * up on stdout as well, so one parser handles all of them.
 */
function parseEnvScript(text: string): Map<string, string> {
  const out = new Map<string, string>();
  for (const raw of text.replace(/\r/g, "").split("\n")) {
    let line = raw.trim();
    if (!line || line.startsWith("#") || line.startsWith("::")) continue;
    if (/^set /i.test(line)) line = line.slice(4).trim();
    else if (line.startsWith("export ")) line = line.slice(7).trim();
    else if (line.startsWith("$env:")) line = line.slice(5).trim();
    else if (line.startsWith("setenv ")) line = line.slice(7).trim();
    if (line.endsWith(";")) line = line.slice(0, -1).trim();
    const eq = line.indexOf("=");
    if (eq <= 0) continue;
    const name = line.slice(0, eq).trim();
    let value = line.slice(eq + 1).trim();
    if (/^"(.*)"$/s.test(value) || /^'(.*)'$/s.test(value)) value = value.slice(1, -1);
    if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) out.set(name, value);
  }
  return out;
}

const samePath = (a: string, b: string): boolean =>
  process.platform === "win32"
    ? a.replace(/[\\/]+$/, "").toLowerCase() === b.replace(/[\\/]+$/, "").toLowerCase()
    : a.replace(/\/+$/, "") === b.replace(/\/+$/, "");

/**
 * Captures the emsdk environment as a delta, the way `captureVcvars` does, so a
 * cache written months ago cannot pin today's PATH or TEMP.
 *
 * construct_env emits PATH whole (its own directories followed by the PATH it
 * inherited), so only the leading entries are kept. The SDK's bundled Node is
 * dropped from them: emcc finds it through EMSDK_NODE, and leaving it on PATH
 * would shadow the Node this repository's tooling runs under.
 */
export function emsdkEnv(refresh = false): EmsdkEnv {
  const dir = emsdkDir();
  const cached = refresh ? undefined : readJson<EmsdkEnv>(emsdkEnvFile);
  if (cached && cached.version === emsdk.version && samePath(cached.dir, dir)) {
    return cached;
  }
  if (!emsdkInstalled()) {
    fail(`emsdk not installed at ${dir}; run: node make.ts deps fetch emsdk`);
  }
  const python = requirePython();

  // EMSDK_QUIET silences the "Setting up EMSDK environment" banners, which
  // otherwise land in the same stream as the assignments on POSIX.
  const childEnv: NodeJS.ProcessEnv = { ...process.env, EMSDK_QUIET: "1" };
  // A Git-Bash MSYSTEM makes emsdk hand back cygwin-shaped paths cmake cannot
  // use, so the child is run as if from a plain Windows shell.
  delete childEnv["MSYSTEM"];
  const stdout = capture(python, ["emsdk.py", "construct_env"], {
    cwd: dir,
    env: childEnv,
  });
  if (stdout === undefined) fail(`\`${python} emsdk.py construct_env\` failed in ${dir}`);

  const parsed = parseEnvScript(readEmsdkEnvFile(dir) ?? stdout);
  const emitted = parsed.get("PATH") ?? "";
  parsed.delete("PATH");
  // construct_env echoes EMSDK_QUIET back; it belongs to this probe, not to the
  // environment a build should run under.
  parsed.delete("EMSDK_QUIET");

  const inherited = (process.env["PATH"] ?? "").split(path.delimiter).filter(Boolean);
  const pathAdditions: string[] = [];
  for (const entry of emitted.split(path.delimiter)) {
    if (!entry) continue;
    if (inherited.some((have) => samePath(have, entry))) continue;
    // Drops the SDK's bundled node, which would otherwise shadow the tooling's
    if (/[\\/]node[\\/]/i.test(entry)) continue;
    pathAdditions.push(entry);
  }
  if (pathAdditions.length === 0) {
    warn("emsdk construct_env added nothing to PATH; is the SDK activated?");
  }

  const result: EmsdkEnv = {
    version: emsdk.version,
    dir,
    vars: Object.fromEntries(parsed),
    pathAdditions,
    capturedAt: new Date().toISOString(),
  };
  writeJson(emsdkEnvFile, result);
  return result;
}

/** The Emscripten CMake toolchain file inside the installed SDK. */
export function emscriptenToolchain(): string {
  const root = emsdkEnv().vars["EMSDK"] ?? emsdkDir();
  const file = path.join(
    root,
    "upstream",
    "emscripten",
    "cmake",
    "Modules",
    "Platform",
    "Emscripten.cmake"
  );
  if (!fs.existsSync(file)) fail(`Emscripten toolchain file not found at ${file}`);
  return file.replace(/\\/g, "/");
}

/** `emcc` as resolved through the captured environment, for `make.ts env`. */
export function emccPath(refresh = false): string | undefined {
  // Recent SDKs ship a native emcc.exe on Windows and a .bat only on older
  // ones, so both names are tried.
  const names = process.platform === "win32" ? ["emcc.exe", "emcc.bat"] : ["emcc"];
  for (const dir of emsdkEnv(refresh).pathAdditions) {
    for (const name of names) {
      const candidate = path.join(dir, name);
      if (fs.existsSync(candidate)) return candidate;
    }
  }
  return undefined;
}
