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

/** One field of `npm view <name>`, parsed, or undefined when the package is
 * unpublished or the registry could not be reached. */
function view<T>(name: string, field: string): T | undefined {
  const cli = npmCli();
  if (!cli) return undefined;
  const out = capture(process.execPath, [cli, "view", name, field, "--json"]);
  if (out === undefined) return undefined;
  try {
    return JSON.parse(out) as T;
  } catch {
    return undefined;
  }
}

/** The versions of `name` the registry already has. A package with exactly one
 * gets a bare string back rather than an array. */
export function publishedVersions(name: string): string[] | undefined {
  const parsed = view<string[] | string>(name, "versions");
  if (parsed === undefined) return undefined;
  return typeof parsed === "string" ? [parsed] : parsed;
}

/** The accounts allowed to publish `name`, as the registry lists them. Each
 * entry reads `account <email>`, and only the account is kept. */
export function maintainers(name: string): string[] | undefined {
  const parsed = view<string[] | string>(name, "maintainers");
  if (parsed === undefined) return undefined;
  const entries = typeof parsed === "string" ? [parsed] : parsed;
  return entries.map((entry) => entry.split(" ")[0]!);
}

/**
 * Why publishing `name` as `account` would be refused, or undefined when it
 * would go through. A name someone else holds is the case worth catching early:
 * npm reports it as a bare 403 at the end of a release that otherwise worked.
 */
export function publishBlocker(name: string, account: string): string | undefined {
  const owners = maintainers(name);
  if (owners === undefined) return undefined;
  if (owners.includes(account)) return undefined;
  return `the name '${name}' on npm belongs to ${owners.join(", ")}; publish under a scope you own`;
}
