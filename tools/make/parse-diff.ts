import { spawn, spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import type { CommandModule } from "yargs";
import { buildPreset } from "./build.ts";
import { presetOptions, resolvePreset, type PresetArgs } from "./lib/preset.ts";
import { buildDir, cacheDir, exeSuffix, repoRoot } from "./lib/paths.ts";
import { asanEnv } from "./lib/asan.ts";
import { readDumps, type FileResult, type TreeNode } from "../parse-diff/sexp.ts";
import { normalizeOurs, normalizeTsgo } from "../parse-diff/normalize.ts";
import { compare, type Mismatch } from "../parse-diff/compare.ts";

interface Args extends PresetArgs {
  corpus?: string[];
  limit?: number;
  filter?: string;
  top: number;
  show: number;
  spans: boolean;
  raw: boolean;
  jsx: boolean;
  build: boolean;
  report: boolean;
}

/** The TypeScript checkout tsgo's parser is built from (CLAUDE.md). */
const typescriptRepo = process.env.FASTLINT_TYPESCRIPT_REPO ?? "C:/dev/TypeScript";
const tsgoModule = path.join(typescriptRepo, "tsc");
const defaultCorpus = [
  path.join(tsgoModule, "testdata/tests/cases"),
  path.join(repoRoot, "source/tests/ts_sources"),
];
const workDir = path.join(cacheDir, "parse-diff");
const dumperSource = path.join(repoRoot, "tools/parse-diff/tsgo-dump.go");

export const command: CommandModule<object, Args> = {
  command : "parse-diff",
  describe: "diff the grammar tree against tsgo's parser over a corpus",
  builder: (yargs) =>
    presetOptions(yargs)
      .option("corpus", { type: "string", array: true, describe: "files or directories" })
      .option("limit", { type: "number", describe: "stop after N files" })
      .option("filter", { type: "string", describe: "only paths containing this text" })
      .option("top", {
        type    : "number",
        default : 25,
        describe: "mismatch buckets to print",
      })
      .option("show", { type: "number", default: 3, describe: "examples per bucket" })
      .option("spans", {
        type    : "boolean",
        default : false,
        describe: "also compare byte spans",
      })
      .option("raw", { type: "boolean", default: false, describe: "skip normalization" })
      .option("jsx", { type: "boolean", default: false, describe: "include .tsx files" })
      .option("build", {
        type    : "boolean",
        default : true,
        describe: "build fastlint first",
      })
      .option("report", {
        type    : "boolean",
        default : true,
        describe: "write docs/parser-conformance.md",
      }) as never,
  handler: async (argv) => {
    const preset = resolvePreset(argv);
    if (argv.build) await buildPreset(preset, { target: "fastlint" });
    const fastlint = path.join(buildDir(preset), "bin", `fastlint${exeSuffix}`);
    const dumper = buildDumper();

    const files = collectFiles(argv.corpus ?? defaultCorpus, argv);
    if (files.length === 0) {
      console.error("parse-diff: no files matched");
      process.exitCode = 2;
      return;
    }
    fs.mkdirSync(workDir, { recursive: true });
    const listFile = path.join(workDir, "files.txt");
    fs.writeFileSync(listFile, files.join("\n") + "\n");
    console.log(`parse-diff: ${files.length} files`);

    const started = Date.now();
    const [theirs, ours] = await Promise.all([
      runDumper(dumper, [], files.join("\n") + "\n", {}),
      runDumper(
        fastlint,
        ["dump-tree", "--spans", "--batch", listFile],
        undefined,
        asanEnv(preset)
      ),
    ]);
    const seconds = ((Date.now() - started) / 1000).toFixed(1);

    const summary = diffAll(files, theirs, ours, argv);
    printSummary(summary, argv, seconds);
    fs.writeFileSync(
      path.join(workDir, "mismatches.txt"),
      summary.lines.join("\n") + "\n"
    );
    if (argv.report) writeReport(summary, files.length, argv);
    process.exitCode = summary.failed === 0 ? 0 : 1;
  },
};

// ------------------------------------------------------------------ corpus

function collectFiles(roots: string[], argv: Args): string[] {
  const out: string[] = [];
  const wanted = (file: string) => {
    const ext = path.extname(file);
    if (ext === ".ts" || ext === ".mts" || ext === ".cts") return true;
    return argv.jsx && ext === ".tsx";
  };
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

// ------------------------------------------------------------------ dumpers

/**
 * Builds tsgo-dump.go inside the TypeScript module through a Go overlay, so
 * it can import tsgo's internal packages without a file landing in that
 * checkout. Rebuilds only when the source is newer than the binary.
 */
function buildDumper(): string {
  fs.mkdirSync(workDir, { recursive: true });
  const exe = path.join(workDir, `tsgo-dump${exeSuffix}`);
  const fresh =
    fs.existsSync(exe) && fs.statSync(exe).mtimeMs >= fs.statSync(dumperSource).mtimeMs;
  if (fresh) return exe;
  const overlay = path.join(workDir, "overlay.json");
  const virtual = path.join(tsgoModule, "cmd/fastlint-dump/main.go");
  fs.writeFileSync(overlay, JSON.stringify({ Replace: { [virtual]: dumperSource } }));
  console.log("parse-diff: building tsgo-dump");
  const result = spawnSyncChecked(
    "go",
    ["build", "-overlay", overlay, "-o", exe, "./cmd/fastlint-dump"],
    tsgoModule
  );
  if (result !== 0) throw new Error("go build failed; is C:/dev/TypeScript checked out?");
  return exe;
}

function spawnSyncChecked(cmd: string, args: string[], cwd: string): number {
  const r = spawnSync(cmd, args, { cwd, stdio: "inherit" });
  return r.status ?? 1;
}

function runDumper(
  exe: string,
  args: string[],
  stdin: string | undefined,
  env: NodeJS.ProcessEnv
): Promise<Map<string, FileResult>> {
  return new Promise((resolve, reject) => {
    const child = spawn(exe, args, {
      cwd  : repoRoot,
      env  : { ...process.env, ...env },
      stdio: [stdin === undefined ? "ignore" : "pipe", "pipe", "inherit"],
    });
    const results = new Map<string, FileResult>();
    child.on("error", reject);
    readDumps(child.stdout!, (file) => results.set(file.path, file))
      .then(() => resolve(results))
      .catch(reject);
    if (stdin !== undefined) child.stdin!.end(stdin);
  });
}

// ------------------------------------------------------------------ diffing

interface Bucket {
  signature: string;
  count: number;
  examples: string[];
}

interface Summary {
  passed: number;
  failed: number;
  /** Differing files tsgo itself reports parse errors on (deliberately invalid tests). */
  failedInvalid: number;
  missing: number;
  buckets: Bucket[];
  lines: string[];
}

function diffAll(
  files: string[],
  theirs: Map<string, FileResult>,
  ours: Map<string, FileResult>,
  argv: Args
): Summary {
  const buckets = new Map<string, Bucket>();
  const lines: string[] = [];
  let passed = 0;
  let failed = 0;
  let failedInvalid = 0;
  let missing = 0;
  for (const file of files) {
    const a = theirs.get(file);
    const b = ours.get(file);
    if (!a?.root || !b?.root) {
      missing++;
      lines.push(
        `${file}: no dump (${a?.error ?? "tsgo missing"} / ${b?.error ?? "fastlint missing"})`
      );
      continue;
    }
    const expected = argv.raw ? a.root : normalizeTsgo(a.root);
    const actual = argv.raw ? b.root : normalizeOurs(b.root);
    const found = compare(expected, actual, { spans: argv.spans });
    if (!found) {
      passed++;
      continue;
    }
    failed++;
    if (a.diagnostics) failedInvalid++;
    const where = `${file}:${found.offset}`;
    const tag = a.diagnostics ? " [invalid input]" : "";
    lines.push(
      `${where}: ${found.signature} — ${found.detail} (in ${found.path.join("/")})${tag}`
    );
    let bucket = buckets.get(found.signature);
    if (!bucket) {
      bucket = { signature: found.signature, count: 0, examples: [] };
      buckets.set(found.signature, bucket);
    }
    bucket.count++;
    if (bucket.examples.length < argv.show)
      bucket.examples.push(`${where} ${found.detail}`);
  }
  const sorted = [...buckets.values()].sort((x, y) => y.count - x.count);
  return { passed, failed, failedInvalid, missing, buckets: sorted, lines };
}

function printSummary(summary: Summary, argv: Args, seconds: string): void {
  const total = summary.passed + summary.failed + summary.missing;
  const rate = total === 0 ? 0 : (100 * summary.passed) / total;
  console.log(
    `parse-diff: ${summary.passed}/${total} match (${rate.toFixed(2)}%), ` +
      `${summary.failed} differ (${summary.failed - summary.failedInvalid} on files tsgo ` +
      `parses cleanly), ${summary.missing} without dumps, ${seconds}s`
  );
  for (const bucket of summary.buckets.slice(0, argv.top)) {
    console.log(`${String(bucket.count).padStart(6)}  ${bucket.signature}`);
    for (const example of bucket.examples) console.log(`          ${example}`);
  }
  console.log(`full list: ${path.join(workDir, "mismatches.txt")}`);
}

function writeReport(summary: Summary, total: number, argv: Args): void {
  const rate = total === 0 ? 0 : (100 * summary.passed) / total;
  const date = new Date().toISOString().slice(0, 10);
  const out: string[] = [
    "# Parser conformance",
    "",
    "Pass rate of `node make.ts parse-diff` (docs/tasklists/MASTER.md 3.4): the",
    "grammar tree against tsgo's parser over the TypeScript conformance and",
    "compiler test cases plus our own fixtures, after the normalization in",
    "tools/parse-diff/normalize.ts. Regenerated by the command; edit the",
    "normalizer, not this file.",
    "",
    `- Date: ${date}`,
    `- Files: ${total}${argv.jsx ? " (including .tsx)" : " (.ts only; .tsx excluded)"}`,
    `- Match: ${summary.passed} (${rate.toFixed(2)}%)`,
    `- Differ: ${summary.failed}, of which ${summary.failed - summary.failedInvalid} are files tsgo parses without diagnostics; the rest are deliberately invalid inputs where only error recovery differs`,
    `- No dump: ${summary.missing}`,
    `- Spans compared: ${argv.spans ? "yes" : "no"}`,
    "",
    "## Top mismatch buckets",
    "",
    "Each file counts once, at its first disagreement. `A > B != C` means a",
    "child of A where tsgo has B and we have C.",
    "",
  ];
  // Columns are padded the way prettier formats a table, so the file stays
  // clean under `make.ts format --check`.
  const rows = summary.buckets
    .slice(0, argv.top)
    .map((b) => [String(b.count), `\`${b.signature}\``]);
  const header = ["Count", "First disagreement"];
  const widths = header.map((h, i) =>
    Math.max(h.length, ...rows.map((r) => r[i].length))
  );
  const line = (cells: string[]) =>
    `| ${cells.map((c, i) => c.padEnd(widths[i])).join(" | ")} |`;
  out.push(line(header), line(widths.map((w) => "-".repeat(w))));
  for (const row of rows) out.push(line(row));
  out.push("");
  fs.writeFileSync(path.join(repoRoot, "docs/parser-conformance.md"), out.join("\n"));
}

export type { TreeNode, Mismatch };
