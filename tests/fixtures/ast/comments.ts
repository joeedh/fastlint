#!/usr/bin/env node
// file header

/** Doc comment on the class. */
export class Widget {
  // leading the property
  private count = 0; // trailing the property

  /* before method */ render(/* param */ x: number): void {
    // dangling in an empty statement list? no, leads the call
    draw(x); // after the call
    // trailing the call on its own line
  }

  empty() {
    // only a comment
  }
}

const list = [
  1, // one
  // two comes next
  2,
];

function f() {} /* between statements */ function g() {}

// last line
