// Turns the text a fix-all pass produced back into edits on the document
// (task 9.4). The fixes of a later pass are in the coordinates of the earlier
// pass's output, so composing them is not worth the trouble; a line diff of
// the before and after text gives one edit per changed run of lines, trimmed
// to the characters that differ. That is what VS Code applies, so the cursor
// and the undo stack see small edits rather than a whole-file replace.

import type { TextEdit } from "vscode-languageserver/node";
import type { TextDocument } from "vscode-languageserver-textdocument";

/** A run of `before` lines replaced by a run of `after` lines. */
interface Hunk {
  beforeStart: number;
  beforeEnd: number;
  afterStart: number;
  afterEnd: number;
}

/** Splits into lines that keep their terminators, so joining them restores
 * the text and a hunk's offsets follow from its lines' lengths. */
function lines(text: string): string[] {
  const out: string[] = [];
  let at = 0;
  for (;;) {
    const nl = text.indexOf("\n", at);
    if (nl < 0) {
      if (at < text.length) out.push(text.slice(at));
      return out;
    }
    out.push(text.slice(at, nl + 1));
    at = nl + 1;
  }
}

/** Beyond this many differing lines the diff is given up on and the changed
 * region is replaced whole; the trace grows as the square of the distance. */
const maxDistance = 1000;

/**
 * Myers' O((N+M)D) diff over lines, returning the hunks in order. Undefined
 * when the texts differ in more than `maxDistance` lines.
 */
function myers(a: readonly string[], b: readonly string[]): Hunk[] | undefined {
  const n = a.length;
  const m = b.length;
  const limit = Math.min(n + m, maxDistance);
  // One frontier per step: `trace[d][k + d + 1]` is the furthest x on diagonal
  // k. The seed stands for step -1, where diagonal 1 starts at x = 0.
  const trace: Int32Array[] = [];
  let v = new Int32Array(3);

  let found = -1;
  for (let d = 0; d <= limit && found < 0; d++) {
    const prev = v;
    v = new Int32Array(2 * d + 3);
    for (let k = -d; k <= d; k += 2) {
      const down = k === -d || (k !== d && prev[k - 1 + d]! < prev[k + 1 + d]!);
      let x = down ? prev[k + 1 + d]! : prev[k - 1 + d]! + 1;
      let y = x - k;
      while (x < n && y < m && a[x] === b[y]) {
        x++;
        y++;
      }
      v[k + d + 1] = x;
      if (x >= n && y >= m) {
        found = d;
        break;
      }
    }
    trace.push(v);
  }
  if (found < 0) return undefined;

  // Walk the trace back, recording each step as a one-line hunk, then merge
  // adjacent ones.
  const hunks: Hunk[] = [];
  let x = n;
  let y = m;
  for (let d = found; d > 0; d--) {
    const prev = trace[d - 1]!;
    const k = x - y;
    const down = k === -d || (k !== d && prev[k - 1 + d]! < prev[k + 1 + d]!);
    const prevK = down ? k + 1 : k - 1;
    const prevX = prev[prevK + d]!;
    const prevY = prevX - prevK;
    while (x > prevX && y > prevY) {
      x--;
      y--;
    }
    if (down) {
      hunks.push({ beforeStart: x, beforeEnd: x, afterStart: y - 1, afterEnd: y });
    } else {
      hunks.push({ beforeStart: x - 1, beforeEnd: x, afterStart: y, afterEnd: y });
    }
    x = prevX;
    y = prevY;
  }
  hunks.reverse();

  const merged: Hunk[] = [];
  for (const hunk of hunks) {
    const last = merged[merged.length - 1];
    if (
      last &&
      last.beforeEnd === hunk.beforeStart &&
      last.afterEnd === hunk.afterStart
    ) {
      last.beforeEnd = hunk.beforeEnd;
      last.afterEnd = hunk.afterEnd;
    } else {
      merged.push({ ...hunk });
    }
  }
  return merged;
}

/** Shrinks a replacement to the characters that differ. */
function trimmed(
  before: string,
  after: string
): { start: number; end: number; text: string } {
  let prefix = 0;
  const shortest = Math.min(before.length, after.length);
  while (prefix < shortest && before[prefix] === after[prefix]) prefix++;
  let suffix = 0;
  while (
    suffix < shortest - prefix &&
    before[before.length - 1 - suffix] === after[after.length - 1 - suffix]
  ) {
    suffix++;
  }
  return {
    start: prefix,
    end  : before.length - suffix,
    text : after.slice(prefix, after.length - suffix),
  };
}

/**
 * The edits that turn `document`'s text (which must equal `before`) into
 * `after`, fewest and smallest first. An unchanged text yields none.
 */
export function editsBetween(
  document: TextDocument,
  before: string,
  after: string
): TextEdit[] {
  if (before === after) return [];
  const a = lines(before);
  const b = lines(after);
  const hunks = myers(a, b) ?? [
    { beforeStart: 0, beforeEnd: a.length, afterStart: 0, afterEnd: b.length },
  ];

  const offsets: number[] = [0];
  for (const line of a) offsets.push(offsets[offsets.length - 1]! + line.length);

  const edits: TextEdit[] = [];
  for (const hunk of hunks) {
    const start = offsets[hunk.beforeStart]!;
    const end = offsets[hunk.beforeEnd]!;
    const replacement = b.slice(hunk.afterStart, hunk.afterEnd).join("");
    const tight = trimmed(before.slice(start, end), replacement);
    edits.push({
      range: {
        start: document.positionAt(start + tight.start),
        end  : document.positionAt(start + tight.end),
      },
      newText: tight.text,
    });
  }
  return edits;
}
