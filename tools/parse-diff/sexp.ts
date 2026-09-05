// Streams the S-expression dumps both parsers print (see tsgo-dump.go and
// `fastlint dump-tree --spans --batch`) into trees, one callback per file.

import readline from "node:readline";
import type { Readable } from "node:stream";

export interface TreeNode {
  kind: string;
  start: number;
  end: number;
  /** Set on fastlint nodes flagged `missing` (a required node that was absent). */
  missing?: boolean;
  children: TreeNode[];
}

export interface FileResult {
  path: string;
  root: TreeNode | undefined;
  error: string | undefined;
}

// `(Kind start end` from tsgo, `(Kind@start-end ...` from fastlint.
const openLine = /^\s*\((\S+?)(?:@(\d+)-(\d+)| (\d+) (\d+))?(?:\s|$)/;
const missingFlag = /^\s*\(\S+ :[^"]* missing(?: |$)/;

/** Parses every file in a dump stream; resolves when the stream ends. */
export async function readDumps(
  stream: Readable,
  onFile: (file: FileResult) => void
): Promise<void> {
  const lines = readline.createInterface({ input: stream, crlfDelay: Infinity });
  let current: FileResult | undefined;
  let stack: TreeNode[] = [];

  const flush = () => {
    if (current) {
      if (stack.length > 0 && !current.error) {
        current.error = `unbalanced dump (${stack.length} open)`;
      }
      onFile(current);
    }
    current = undefined;
    stack = [];
  };

  for await (const line of lines) {
    if (line.startsWith("#file ")) {
      flush();
      current = { path: line.slice(6).trim(), root: undefined, error: undefined };
      continue;
    }
    if (line.startsWith("#error ")) {
      if (current) current.error = line.slice(7).trim();
      continue;
    }
    if (!current) continue;
    const trimmed = line.trimStart();
    if (trimmed.startsWith(")")) {
      const done = stack.pop();
      if (done && stack.length === 0) current.root = done;
      continue;
    }
    const m = openLine.exec(line);
    if (!m) continue; // token text or diagnostics line
    const node: TreeNode = {
      kind    : m[1],
      start   : Number(m[2] ?? m[4] ?? 0),
      end     : Number(m[3] ?? m[5] ?? 0),
      children: [],
    };
    if (missingFlag.test(line)) node.missing = true;
    if (stack.length > 0) stack[stack.length - 1].children.push(node);
    stack.push(node);
  }
  flush();
}
