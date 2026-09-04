import { spawn, spawnSync, type SpawnOptions } from "node:child_process";
import { color, fail } from "./log.ts";

export interface RunOptions {
  cwd?: string;
  env?: NodeJS.ProcessEnv;
  /** Capture stdout instead of inheriting it. */
  capture?: boolean;
  /** Return the exit code rather than exiting the process on failure. */
  allowFailure?: boolean;
  quiet?: boolean;
}

export interface RunResult {
  code: number;
  stdout: string;
}

/**
 * Runs a command with an argv array, never a shell string, so no path with a
 * space needs quoting. Exits with the child's code unless allowFailure is set.
 */
export async function run(
  command: string,
  args: readonly string[],
  options: RunOptions = {}
): Promise<RunResult> {
  if (!options.quiet) {
    console.log(color.dim(`$ ${command} ${args.join(" ")}`));
  }
  const spawnOptions: SpawnOptions = {
    cwd  : options.cwd,
    env  : options.env ?? process.env,
    stdio: options.capture ? ["ignore", "pipe", "inherit"] : "inherit",
  };
  const child = spawn(command, [...args], spawnOptions);

  let stdout = "";
  if (options.capture && child.stdout) {
    child.stdout.setEncoding("utf8");
    child.stdout.on("data", (chunk: string) => {
      stdout += chunk;
    });
  }

  const code = await new Promise<number>((resolve, reject) => {
    child.once("error", reject);
    child.once("close", (status) => resolve(status ?? 1));
  });

  if (code !== 0 && !options.allowFailure) {
    fail(`${command} exited with code ${code}`);
  }
  return { code, stdout };
}

/** Synchronous capture, for short probes like `vswhere` and `--version`. */
export function capture(
  command: string,
  args: readonly string[],
  options: { cwd?: string; env?: NodeJS.ProcessEnv } = {}
): string | undefined {
  const result = spawnSync(command, [...args], {
    cwd        : options.cwd,
    env        : options.env ?? process.env,
    encoding   : "utf8",
    windowsHide: true,
  });
  if (result.error || result.status !== 0) return undefined;
  return result.stdout;
}
