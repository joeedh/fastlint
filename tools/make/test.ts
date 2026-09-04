import fs from "node:fs";
import path from "node:path";
import type { CommandModule } from "yargs";
import { buildPreset } from "./build.ts";
import { asanEnv } from "./lib/asan.ts";
import { color, fail, info, step } from "./lib/log.ts";
import { presetOptions, resolvePreset, type PresetArgs } from "./lib/preset.ts";
import { buildDir, exeSuffix, repoRoot } from "./lib/paths.ts";
import { run } from "./lib/spawn.ts";

interface Args extends PresetArgs {
  filter?: string;
  tag?: string[];
  all: boolean;
  update: boolean;
  ts: boolean;
  ctest: boolean;
  list: boolean;
  isolate: boolean;
}

interface TestReport {
  suite: string;
  summary: { passed: number; failed: number; skipped: number };
  tests: { name: string; status: string; tags: string[]; durationMs: number }[];
}

/** Every built test executable, which CMake names `<suite>_tests`. */
function testExecutables(dir: string): string[] {
  const out: string[] = [];
  const walk = (at: string): void => {
    if (!fs.existsSync(at)) return;
    for (const entry of fs.readdirSync(at, { withFileTypes: true })) {
      const full = path.join(at, entry.name);
      if (entry.isDirectory()) walk(full);
      else if (entry.isFile() && entry.name.endsWith(`_tests${exeSuffix}`))
        out.push(full);
    }
  };
  walk(dir);
  return out.sort();
}

/** Every `*.test.ts` under a directory. */
function findTsTests(at: string): string[] {
  const out: string[] = [];
  if (!fs.existsSync(at)) return out;
  for (const entry of fs.readdirSync(at, { withFileTypes: true })) {
    const full = path.join(at, entry.name);
    if (entry.isDirectory()) out.push(...findTsTests(full));
    else if (entry.name.endsWith(".test.ts")) out.push(full);
  }
  return out;
}

function runnerArgs(argv: Args): string[] {
  const args: string[] = [];
  if (argv.filter) args.push("--filter", argv.filter);
  for (const tag of argv.tag ?? []) args.push("--tag", tag);
  if (argv.all) args.push("--all");
  if (argv.update) args.push("--update");
  if (argv.list) args.push("--list");
  if (argv.isolate) args.push("--isolate");
  return args;
}

export async function runCppTests(preset: string, argv: Args): Promise<boolean> {
  await buildPreset(preset);
  const dir = buildDir(preset);
  const exes = testExecutables(dir);
  if (exes.length === 0) {
    info("no test executables built yet");
    return true;
  }

  const env = asanEnv(preset);

  if (argv.ctest) {
    const { toolchain } = await import("./lib/toolchain.ts");
    const result = await run(
      toolchain().ctest,
      ["--test-dir", dir, "--output-on-failure"],
      {
        cwd: repoRoot,
        env,
        allowFailure: true,
      }
    );
    return result.code === 0;
  }

  if (argv.list) {
    for (const exe of exes) {
      await run(exe, runnerArgs(argv), {
        cwd: repoRoot,
        env,
        allowFailure: true,
        quiet       : true,
      });
    }
    return true;
  }

  let passed = 0;
  let failed = 0;
  let skipped = 0;
  let ok = true;

  for (const exe of exes) {
    const suite = path.basename(exe, exeSuffix).replace(/_tests$/, "");
    step(`test ${suite}`);
    // The runner writes its report to a file so its own stdout stays readable.
    const reportFile = path.join(dir, `${suite}.report.json`);
    const result = await run(
      exe,
      [...runnerArgs(argv), "--json", reportFile, "--no-summary"],
      {
        cwd: repoRoot,
        env,
        allowFailure: true,
        quiet       : true,
      }
    );
    const report = fs.existsSync(reportFile)
      ? (JSON.parse(fs.readFileSync(reportFile, "utf8")) as TestReport)
      : undefined;
    if (report) {
      passed += report.summary.passed;
      failed += report.summary.failed;
      skipped += report.summary.skipped;
    }
    if (result.code !== 0) ok = false;
  }

  const parts = [
    color.green(`${passed} passed`),
    failed > 0 ? color.red(`${failed} failed`) : `${failed} failed`,
    color.dim(`${skipped} skipped`),
  ];
  info(`\n${parts.join(", ")}`);
  return ok;
}

export async function runTsTests(): Promise<boolean> {
  // Node's runner treats a directory with no test files as a failure.
  if (findTsTests(path.join(repoRoot, "tools")).length === 0) {
    info("no TypeScript tests yet");
    return true;
  }
  step("node --test");
  const result = await run(process.execPath, ["--test", "tools/"], {
    cwd         : repoRoot,
    allowFailure: true,
  });
  return result.code === 0;
}

export const command: CommandModule<object, Args> = {
  command : "test",
  describe: "build and run the C++ tests, aggregating each runner's JSON report",
  builder: (yargs) =>
    presetOptions(yargs)
      .option("filter", { type: "string", describe: "substring or glob over test names" })
      .option("tag", {
        type    : "string",
        array   : true,
        describe: "run only tests with this tag",
      })
      .option("all", {
        type    : "boolean",
        default : false,
        describe: "include slow and bench tiers",
      })
      .option("update", {
        type    : "boolean",
        alias   : "u",
        default : false,
        describe: "update snapshots",
      })
      .option("ts", {
        type    : "boolean",
        default : false,
        describe: "run the TypeScript tests too",
      })
      .option("ctest", {
        type    : "boolean",
        default : false,
        describe: "drive the suites through ctest",
      })
      .option("list", {
        type    : "boolean",
        default : false,
        describe: "list tests instead of running",
      })
      .option("isolate", {
        type    : "boolean",
        default : false,
        describe: "run each test in a child process",
      }) as never,
  handler: async (argv) => {
    const preset = resolvePreset(argv);
    const cppOk = await runCppTests(preset, argv);
    const tsOk = argv.ts ? await runTsTests() : true;
    if (!cppOk || !tsOk) fail("tests failed");
  },
};
