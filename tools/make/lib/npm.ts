// Running npm from a task. On Windows the `npm` on PATH is a `.cmd`, which Node
// refuses to spawn without a shell, so the tasks reach npm's own entry script
// through the Node they are already running under. That also pins the npm that
// ships with this Node instead of whichever one a shell would find.

import fs from "node:fs";
import path from "node:path";

import { fail } from "./log.ts";
import { capture, run, type RunOptions, type RunResult } from "./spawn.ts";

/** npm's entry script beside the running Node, or undefined on a layout that
 * does not have one (a Node built without its bundled npm). */
export function npmCli(): string | undefined {
  const candidate = path.join(
    path.dirname(process.execPath),
    "node_modules",
    "npm",
    "bin",
    "npm-cli.js"
  );
  return fs.existsSync(candidate) ? candidate : undefined;
}

export async function npm(
  args: readonly string[],
  options: RunOptions = {}
): Promise<RunResult> {
  const cli = npmCli();
  if (!cli) fail("no npm beside this Node; install Node with its bundled npm");
  return run(process.execPath, [cli, ...args], options);
}

/** The account `npm publish` would publish as, or undefined when not logged in. */
export function npmWhoami(): string | undefined {
  const cli = npmCli();
  if (!cli) return undefined;
  return capture(process.execPath, [cli, "whoami"])?.trim() || undefined;
}

/** The versions of `name` the registry already has, or undefined when the
 * package is unpublished or the registry could not be reached. */
export function publishedVersions(name: string): string[] | undefined {
  const cli = npmCli();
  if (!cli) return undefined;
  const out = capture(process.execPath, [cli, "view", name, "versions", "--json"]);
  if (out === undefined) return undefined;
  try {
    const parsed = JSON.parse(out) as string[] | string;
    return typeof parsed === "string" ? [parsed] : parsed;
  } catch {
    return undefined;
  }
}
