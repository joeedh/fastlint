import { spawn, spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import type { CommandModule } from "yargs";
import { buildPreset } from "./build.ts";
import { presetOptions, resolvePreset, type PresetArgs } from "./lib/preset.ts";
import { buildDir, exeSuffix, repoRoot } from "./lib/paths.ts";
import { asanEnv, isAsan } from "./lib/asan.ts";
import { warn } from "./lib/log.ts";

// Drives `fastlint fuzz` over a corpus in batches. The binary prints
// `# <file> <seed>` before each mutated parse, so a sanitizer abort, an
// invariant failure or a hang is pinned to one seed; that seed is replayed to
// a file under build/<preset>/fuzz-failures/ and shrunk by delta debugging
// while the failure (same exit class) still reproduces.

interface Args extends PresetArgs {
  corpus?: string[];
  limit?: number;
  filter?: string;
  iterations: number;
  seed: number;
  batch: number;
  timeout: number;
  minimize: boolean;
  build: boolean;
}

const typescriptRepo = process.env.FASTLINT_TYPESCRIPT_REPO ?? "C:/dev/TypeScript";
const defaultCorpus = [
  path.join(typescriptRepo, "tsc/testdata/tests/cases"),
  path.join(repoRoot, "source/tests/ts_sources"),
];

export const command: CommandModule<object, Args> = {
  command : "fuzz",
  describe: "mutate corpus files at token level and parse them under ASAN",
  builder: (yargs) =>
    presetOptions(yargs)
      .option("preset", { default: "asan" })
      .option("corpus", {
        type    : "array",
        string  : true,
        describe: "files or directories (default: the tsgo test corpus)",
      })
      .option("limit", { type: "number", describe: "only the first N files" })
      .option("filter", { type: "string", describe: "only paths containing this" })
      .option("iterations", {
        type    : "number",
        default : 50,
        describe: "mutated parses per file",
      })
      .option("seed", { type: "number", default: 1, describe: "base seed" })
      .option("batch", {
        type    : "number",
        default : 50,
        describe: "files per fastlint process",
      })
      .option("timeout", {
        type    : "number",
        default : 120,
        describe: "seconds a batch may run before it counts as a hang",
      })
      .option("minimize", {
        type    : "boolean",
        default : true,
        describe: "shrink each failing input",
      })
      .option("build", {
        type    : "boolean",
        default : true,
        describe: "build first",
      }) as never,
  handler: async (argv) => {
    const preset = resolvePreset(argv);
    if (!isAsan(preset))
      warn(`preset ${preset} has no sanitizer; memory errors go unnoticed`);
    if (argv.build) await buildPreset(preset, { target: "fastlint" });
    const exe = path.join(buildDir(preset), "bin", `fastlint${exeSuffix}`);
    const env = asanEnv(preset);
    if (isAsan(preset)) {
      // Freed mutants would otherwise sit in the quarantine (256 MB by
      // default) and the allocator's size-class caches, so a long batch grows
      // to a gigabyte for no diagnostic gain.
      env["ASAN_OPTIONS"] =
        `${env["ASAN_OPTIONS"] ?? ""}:quarantine_size_mb=16:malloc_context_size=8`;
    }
    const failDir = path.join(buildDir(preset), "fuzz-failures");
    fs.mkdirSync(failDir, { recursive: true });

    const files = collectFiles(argv.corpus ?? defaultCorpus, argv);
    if (files.length === 0) {
      console.error("fuzz: no files matched");
      process.exitCode = 2;
      return;
    }
    console.log(
      `fuzz: ${files.length} files x ${argv.iterations} iterations, seed ${argv.seed}, preset ${preset}`
    );

    const started = Date.now();
    const failures: Failure[] = [];
    let cases = 0;
    let index = 0;
    while (index < files.length) {
      const batch = files.slice(index, index + argv.batch);
      const result = await runBatch(exe, env, batch, argv);
      cases += result.cases;
      if (result.failure === undefined) {
        index += batch.length;
        continue;
      }
      const failure = result.failure;
      failures.push(failure);
      const n = failures.length;
      const written = replay(
        exe,
        env,
        failure,
        path.join(failDir, `${n}-${path.basename(failure.file)}`)
      );
      failure.input = written;
      console.log(
        `fuzz: ${failure.kind} on ${failure.file} seed ${failure.seed}` +
          (written ? `\n  input: ${written}` : "")
      );
      if (written && argv.minimize) {
        const minimized = await minimize(exe, env, failure, argv);
        if (minimized) console.log(`  minimized: ${minimized}`);
      }
      fs.writeFileSync(
        path.join(failDir, `${n}.txt`),
        [
          `${failure.kind} on ${failure.file} seed ${failure.seed}`,
          `replay: fastlint fuzz --replay ${failure.seed} --out <path> ${failure.file}`,
          "",
          failure.report,
        ].join("\n")
      );
      // Resume after the failing file; the rest of its iterations are skipped.
      index += batch.indexOf(failure.file) + 1;
    }

    const seconds = ((Date.now() - started) / 1000).toFixed(1);
    console.log(`fuzz: ${cases} cases, ${failures.length} failures, ${seconds}s`);
    if (failures.length) process.exitCode = 1;
  },
};

// ------------------------------------------------------------------ corpus

function collectFiles(roots: string[], argv: Args): string[] {
  const out: string[] = [];
  const wanted = (file: string) => /\.(ts|tsx|mts|cts)$/.test(file);
  const visit = (p: string) => {
    const stat = fs.statSync(p, { throwIfNoEntry: false });
    if (!stat) return;
    if (stat.isFile()) {
      if (wanted(p)) out.push(p.replace(/\\/g, "/"));
      return;
    }
    for (const entry of fs
      .readdirSync(p, { withFileTypes: true })
      .sort((a, b) => a.name.localeCompare(b.name))) {
      if (entry.name === "node_modules" || entry.name.startsWith(".")) continue;
      visit(path.join(p, entry.name));
    }
  };
  for (const root of roots) visit(root);
  let files = out;
  if (argv.filter) files = files.filter((f) => f.includes(argv.filter!));
  if (argv.limit) files = files.slice(0, argv.limit);
  return files;
}

// ----------------------------------------------------------------- running

interface Failure {
  kind: "crash" | "hang" | "invariant";
  file: string;
  seed: string;
  /** The sanitizer or invariant report, without the `# ` progress lines. */
  report: string;
  input?: string;
}

interface Outcome {
  code: number | null;
  timedOut: boolean;
  stderr: string;
}

function runProcess(
  exe: string,
  env: NodeJS.ProcessEnv,
  args: string[],
  timeoutSeconds: number,
  onLine?: (line: string) => void
): Promise<Outcome> {
  return new Promise((resolve) => {
    const child = spawn(exe, args, {
      cwd: repoRoot,
      env,
      stdio: ["ignore", "ignore", "pipe"],
    });
    let stderr = "";
    let pending = "";
    let timedOut = false;
    const timer = setTimeout(() => {
      timedOut = true;
      child.kill();
    }, timeoutSeconds * 1000);
    child.stderr!.setEncoding("utf8");
    child.stderr!.on("data", (chunk: string) => {
      pending += chunk;
      let nl: number;
      while ((nl = pending.indexOf("\n")) >= 0) {
        const line = pending.slice(0, nl).replace(/\r$/, "");
        pending = pending.slice(nl + 1);
        if (onLine) onLine(line);
        if (!line.startsWith("# ")) stderr += `${line}\n`;
      }
    });
    child.once("error", () => {
      clearTimeout(timer);
      resolve({ code: null, timedOut, stderr: `${stderr}(failed to start ${exe})` });
    });
    child.once("close", (code) => {
      clearTimeout(timer);
      if (pending && !pending.startsWith("# ")) stderr += pending;
      resolve({ code, timedOut, stderr });
    });
  });
}

async function runBatch(
  exe: string,
  env: NodeJS.ProcessEnv,
  files: string[],
  argv: Args
): Promise<{ cases: number; failure?: Failure }> {
  let last: { file: string; seed: string } | undefined;
  let cases = 0;
  const outcome = await runProcess(
    exe,
    env,
    [
      "fuzz",
      "--iterations",
      String(argv.iterations),
      "--seed",
      String(argv.seed),
      ...files,
    ],
    argv.timeout + files.length * argv.iterations * 0.05,
    (line) => {
      if (!line.startsWith("# ")) return;
      const at = line.lastIndexOf(" ");
      last = { file: line.slice(2, at), seed: line.slice(at + 1) };
      cases++;
    }
  );
  if (outcome.code === 0 && !outcome.timedOut) return { cases };
  if (!last) {
    console.error(`fuzz: batch failed before its first case:\n${outcome.stderr}`);
    process.exitCode = 2;
    return { cases };
  }
  const kind = outcome.timedOut
    ? "hang"
    : outcome.stderr.includes("invariant failed")
      ? "invariant"
      : "crash";
  return {
    cases,
    failure: { kind, file: last.file, seed: last.seed, report: outcome.stderr },
  };
}

function replay(
  exe: string,
  env: NodeJS.ProcessEnv,
  failure: Failure,
  out: string
): string | undefined {
  const result = spawnSyncQuiet(exe, env, [
    "fuzz",
    "--replay",
    failure.seed,
    "--out",
    out,
    failure.file,
  ]);
  if (result !== 0 || !fs.existsSync(out)) {
    warn(`fuzz: could not replay seed ${failure.seed} for ${failure.file}`);
    return undefined;
  }
  return out;
}

function spawnSyncQuiet(exe: string, env: NodeJS.ProcessEnv, args: string[]): number {
  const result = spawnSync(exe, args, {
    cwd: repoRoot,
    env,
    stdio: ["ignore", "ignore", "pipe"],
  });
  return result.status ?? 1;
}

// -------------------------------------------------------------- minimizing

/** Runs `fuzz --check` on one input and says whether it fails the same way. */
async function reproduces(
  exe: string,
  env: NodeJS.ProcessEnv,
  input: string,
  kind: Failure["kind"],
  timeoutSeconds: number
): Promise<boolean> {
  const outcome = await runProcess(exe, env, ["fuzz", "--check", input], timeoutSeconds);
  if (kind === "hang") return outcome.timedOut;
  if (outcome.timedOut || outcome.code === 0) return false;
  const invariant = outcome.stderr.includes("invariant failed");
  return kind === "invariant" ? invariant : !invariant;
}

/**
 * Delta debugging over lines, then over bytes, writing the smallest input
 * that still fails next to the original as `<name>.min.<ext>`.
 */
async function minimize(
  exe: string,
  env: NodeJS.ProcessEnv,
  failure: Failure,
  argv: Args
): Promise<string | undefined> {
  const input = failure.input!;
  const ext = path.extname(input);
  const probe = input.replace(ext, `.probe${ext}`);
  const out = input.replace(ext, `.min${ext}`);
  const timeout = failure.kind === "hang" ? 5 : Math.min(argv.timeout, 30);
  const still = async (text: string) => {
    fs.writeFileSync(probe, text, "latin1");
    return reproduces(exe, env, probe, failure.kind, timeout);
  };
  // latin1 round-trips every byte; utf8 would rewrite invalid sequences.
  let text = fs.readFileSync(input, "latin1");
  if (!(await still(text))) {
    fs.rmSync(probe, { force: true });
    warn("  failure does not reproduce from the replayed file; skipping minimization");
    return undefined;
  }
  text = await ddmin(text.split("\n"), (parts) => still(parts.join("\n"))).then((p) =>
    p.join("\n")
  );
  text = await ddmin([...text], (parts) => still(parts.join(""))).then((p) => p.join(""));
  fs.writeFileSync(out, text, "latin1");
  fs.rmSync(probe, { force: true });
  return out;
}

/** Zeller's ddmin: removes chunks while the predicate holds, halving granularity. */
async function ddmin<T>(
  items: T[],
  holds: (subset: T[]) => Promise<boolean>
): Promise<T[]> {
  let current = items;
  let n = 2;
  let budget = 400;
  while (current.length >= 2 && budget > 0) {
    const size = Math.ceil(current.length / n);
    let reduced = false;
    for (let start = 0; start < current.length && budget > 0; start += size) {
      const complement = [...current.slice(0, start), ...current.slice(start + size)];
      budget--;
      if (await holds(complement)) {
        current = complement;
        n = Math.max(n - 1, 2);
        reduced = true;
        break;
      }
    }
    if (reduced) continue;
    if (n >= current.length) break;
    n = Math.min(n * 2, current.length);
  }
  return current;
}
