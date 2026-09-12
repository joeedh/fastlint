// Applying the fixes a report carries (task 8.3). A message's `fix` is a
// UTF-16 range and text over the linted source, so a pass applies the ones
// that do not overlap and lints again; both the CLI's `--fix` and the VS Code
// extension's fix-all run this loop.

import type { EslintFix, EslintMessage } from "./engine.ts";

/** Passes a fix loop makes before giving up on a text whose fixes keep
 * producing new problems. ESLint stops at ten as well. */
export const maxFixPasses = 10;

/** The fixes of `messages` that can apply together: in source order, each
 * starting at or after the previous one ended. */
export function nonOverlapping(messages: readonly EslintMessage[]): EslintFix[] {
  const fixes = messages
    .filter((message) => message.fix !== undefined)
    .map((message) => message.fix!)
    .sort((a, b) => a.range[0] - b.range[0] || a.range[1] - b.range[1]);
  const out: EslintFix[] = [];
  let end = -1;
  for (const fix of fixes) {
    if (fix.range[0] < end) continue;
    out.push(fix);
    end = fix.range[1];
  }
  return out;
}

/** `text` with `fixes` applied, which must be non-overlapping and in order. */
export function applyFixes(text: string, fixes: readonly EslintFix[]): string {
  let out = "";
  let at = 0;
  for (const fix of fixes) {
    out += text.slice(at, fix.range[0]) + fix.text;
    at = fix.range[1];
  }
  return out + text.slice(at);
}
