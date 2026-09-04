# fastlint

A fast TypeScript/JavaScript linter written in C++. It has its own scanner and
recursive-descent parser, a flat arena AST with full trivia fidelity so fixers
can edit the tree instead of the text, type information from `tsc --api` (the
Go-native TypeScript 7 compiler), and a SQLite cache of type facts and rule
results. Custom rules in TypeScript are planned via litestl's binding system,
built both as an N-API addon and as WASM.

## Documents

- docs/STRATEGY.md — architecture and the reasoning behind each choice. Read it
  before changing the parser, AST, fixer, or type-cache design.
- docs/tasklists/MASTER.md — the master task list with status checkboxes.
  Update checkboxes as work lands; add sub-tasks rather than tracking work
  elsewhere.
- docs/tests.md — the in-house test framework (`fastlint::test`), snapshot
  format, and per-component test strategy.
- docs/debugging.md — debugging aids per subsystem (dump commands, trace
  flags, ASAN, leak tracker).
- vendor/litestl/CLAUDE.md — conventions for the vendored litestl library.

## Environment

- Windows 11, VS 18 Community. cmake, ninja, clang-format and `vcvarsall.bat`
  ship with VS and are not on PATH; `make.ts env` locates them with `vswhere`.
- Linux and WSL build too, with gcc or clang. `make.ts env` takes the tools off
  PATH there; clang-format must be 20 or newer to match how the sources are
  committed.
- Node 24, TypeScript 7.0.2. The native compiler binary is `tsc`; its API
  server is `tsc --api`. The Go source is checked out at C:/dev/TypeScript
  (`tsc/internal/api/proto.go` is the protocol).
- Package manager is pnpm (`pnpm-lock.yaml`). Do not run `npm install`.

## Build and run

`node make.ts <command>` is the single entry point (yargs, commands in
`tools/make/<cmd>.ts`). The build is CMake + Ninja with presets `debug`,
`release`, `relwithdebinfo`, `asan`, `clang-asan`. Until `make.ts` exists,
see MASTER.md task 1 for the intended commands: `env`, `vcvars`, `configure`,
`build`, `test`, `format`, `deps`, `run`, `check`.

## C++ conventions

- C++20, headers `.h`, sources `.cc`, under `source/`. Warnings are errors
  for our code, not for `vendor/`.
- Use litestl containers instead of the STL in all our code:
  `litestl::util::Vector`, `Map`, `Set`, `OrderedSet`, `Span`, `Array`,
  `BoolVector`, `string`. `std::` containers are allowed only inside
  boundary adapters (SQLite, msgpack, N-API, OS calls) and must not leak
  past them. `util::Array` is an alias of `std::array`; spell it `Array`
  anyway so a future replacement is a one-line change.
- Allocate through `litestl::alloc` so the leak tracker sees every block.
  Wrap intentional permanent allocations in `alloc::PermanentGuard`.
- No exceptions in our code. Errors are return values; tests have no
  `CHECK_THROWS`.
- AST nodes are flat arena records addressed by `uint32_t` ids. Do not put
  SBO containers, pointers, or virtual dispatch inside nodes; children are
  ranges into shared arena arrays. SBO containers belong in scratch and
  rule-local state.
- Format with clang-format (`node make.ts format`).

## TypeScript conventions

- Tooling and spikes are TypeScript run directly by Node (`node make.ts`),
  with no build step. `tsconfig.json` enables `strict`, `strictNullChecks`
  and `erasableSyntaxOnly`, so avoid enums, namespaces, and parameter
  properties.
- Format with the `@pathtx/prettier` fork, installed as a local dev
  dependency, never the global prettier.
- TS tests use Node's built-in runner (`node --test`).

## Testing

- Every C++ test uses `fastlint::test` from `source/testing/`. Snapshot files
  under `tests/__snapshots__/` are committed and reviewed like code; never
  run `-u` without reading the diff.
- `node make.ts test` runs the `[fast]` tier. `[integration]` needs `tsc` on
  PATH; `[slow]` and `[bench]` are opt-in with `--all`.
- Parser work is gated on the differential harness against tsgo
  (`node make.ts parse-diff`) and the ASAN fuzz run.


## Comments

Comments are prose, so the Prose rules below govern them as well. The rules in this section
are the ones that apply only to code.

- **A comment describes the code directly beneath it.** A comment placed above an `if` is read
  as a caption for the branch it guards, so one that explains the opposite case belongs on the
  `else`, or should be reworded to describe the test itself. Misplacing a comment this way is a
  correctness bug, not a style one.
- **Delete commented-out code — never leave it as commentary.** Git history holds it. A
  commented-out call, import or block explains nothing about the code that survives, and it
  goes stale silently because nothing type-checks it.
- **Never restate what the code already says.** `inputs: {}, //tool properties` and
  `case keymap.Escape: //esc` add a maintenance burden and no information. A comment earns its
  place by giving a reason, a constraint, or a consequence.
- **Cite a named constant rather than its value.** A comment saying "thirty seconds" beside
  `LINGER_MS` is wrong the first time the constant changes; write `` `LINGER_MS` ``.
- **Rename instead of commenting a name.** If the sentence's work is translating an
  identifier — what `snapMode` means, what a bare `-1` means — rename the identifier or
  introduce a named constant, then delete the sentence. Comment a name only when the name
  cannot be fixed. Try to avoid names longer than three words or 25 characters
  (10 characters or less is preferred).
- **Comment the consequence, not the arguments.** Options passed at a call site (`capture`,
  `passive`, a flag, a lifetime) are already on screen. Say what the reader cannot see: what
  the call does to everything around it. "Does not inhibit the event from reaching other
  consumers" earns its line; "registered `passive` so it cannot call `preventDefault`" does not.
- **State facts; do not defend the design.** Rationale belongs in a comment only when a reader
  looking at the surrounding code still could not derive it — an ordering constraint, a platform
  quirk, a decision with a live alternative. "Why this is the good version" and "what would go
  wrong under the naive one" are commit-message material.
- **A doc comment continues its declaration; it does not restate it.** Do not re-supply the
  subject the declaration already names, and do not narrate the signature. A field or property
  takes a noun phrase or a bare predicate — "Pointer ids currently down.", "Detected via the
  presence of multiple pointer ids." A class, function or method takes a predicate, because the
  reader needs to know what it does — "Draws the links beneath the node frames in screen space."
  A headless noun phrase over a class or a function is a fragment opener; do not use one.
  A doc comment that reads as a standalone paragraph is usually rationale in disguise.
- **Inline notes and doc comments are punctuated differently.** An inline `//` note is a
  fragment with no terminal period; a `/** … */` doc comment is a punctuated sentence. One
  line each, unless the fact genuinely needs two.
- **Non-doc comments use `//`.** Doc comments use proper `/** … */` brackets. Don't use
  `/* … */` for ordinary inline commentary.
- **Non-doc comments are at most 3 lines.** A longer block comment is allowed sparingly —
  budget roughly one per 500 lines of a file — for genuinely load-bearing context that
  can't be stated in three lines.
- **Doc comments stay reasonably concise.** Say what the thing is and any non-obvious
  contract; don't restate the signature or narrate the implementation.
- **Temporary comments are marked `CLAUDENOTE:`.** Any scratch/working comment Claude
  writes gets that prefix, and all of them must be removed before the final commit of a
  plan (or at the end of the plan, whichever comes first).

### Prose

These rules govern every piece of prose in the repository. They apply to code comments, to
this file, and to everything under `documentation/`.

- **Write plain declarative prose — no epigrams.** State the constraint or decision
  directly: "An empty answer is deliberate and is passed to the model as-is", not "Empty is an
  answer — silence, said out loud." If a sentence needs a second read to parse, rewrite it.
  Specific patterns to catch:
  - **Inverted syntax and personification** — the sentence performs rather than informs.
  - **Metaphorical equations** — "The leak scan is the refusal", "what ships is identity",
    "the project as commands". The connector word varies — do not get hung up on "is"
    versus "as". Say what happens instead: "Refuses if the leak scan finds a known name
    still in the body."
  - **Fragment openers that defer the subject — never use this pattern.** Naming a placeholder
    and then withholding the real content behind a colon or a dash is always wrong: "The
    redactor to scan a report with: the one that wrote it, else one built from the project as it
    stands." Lead with a complete sentence and name each case as you reach it. A doc comment is
    not an exception, and deleting the label is not the fix, because the apposition left behind
    is still headless. Supply a predicate instead. Write "Draws the links beneath the node
    frames in screen space." rather than "The link underlay: a screen-space canvas beneath the
    node frames." or the bare "Screen-space canvas beneath the node frames."
  - **Double negatives** — "the palette cannot be relied on not to". State the positive claim.
  - **Pronouns and ellipses that point outside the sentence** — "the second case", "asking
    twice is how…" — each sentence should carry its own referents.
  - **"Clause A, else B" constructions** — "Resolve a push's destination: the named window
    when it still exists, else the focused window falling back to the most recently focused
    one." Spell out the cases as ordinary sentences instead: "Pushes to the named window if it
    still exists. Otherwise pushes to the focused window, or the most recently focused window
    if none is focused."
  - **Adverbs hung off the end of a noun phrase** — "the next pointerdown anywhere", "the
    handler above". The adverb postmodifies the noun, but the reader cannot tell on first pass
    whether it attaches to the noun or to the clause's verb, and an event or API name coined
    from a verb ("pointerdown") re-parses as a clause when an adverb follows it. Attach the
    qualification to a verb, or state it as its own fact: "the listener is on `window`".
  - **Non-assertive words under a definite** — "any", "anywhere", "ever" range over
    alternatives, so they fight a definite description that names exactly one thing. "A press
    anywhere dismisses it" reads fine; "the next pointerdown anywhere" does not.
  - **Rhetorical emphasis** — bold and italics inside a sentence mark the clause the author
    found most interesting, not the one the reader needs first. Put the load-bearing claim in
    the first sentence and drop the markup. A bolded lead-in that labels a Markdown bullet is
    structure rather than emphasis, and is fine.
  - **A head noun that is not what the thing is** — a module of commands documented as "The
    prompt an asset is generated from, as commands" asserts that the module is a prompt, then
    retracts it through a preposition. Lead with the head noun that names the thing —
    "Commands for the prompt an asset is generated from" — and demote the rest to a
    complement. A trailing ", as X" or ", in the form of X" is the same metaphorical equation
    above smuggled in through an adjunct.
- **Reserve backticks for code symbols.** Backticks belong on identifiers, types, commands,
  and file globs the reader will type. A file path cited mid-sentence as a reference —
  documentation/NodeEditor.md §3 — takes none, because marking it up gives it the same weight
  as the identifiers around it and dilutes them. Markdown link text is the one exception and
  keeps its backticks, where the marking separates a path from the prose around it rather than
  competing with nearby identifiers.
- **Bracket a subordinate alternative rather than fencing it with commas.** Parentheses mark the
  material as skippable, so the reader gets a complete sentence either way; paired commas leave
  it unclear whether the second comma closes an interpolation or opens a new clause. Write
  "Dropping onto itself (or onto a neighbor it would split against) is not a rip". Drop any comma
  that would follow the closing bracket — it separates the subject from its verb.

