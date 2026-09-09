// The CLI's file collection (task 8.3). A path names either one file or a tree
// to walk, and the walk takes the extensions cli/files.cc takes, so the two
// front ends lint the same set. The CLI hands both engines the list it built
// here, so a run has one answer to what it linted.

import fs from "node:fs";
import path from "node:path";

/** The extensions a directory walk picks up. A file named outright is linted
 * whatever it is called, as `.js` is. */
export const sourceExtensions = [".ts", ".tsx", ".mts", ".cts"];

/** Directories a walk never descends into. A dependency tree is not the
 * project's code, and a dot directory holds tooling state. */
function skipDirectory(name: string): boolean {
  return name === "node_modules" || name.startsWith(".");
}

function walk(dir: string, out: string[]): void {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      if (!skipDirectory(entry.name)) walk(full, out);
    } else if (entry.isFile() && sourceExtensions.includes(path.extname(entry.name))) {
      out.push(full);
    }
  }
}

/**
 * The files `inputs` names, as absolute paths with forward slashes, in the order
 * they were given and without repeats. A directory is walked; anything else is
 * taken as a file, so a path that does not exist is reported by the read rather
 * than dropped in silence.
 */
export function collectFiles(inputs: readonly string[]): string[] {
  const out: string[] = [];
  for (const input of inputs) {
    const resolved = path.resolve(input);
    if (fs.existsSync(resolved) && fs.statSync(resolved).isDirectory()) {
      walk(resolved, out);
    } else {
      out.push(resolved);
    }
  }
  const seen = new Set<string>();
  return out
    .map((file) => file.replace(/\\/g, "/"))
    .filter((file) => (seen.has(file) ? false : (seen.add(file), true)));
}
