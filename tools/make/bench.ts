import fs from "node:fs";
import path from "node:path";
import type { CommandModule } from "yargs";
import { buildPreset } from "./build.ts";
import { presetOptions, resolvePreset, type PresetArgs } from "./lib/preset.ts";
import {
  buildDir,
  cacheDir,
  exeSuffix,
  readJson,
  repoRoot,
  writeJson,
} from "./lib/paths.ts";
import { run } from "./lib/spawn.ts";

// Parse throughput on a corpus, via `fastlint bench` (I/O excluded, best of
// `--repeat`). Results are JSON under .cache/bench/; `--save <name>` keeps one
// as a baseline and `--compare <name>` prints the change against it.

interface Args extends PresetArgs {
  corpus?: string[];
  repeat: number;
  save?: string;
  compare?: string;
  build: boolean;
}

interface Result {
  files: number;
  bytes: number;
  tokens: number;
  nodes: number;
  diagnostics: number;
  repeat: number;
  bestSeconds: number;
  meanSeconds: number;
  mbPerSecond: number;
}

interface Saved extends Result {
  preset: string;
  corpus: string[];
  date: string;
}

const typescriptRepo = process.env.FASTLINT_TYPESCRIPT_REPO ?? "C:/dev/TypeScript";
const defaultCorpus = [path.join(typescriptRepo, "tsc/testdata/tests/cases")];
const benchDir = path.join(cacheDir, "bench");

export const command: CommandModule<object, Args> = {
  command : "bench",
  describe: "measure parse throughput (MB/s) on a corpus",
  builder: (yargs) =>
    presetOptions(yargs)
      .option("preset", { default: "release" })
      .option("corpus", {
        type    : "array",
        string  : true,
        describe: "files or directories (default: the tsgo test corpus)",
      })
      .option("repeat", {
        type    : "number",
        default : 5,
        describe: "passes; the best counts",
      })
      .option("save", { type: "string", describe: "store the result as this baseline" })
      .option("compare", { type: "string", describe: "compare against a saved baseline" })
      .option("build", {
        type    : "boolean",
        default : true,
        describe: "build first",
      }) as never,
  handler: async (argv) => {
    const preset = resolvePreset(argv);
    if (argv.build) await buildPreset(preset, { target: "fastlint" });
    const exe = path.join(buildDir(preset), "bin", `fastlint${exeSuffix}`);
    const corpus = (argv.corpus ?? defaultCorpus).map((c) => path.resolve(c));

    const { stdout } = await run(
      exe,
      ["bench", "--json", "--repeat", String(argv.repeat), ...corpus],
      { cwd: repoRoot, capture: true, quiet: true }
    );
    const result = JSON.parse(stdout.trim()) as Result;
    const mb = result.bytes / (1024 * 1024);
    console.log(
      `bench (${preset}): ${result.files} files, ${mb.toFixed(2)} MB, ` +
        `${result.tokens} tokens, ${result.nodes} nodes`
    );
    console.log(
      `  best of ${result.repeat}: ${(result.bestSeconds * 1000).toFixed(1)} ms, ` +
        `${result.mbPerSecond.toFixed(1)} MB/s (mean ${(result.meanSeconds * 1000).toFixed(1)} ms)`
    );

    const saved: Saved = { ...result, preset, corpus, date: new Date().toISOString() };
    fs.mkdirSync(benchDir, { recursive: true });
    writeJson(path.join(benchDir, "last.json"), saved);
    if (argv.save) writeJson(path.join(benchDir, `${argv.save}.json`), saved);

    if (argv.compare) {
      const baseline = readJson<Saved>(path.join(benchDir, `${argv.compare}.json`));
      if (!baseline) {
        console.error(`bench: no baseline named ${argv.compare} under ${benchDir}`);
        process.exitCode = 2;
        return;
      }
      const delta = (result.mbPerSecond / baseline.mbPerSecond - 1) * 100;
      const sign = delta >= 0 ? "+" : "";
      console.log(
        `  vs ${argv.compare} (${baseline.date.slice(0, 10)}, ${baseline.preset}): ` +
          `${baseline.mbPerSecond.toFixed(1)} MB/s -> ${result.mbPerSecond.toFixed(1)} MB/s ` +
          `(${sign}${delta.toFixed(1)}%)`
      );
      if (baseline.files !== result.files || baseline.bytes !== result.bytes) {
        console.log("  note: the corpus differs from the baseline's");
      }
    }
  },
};
