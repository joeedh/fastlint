#!/usr/bin/env node
// The `lintrix` command the npm package installs (task 8.3). It resolves the
// config, runs the built-in rules through the native binary when one is around
// and through the bundled WASM build when there is not, runs the plugin rules
// itself, and prints the two merged into one report.
//
// It parses its own arguments rather than pulling in a parser, so the published
// package has no runtime dependency of its own.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { nativeConfig, nativeConfigName, type CompiledConfig } from "./compile.ts";
import { findConfig, loadCompiledConfig } from "./config.ts";
import { lintFiles, type FileMessages } from "./driver.ts";
import {
  findWasmModule,
  lintWithBinary,
  lintWithWasm,
  resolveBinary,
  type EngineKind,
  type FileReport,
} from "./engine.ts";
import { collectFiles } from "./files.ts";
import { formatJson, formatPretty, mergeReports, withoutWarnings } from "./report.ts";

interface Options {
  command: "lint" | "config" | "init" | "help" | "version";
  configPath?: string;
  format: "pretty" | "json";
  engine: EngineKind | "auto";
  color?: boolean;
  quiet: boolean;
  maxWarnings: number;
  concurrency?: number;
  native: boolean;
  out?: string;
  inputs: string[];
}

const usage = `usage: lintrix [options] <file|dir>...
       lintrix config [--native] [--out <path>]
       lintrix --init

options:
  --config <file>     the config to use, found by walking up otherwise
  --format <name>     pretty (the default) or json
  --engine <name>     auto (the default), native or wasm
  --concurrency <n>   worker count for the plugin rules
  --max-warnings <n>  exit 1 when more warnings than this remain
  --quiet             drop warnings from the output and the counts
  --color, --no-color force the colouring either way
  --init              write a starter lintrix.config.json and exit
  --version           print the version and exit
  -h, --help          show this help and exit

The built-in rules run through the native binary the config's "binary" names, or
one found on PATH; without one they run through the WASM build in this package.
Rules from a plugin run here either way. See docs/rules.md "Config".
`;

/** Reads the argument list, or fails with what was wrong with it. */
function parseArgs(argv: readonly string[]): Options {
  const options: Options = {
    command: "lint",
    format: "pretty",
    engine: "auto",
    quiet: false,
    maxWarnings: -1,
    native: false,
    inputs: [],
  };
  const value = (flag: string, next: string | undefined): string => {
    if (next === undefined) throw new Error(`${flag} needs a value`);
    return next;
  };

  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i]!;
    if (i === 0 && arg === "config") {
      options.command = "config";
    } else if (i === 0 && arg === "lint") {
      options.command = "lint";
    } else if (arg === "--help" || arg === "-h") {
      options.command = "help";
    } else if (arg === "--version") {
      options.command = "version";
    } else if (arg === "--init") {
      options.command = "init";
    } else if (arg === "--config") {
      options.configPath = value(arg, argv[++i]);
    } else if (arg === "--format") {
      const format = value(arg, argv[++i]);
      if (format !== "pretty" && format !== "json") {
        throw new Error(`unknown format '${format}'`);
      }
      options.format = format;
    } else if (arg === "--engine") {
      const engine = value(arg, argv[++i]);
      if (engine !== "auto" && engine !== "native" && engine !== "wasm") {
        throw new Error(`unknown engine '${engine}'`);
      }
      options.engine = engine;
    } else if (arg === "--concurrency") {
      options.concurrency = Number(value(arg, argv[++i]));
    } else if (arg === "--max-warnings") {
      options.maxWarnings = Number(value(arg, argv[++i]));
    } else if (arg === "--quiet") {
      options.quiet = true;
    } else if (arg === "--color") {
      options.color = true;
    } else if (arg === "--no-color") {
      options.color = false;
    } else if (arg === "--native") {
      options.native = true;
    } else if (arg === "--out") {
      options.out = value(arg, argv[++i]);
    } else if (arg.startsWith("-")) {
      throw new Error(`unknown option '${arg}'`);
    } else {
      options.inputs.push(arg);
    }
  }
  return options;
}

/** The version this package publishes, read from its own manifest. */
function version(): string {
  const here = path.dirname(fileURLToPath(import.meta.url));
  for (const up of ["..", "../..", "../../..", "../../../.."]) {
    const manifest = path.join(here, up, "package.json");
    if (fs.existsSync(manifest)) {
      const parsed = JSON.parse(fs.readFileSync(manifest, "utf8")) as { version?: string };
      if (parsed.version) return parsed.version;
    }
  }
  return "0.0.0";
}

const starterConfig = `{
  "$schema": "./node_modules/lintrix/schema/lintrix.config.schema.json",
  "extends": "lintrix:recommended",
  "rules": {},
  "ignores": ["**/node_modules/**", "**/dist/**"]
}
`;

/** Writes a starter config in the working directory, refusing to overwrite. */
function init(): number {
  const out = path.resolve("lintrix.config.json");
  if (fs.existsSync(out)) {
    process.stderr.write(`${path.basename(out)} already exists\n`);
    return 1;
  }
  fs.writeFileSync(out, starterConfig);
  process.stdout.write(`wrote ${path.basename(out)}\n`);
  process.stdout.write(
    'extends lintrix:recommended; set rule severities under "rules".\n'
  );
  return 0;
}

/** The config the run uses, or a failure naming what was missing. */
async function load(options: Options): Promise<{ path: string; compiled: CompiledConfig }> {
  const found = options.configPath
    ? path.resolve(options.configPath)
    : findConfig(process.cwd());
  if (!found) {
    throw new Error("no lintrix.config.{ts,mts,js,mjs,json} here or above");
  }
  if (!fs.existsSync(found)) throw new Error(`${found} does not exist`);
  return { path: found, compiled: await loadCompiledConfig(found) };
}

/** `lintrix config`: the resolved JSON, or the native handoff document. */
async function configCommand(options: Options): Promise<number> {
  const { path: configPath, compiled } = await load(options);
  for (const name of compiled.unknownRules) {
    process.stderr.write(`rule '${name}' is not defined by its plugin\n`);
  }
  const document = options.native ? nativeConfig(compiled) : compiled.file;
  const json = `${JSON.stringify(document, undefined, 2)}\n`;
  if (!options.out) {
    process.stdout.write(json);
    return 0;
  }
  const asked = path.resolve(options.out);
  const intoDir = fs.existsSync(asked) && fs.statSync(asked).isDirectory();
  const out = intoDir
    ? path.join(asked, options.native ? nativeConfigName : "lintrix.config.json")
    : asked;
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, json);
  process.stdout.write(`wrote ${out}\n`);
  return 0;
}

/** Runs the built-in rules through whichever engine the options allow. */
async function runBuiltins(
  options: Options,
  configPath: string,
  compiled: CompiledConfig,
  files: readonly string[]
): Promise<FileReport[]> {
  const binary = options.engine === "wasm" ? undefined : resolveBinary(compiled);
  if (binary) return lintWithBinary(binary, configPath, compiled, files);
  if (options.engine === "native") {
    throw new Error("no native lintrix binary found; drop --engine native to use WASM");
  }
  const module = findWasmModule();
  if (!module) {
    throw new Error("no native binary on PATH and no bundled WASM build to fall back to");
  }
  return lintWithWasm(module, compiled, files);
}

/** `lintrix <files>`: lint, merge and print. */
async function lintCommand(options: Options): Promise<number> {
  const { path: configPath, compiled } = await load(options);
  for (const name of compiled.unknownRules) {
    process.stderr.write(`rule '${name}' is not defined by its plugin\n`);
  }

  const files = collectFiles(options.inputs);
  if (files.length === 0) {
    process.stderr.write(usage);
    return 2;
  }

  const builtins = await runBuiltins(options, configPath, compiled, files);
  // The plugin rules need no engine of their own: the driver already runs them
  // over the addon, and a config with none costs nothing here.
  const plugin: FileMessages[] =
    compiled.rules.size > 0
      ? await lintFiles(files, { configPath, concurrency: options.concurrency })
      : [];

  let reports = mergeReports(files, builtins, plugin);
  if (options.quiet) reports = withoutWarnings(reports);

  const color = options.color ?? process.stdout.isTTY === true;
  process.stdout.write(
    options.format === "json" ? formatJson(reports) : formatPretty(reports, color)
  );

  const errors = reports.reduce((sum, report) => sum + report.errorCount, 0);
  const warnings = reports.reduce((sum, report) => sum + report.warningCount, 0);
  if (errors > 0) return 1;
  if (options.maxWarnings >= 0 && warnings > options.maxWarnings) return 1;
  return 0;
}

async function main(): Promise<number> {
  let options: Options;
  try {
    options = parseArgs(process.argv.slice(2));
  } catch (error) {
    process.stderr.write(`${error instanceof Error ? error.message : String(error)}\n\n`);
    process.stderr.write(usage);
    return 2;
  }
  if (process.argv.length <= 2) {
    process.stdout.write(usage);
    return 0;
  }

  try {
    switch (options.command) {
      case "help":
        process.stdout.write(usage);
        return 0;
      case "version":
        process.stdout.write(`${version()}\n`);
        return 0;
      case "init":
        return init();
      case "config":
        return await configCommand(options);
      case "lint":
        return await lintCommand(options);
    }
  } catch (error) {
    process.stderr.write(`${error instanceof Error ? error.message : String(error)}\n`);
    return 2;
  }
}

process.exitCode = await main();
