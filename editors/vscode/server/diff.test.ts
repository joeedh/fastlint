import assert from "node:assert/strict";
import { test } from "node:test";
import { TextDocument } from "vscode-languageserver-textdocument";

import { editsBetween } from "./diff.ts";

const doc = (text: string): TextDocument =>
  TextDocument.create("file:///p/a.ts", "typescript", 1, text);

/** Applies `edits` to `text` the way an editor would, so a test checks the
 * result rather than the exact edits. */
function applied(text: string, before: TextDocument, after: string): string {
  const edits = editsBetween(before, text, after);
  const result = TextDocument.applyEdits(before, edits);
  return result;
}

test("identical text yields no edits", () => {
  assert.deepEqual(editsBetween(doc("a\nb\n"), "a\nb\n", "a\nb\n"), []);
});

test("one changed line becomes one edit trimmed to the difference", () => {
  const before = "let a = 1;\nvar b = 2;\nlet c = 3;\n";
  const after = "let a = 1;\nlet b = 2;\nlet c = 3;\n";
  const edits = editsBetween(doc(before), before, after);
  assert.equal(edits.length, 1);
  assert.deepEqual(edits[0]!.range, {
    start: { line: 1, character: 0 },
    end  : { line: 1, character: 3 },
  });
  assert.equal(edits[0]!.newText, "let");
});

test("far-apart changes become separate edits", () => {
  const before = ["debugger;", ...Array(50).fill("x();"), "if (a) b();", ""].join("\n");
  const after = ["", ...Array(50).fill("x();"), "if (a) {b();}", ""].join("\n");
  const edits = editsBetween(doc(before), before, after);
  assert.equal(edits.length, 2);
  assert.equal(applied(before, doc(before), after), after);
});

test("insertions, deletions and a missing final newline round-trip", () => {
  const cases: [string, string][] = [
    ["a\nb\nc\n", "a\nc\n"],
    ["a\nc\n", "a\nb\nc\n"],
    ["a\nb", "a\nb\n"],
    ["a\nb\n", "a\nb"],
    ["", "x\n"],
    ["x\n", ""],
    ["a\r\nb\r\n", "a\r\nB\r\n"],
    ["\n\n\n", "\n"],
  ];
  for (const [before, after] of cases) {
    assert.equal(
      applied(before, doc(before), after),
      after,
      JSON.stringify([before, after])
    );
  }
});

test("a text past the distance cap still round-trips", () => {
  const before = Array.from({ length: 1500 }, (_, i) => `line ${i}`).join("\n");
  const after = Array.from({ length: 1500 }, (_, i) => `LINE ${i}`).join("\n");
  const edits = editsBetween(doc(before), before, after);
  assert.equal(edits.length, 1);
  assert.equal(applied(before, doc(before), after), after);
});
