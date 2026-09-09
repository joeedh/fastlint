# Writing plugins

A plugin is a rule (or a set of rules) written in TypeScript and run over the
same tree the built-in C++ rules see. This page is for the rule author: the
shape of a rule, the node view it reads, how a config collects rules, and how
the driver runs them. The build internals behind it (the N-API addon, the WASM
module, the shared `embed::lintText` entry point) are in docs/embedding.md.

## What a plugin can and cannot do

- A rule inspects syntax and reports problems. It walks the parsed tree through
  a typed, read-only node view and calls `context.report` for each problem.
- A rule runs under either embedding unchanged. It reads the generated view
  surface and nothing host-specific, so the same module runs over the N-API
  addon in Node and over the WASM module in a browser.
- No type information. An embedding has no tsgo process, so there is no type
  facts handle on the context. A rule that needs a type stays a native rule.
- No fixer yet. A rule reports a location and a message; it cannot attach an
  edit. A rule that needs a fix stays a native rule for now.

## Scaffolding a rule

- `node make.ts new-rule <name> [--selector <NodeKind>] [--out <file>]` writes a
  starter module: a working `Rule` that reports on every `selector` node, named
  for the author to narrow. `<name>` is kebab-case and becomes a camelCase
  export (`no-await-in-loop` exports `noAwaitInLoop`).
- `--selector` is the node kind the rule visits, `Identifier` by default. `--out`
  overrides the output path, which is `<name>.ts` in the current directory
  otherwise. The command refuses to overwrite an existing file, and prints the
  `import` line to add to a config.
- `node make.ts new-rule --init` writes a starter `lintrix.config.ts` instead of
  a rule: a `plugins` entry pointing at `./rules/index.ts` and an empty `rules`
  map. It honors `--out` and refuses to overwrite, and `--init` with a `<name>`
  is rejected. `node make.ts new-rule --help` lists every option with an example
  of each.
- The starters import from the `lintrix` package surface, so they stand on
  their own outside this repository.

## The rule shape

A rule is the `Rule` interface from `plugin/ts/runtime.ts`, re-exported from the
`lintrix` package surface.

```ts
import { NodeKind } from "lintrix";
import type { Rule } from "lintrix";

export const noDebugger: Rule = {
  name: "no-debugger",
  messages: {
    unexpected: "Unexpected 'debugger' statement.",
  },
  create(context) {
    return {
      DebuggerStatement(node) {
        context.report({ node, messageId: "unexpected" });
      },
    };
  },
};
```

- `name` is the rule id that appears on every problem it reports.
- `messages` holds the message templates, looked up by `messageId`. A
  `{{placeholder}}` in a template is filled from the `data` passed to `report`.
  `messages` is optional when a rule always passes a literal `message` instead.
- `create(context)` returns the visitors. It runs once per file, so a rule that
  accumulates state across a file keeps it in the closure `create` opens.

### Visitors

- A visitor is a function keyed by a node-kind name — the keys of `NodeKind`.
  The walk dispatches each node to every rule that registered a visitor for its
  kind, in rule order, in one preorder pass.
- The kind name on the key is a coarse filter. A visitor narrows to the typed
  view with `node.is(NodeKind.Kind)`, which TypeScript reads as a type guard, so
  the view's own fields become available inside the branch.

```ts
create(context) {
  return {
    MemberExpression(node) {
      if (!node.is(NodeKind.MemberExpression)) return;
      const object = node.object;
      if (object.is(NodeKind.Identifier) && object.text === "console") {
        context.report({ node, messageId: "unexpected" });
      }
    },
  };
}
```

### The context

`RuleContext` is the one argument `create` receives and the one handle a visitor
reports through.

- `context.filename` is the file being linted.
- `context.sourceText` is the whole source, for a rule that needs the raw text
  between two offsets. `no-empty` reads it to skip a block whose braces hold a
  comment.
- `context.options` is what the config listed after the severity, and it is
  empty for a bare severity. A rule reads its own options from there.
- `context.report({ node, messageId?, message?, data? })` records a problem.
  Pass a `messageId` that keys into `messages`, or a literal `message`; `data`
  fills the template's placeholders. The problem's location comes from the
  node's range, reported as a 1-based line and column.

## The node view

A visitor reads an immutable view of a node, not the layout. The base `Node`
interface (plugin/generated/ts/views.ts) carries what every node has; a narrowed
view adds that kind's own fields.

- `node.type` is the `NodeKind` discriminant. `node.flags` is the raw flag word;
  `node.hasFlag(Flag.X)` tests one flag.
- `node.childCount` and `node.child(i)` walk the positional children; a child is
  `null` where the slot is absent.
- `node.parent` is the enclosing node, or `null` at the root.
- `node.text` is the node's source text. `node.range` is its `[start, end)` byte
  offsets.
- `node.is(NodeKind.Kind)` narrows to the per-kind view. `node.descendants(kind)`
  returns every descendant of one kind in preorder, as one typed array.

The per-kind fields come from the grammar. A field is a named child
(`MemberExpression.object`), a list child (`BlockStatement.body`, an array), a
flag (`MemberExpression.isComputed`, a boolean), or an enum
(`VariableDeclaration.kind`, a `VariableKind`). The generated views are the
reference for which fields a kind has.

- In-repo rules under `plugin/ts/rules/` import both the runtime and the field
  enums from the generated module. `no-var` reads `VariableKind.Var`, `eqeqeq`
  reads `BinaryOperator.Equal` and `BinaryOperator.NotEqual`.
- The `lintrix` package surface re-exports `NodeKind`, `kindNames`, `Node`,
  `Rule`, `RuleContext`, `defineConfig`, the config loader and the driver. A rule that needs a
  field enum an external package does not yet re-export imports it from the
  generated views module directly.

## Packaging rules as a plugin

A config reaches a rule through a plugin: a module whose `rules` maps each
rule's own name to the rule. The map is what a `plugins` prefix stands for, so a
config names one rule `prefix/rule`.

```ts
// rules/index.ts
import { eqeqeq } from "./eqeqeq.ts";
import { noConsole, noDebugger } from "./no-debugger.ts";

export default {
  rules: {
    eqeqeq,
    "no-console": noConsole,
    "no-debugger": noDebugger,
  },
};
```

- The plugin is read from the module's default export, or from a named `plugin`
  or `rules` export. A value in the map that is not rule-shaped is rejected at
  load rather than at the first visit.
- The key in the map is the name the config spells after the prefix, and the
  rule's own `name` is what the rule tester and the message list use. Keeping
  them the same is the least surprising thing to do.
- A plugin is an ordinary module, so a package (`@acme/lintrix-rules`) and a
  file in the project (`./rules/index.ts`) are named the same way.

## Config

A config is one document, whether it is written as JSON or as a TypeScript
module. docs/rules.md "Config" is the schema reference; this is what a plugin
author needs from it.

```ts
// lintrix.config.ts
import { defineConfig } from "lintrix";

export default defineConfig({
  plugins: { acme: "./rules/index.ts" },
  rules: {
    "acme/no-debugger": "error",
    "acme/no-console": "warn",
    "acme/eqeqeq": ["error", "always"],
  },
  overrides: [{ files: ["**/*.test.ts"], rules: { "acme/no-debugger": "off" } }],
  ignores: ["dist/**"],
});
```

- `defineConfig` is an identity helper that type-checks the literal at authoring.
  A `.json` config takes the same keys and no helper.
- `plugins` maps a prefix to the module the rules come from. A relative specifier
  is resolved against the config file's own directory.
- A rule setting is a severity (`"off"`, `"warn"`, `"error"`, or 0 to 2), or an
  array of the severity followed by the rule's options. `context.options` is
  what a rule reads them back from, and it is empty for a bare severity.
- `overrides` raise or lower a rule for the files their globs match, in order,
  and `ignores` skips a file outright. A later layer's bare severity keeps the
  options an earlier layer gave.
- The rules a plugin owns run on this side; `extends`, `project`, `projects` and
  the directive settings are read by the native binary, which runs the built-in
  rules of the same config.
- `node make.ts config` prints what a config compiles to, which is the way to
  see the JSON a `.ts` config produces.

## Running the rules

Three entry points run a config's rules, all from the `lintrix` surface.

- `lint(addon, source, filename, rules)` lints one buffer and returns the flat
  problem list. It parses once, builds the node view over the session, walks the
  tree, and frees the session when it returns. `rules` is the resolved
  `{id, rule, severity, options}` tuples, which `resolveRule(rule)` builds for a
  caller running a rule on its own. This is the call a playground or an editor
  integration makes directly.
- `lintOne(addon, compiled, filename)` reads one file and lints it with what the
  config resolves for it, reporting an `ignored` file rather than reading it.
- `lintFiles(files, { configPath, addonPath, concurrency? })` lints many files
  and returns one entry per file in input order. With more than one file it
  shards them across a `worker_threads` pool: each worker loads the addon and
  the config once, and the driver hands a worker the next file as soon as it
  returns the last, so a slow file never idles the others. One file, or
  `concurrency: 1`, stays in the calling thread. The worker pool is N-API only;
  a browser runs one file at a time on its single thread through `lint`.

Each problem carries the `ruleId` the config configured (`acme/no-debugger`) and
its `severity`, 2 for an error and 1 for a warning, as ESLint's JSON reports it.

## Performance

A rule authored in TypeScript trades speed for authoring convenience, and the
cost is the boundary, not the rule logic. `node source/fastlint/plugin/ts/bench.ts
<addon-or-module path>` measures it.

- The bare walk wraps each node and reads its kind and children back across the
  host boundary. It is about 3 microseconds per node, and it dominates. A native
  rule pays a dispatch-map lookup per node instead, in nanoseconds.
- Rule count barely matters; how much each rule inspects does, because every
  accessor a visitor reads is another boundary crossing. Five inspecting rules
  take roughly 0.4 s under N-API where the C++ front end takes 10 ms.
- Keep a hot visitor's per-node work small, and prefer `node.is` plus a field
  read over a broad `descendants` sweep where a narrow visitor does. The
  standing cost to cut is the per-node handle itself (docs/embedding.md).
