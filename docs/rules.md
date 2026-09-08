# Rules

The rule framework: how a rule is defined, how the linter runs every rule in
one walk, how reports become diagnostics, and the surrounding pieces (config,
disable directives, output, the rule tester, the `lint` command). Code lives
in `source/fastlint/lint/`; the rules themselves in `source/fastlint/rules/`.

## Rule interface

A rule is a static `RuleDef` (lint/rule.h): metadata plus a `create`
function.

- `RuleMeta` carries the configured `name` (no plugin prefix), a
  `description`, the `docsUrl`, the `recommended`, `fixable`,
  `hasSuggestions` and `typeAware` flags, and the `messages` table.
- `messages` is a `Message[]` of `{id, text}`; `text` may contain
  `{{name}}` placeholders that a report's data fills in. Rules report by
  message id, never by free text, so the tester and the docs can name them.
- `create(RuleContext &ctx)` runs once per rule per file. It registers
  listeners and returns; everything else happens through the context.
- A rule file defines its `RuleDef` as an `extern const` named in
  rules/rules.h and listed in rules/builtin.cc. `builtinRegistry()` holds
  every built-in rule.

ESLint's shape was kept where it costs nothing (`create`, `messages`,
`fixable`, `hasSuggestions`, `valid`/`invalid` tests) so the semantics of a
rule can be ported by reading the ESLint or typescript-eslint source next to
ours.

## RuleContext

What a rule gets for one file.

- `file()`, `bindings()`, `source()`, `filename()`, `textOf(node)`.
- `option(i)` is the rule's i-th configured option (the array element after
  the severity) as a `JsonValue`, or null. Rules read options with
  `asString()`, `getBool(...)` and so on and supply their own defaults;
  there is no schema validation yet.
- `types()` is the `TypeFacts` for the file, or null when the linter runs
  without a type server. A rule with `typeAware` set is not created at all
  in that case. `LintOptions::types` names a `types::TypeSource`
  (docs/type-facts.md "Type sources"), which the linter asks for the facts of
  each file, and of each fixpoint pass, since a pass lints text the server
  has not seen. A file the source cannot type is linted by the syntactic
  rules alone and `FileResult::typeError` says why.
- `on(kind, fn)`, `onExit(kind, fn)`, `on<View>(fn)`, `onExit<View>(fn)`
  register listeners. `fn` is an owning `function<void(Node *)>`; the
  context keeps it alive for the file. A view form registers on every kind
  the view matches (`on<ast::Function>` for all five function kinds).
- `state<T>(args...)` allocates per-file state the listeners may capture by
  pointer. It is destroyed with the context, after the last listener has
  fired, so exit listeners on `Program` can read the totals.
- `report(...)` takes a `Report` or one of the shorthand forms
  (`node, id`; `node, id, {placeholders}`; `node, id, fix`). The span is the
  node's unless `Report::at(start, end)` gave one.

Listeners must not register further listeners; registration closes when
`create` returns.

### Pattern options

Options that hold regular expressions (`no-fallthrough`'s `commentPattern`,
ignore patterns to come) compile through `lint::Regex` (lint/regex.h), a
small backtracking matcher over bytes: literals, `.`, classes, `\d \w \s
\b`, groups, alternation, greedy and lazy quantifiers, `^` and `$`, and the
`i` flag. A `\uXXXX`, `\u{...}` or `\xXX` escape above ASCII matches the
UTF-8 bytes of its code point, so a literal non-ASCII character works outside
a character class. Lookaround, backreferences, Unicode property classes and a
multi-byte escape inside a character class do not compile; a rule falls back
to its default pattern when `compile` fails.

## Dispatch

`Linter::lintFile` creates every enabled rule's context, then hands each
registered listener to one `ast::Dispatcher` and runs it once over the file's
preorder vector (docs/ast-design.md "Traversal and dispatch"). A file with
fifty enabled rules is walked once. Enter listeners fire in registration
order per kind, rules in config order.

## Reports and diagnostics

A `Report` becomes a `Diagnostic` (lint/linter.h): rule, severity from the
config, byte span, one-based line and column (columns count UTF-16 code units,
as editor protocols and ESLint's JSON do), the message id and the interpolated
text, whether a fix was offered, and any suggestions' interpolated texts. Diagnostics are sorted by
position. A report whose message id the rule does not declare produces an
"Unknown message id" diagnostic rather than a crash, so a typo shows up in
the rule's tests.

## Fixes

`Report::fix` is a `function<void(ast::Fixer &)>`; the report's node is the
fix target. The linter turns each surviving report's fix into an `ast::Fix`
and appends it to the pass's fix list, so the fixpoint driver applies them in
source order with its usual deferral (docs/ast-design.md "Fixpoint
driver"). Fixes of suppressed reports are never applied. Suggestions carry
fixes too but nothing applies them yet; they are meant for an editor.

`Linter::lintSource` with `LintOptions::fix` wraps `lintFile` in
`runToFixpoint`: each pass parses, lowers, binds and lints the current text,
applies the fixes, prints, and repeats until a pass proposes nothing. The
diagnostics reported are those of the final pass, so fixed problems vanish
from the output. If the driver reverted a pass or hit `maxPasses`, the final
text is linted once more without fixing so the diagnostics match it.

## Syntax errors

When the parser reports diagnostics, `lintFile` emits them as fatal
diagnostics (`Parsing error: ...`, no rule) and does not run rules, as ESLint
does. Recovery makes running rules on a broken tree possible; it is off
until a use case asks for it.

## Disable directives

lint/directives.h reads every comment in the grammar tree's trivia.

- `// fastlint-disable-line [rules]`, `// fastlint-disable-next-line
  [rules]`, `/* fastlint-disable [rules] */`, `/* fastlint-enable [rules]
  */`. Rules are comma separated; a ` -- justification` tail is ignored.
- The `eslint-` spellings are accepted as aliases (config
  `eslintDirectives`, default true). This is the answer to the "ESLint
  compatibility surface" question in docs/STRATEGY.md: a project moving
  from ESLint keeps its suppression comments, and names with a
  `@typescript-eslint/` prefix resolve to ours.
- A directive naming a rule the registry does not know is dropped when
  spelled `eslint-` (it belongs to a plugin we do not implement) and
  reported as "Definition for rule 'x' was not found." when spelled
  `fastlint-`.
- A line directive in a block comment that spans lines is an error, as in
  ESLint.
- Block directives replay as a state machine over the diagnostics in source
  order: a blanket `disable` clears the per-rule state; `enable rule` under
  a blanket `disable` re-enables just that rule; the latest per-rule
  `disable` wins over a blanket one.
- Unused `disable` directives are reported at the comment with the severity
  of `reportUnusedDisableDirectives` (default warn), naming the rule when
  the directive did. Unused `enable` directives are not reported.

## Config

`fastlint.config.json` is found by walking up from the working directory
(`Config::find`). Its shape:

```json
{
  "extends": ["fastlint:recommended"],
  "rules": {
    "no-debugger": "error",
    "eqeqeq": ["error", "always"]
  },
  "overrides": [
    { "files": ["**/*.test.ts"], "rules": { "no-debugger": "off" } }
  ],
  "ignores": ["dist/**"],
  "reportUnusedDisableDirectives": "warn",
  "eslintDirectives": true
}
```

- A rule setting is a severity (`"off"`, `"warn"`, `"error"`, or 0 to 2) or
  an array whose first element is the severity and whose remaining elements
  are the rule's options. A bare severity in a later layer keeps the options
  an earlier layer gave.
- `extends` names presets: `fastlint:recommended` (every rule with
  `recommended` set, at error) and `fastlint:all`. Presets fill the base
  layer before `rules`.
- `overrides` apply in order to the files their globs match; `files` may be
  one glob or a list. Globs (lint/glob.h) support `*`, `?`, `**` and
  `{a,b}`, match dotfiles, and are relative to the config file's directory
  with forward slashes.
- `ignores` globs skip files entirely.
- Unknown rule names are not a config error. They are reported once per
  linted file, as ESLint reports them.
- `--rule name:severity` on the command line layers on top of everything.
- A `fastlint.config.ts` is planned; it needs Node to evaluate, so it waits
  for the plugin work in task 7.

`Config::resolve(filename)` yields a `ResolvedConfig`: the rules with their
final severities and settings, the unknown names, the directive settings and
whether the file is ignored.

## Output

lint/format.h has two formatters over `FileResult`s.

- `formatPretty` mirrors ESLint's stylish output: the file path, one
  `line:col  severity  message  rule` row per diagnostic with aligned
  columns, then `N problems (E errors, W warnings)` and the fixable counts.
  ANSI colour when asked.
- `formatJson` mirrors ESLint's JSON formatter: an array of `{filePath,
  messages, errorCount, warningCount, fixableErrorCount,
  fixableWarningCount}` with `output` when `--fix` changed the text. Each
  message has `ruleId`, `severity` (2 error, 1 warning), `message`, `line`,
  `column`, `endLine`, `endColumn`, `messageId`, and `fatal`, `fixable` and
  `suggestions` when set. The `fix` object ESLint emits (a text range and
  replacement) is absent: our fixes are tree edits, not text edits.
- SARIF is not written yet.

## Command line

```
fastlint lint [--config <file>] [--no-config] [--rule <name:severity>]...
              [--project <tsconfig>] [--type-stats] [--fix]
              [--format pretty|json] [--color|--no-color] [--quiet]
              [--max-warnings N] <file|dir>...
```

- With no config file and no `--rule`, the recommended preset applies.
- `--project` starts one `tsc --api` server over the tsconfig and runs the
  type-aware rules; without it they are skipped. A linted file the project
  does not include is typed in the server's inferred project. A file that
  cannot be typed is reported on stderr and gets the syntactic rules only.
- `--type-stats` prints the type queries of the run on stderr: node cache
  hits and misses, nodes without a server counterpart, type, child and
  symbol fetches, and the RPC call and byte counts. It then lists per-rule
  attribution, busiest first: the fetches each rule drove, so a rule's cache
  working set can be measured. A rule's count is the gain in the shared stats
  across its listener, so the per-rule totals sum to the run totals. Rules that
  asked nothing of the type server are omitted.
- `--fix` writes the fixed text back and prints `fixed N problems`.
- `--quiet` drops warnings from the output and the counts.
- Exit code 1 when any error remains (or warnings exceed `--max-warnings`),
  2 on a usage or I/O failure, 0 otherwise.

## Testing a rule

`testing/rule_tester.h` (library `fastlint_rule_tester`) is shaped like
ESLint's `RuleTester`:

```cpp
test::runRuleTests(
    rules::kNoDebugger,
    {
        {"var test = { debugger: 1 }; test.debugger;"},
        {"const debugger_ = 1;", nullptr, "test.js"},
    },
    {
        {"if (foo) debugger", {{"unexpected", 1, 10, 1, 18}}},
        {"debugger;\nfoo();\n", {{"unexpected", 1, 1, 1, 10}}, "foo();\n"},
    });
```

- Each case runs the rule alone at error severity in its own `SUBCASE`.
  `ValidCase` is `{code, options, filename}`; `InvalidCase` adds the expected
  errors and the `output` after `--fix` (null when no fix changes the code).
- `ExpectedError` is `{messageId, line, column, endLine, endColumn,
  message}`; a zero or null field is not checked.
- `options` is a JSON array text whose elements follow the severity.
- The rule's message ids are checked against its `messages` table, so a
  typo in either fails the test.
- Suggestions are not checked; nothing applies them yet.

A type-aware rule uses `runTypedRuleTests` with the same case shapes, from
a test tagged `integration`. It starts one server over a fixture project
named by the trailing `project` argument, `basic` by default, and serves
each case as `src/case.ts` of that project (or `src/<filename>` when the
case names a file), so cases see `strict` and the default library. The
`loose` project adds `noImplicitThis: false` for rules whose `this`
handling depends on it. A JSX case names a `.tsx` file whose stem differs
from `case`, because tsc keeps only the highest-priority extension when a
`.ts` and a `.tsx` share a stem, so `case.tsx` beside `case.ts` is dropped
from the program; the fixtures use `casex.tsx`. The cases run in one loop
rather than subcases, since a subcase replay would restart the server per
case. The test is skipped when no native `tsc` is found.

## Adding a rule

1. Read the ESLint or typescript-eslint source (C:/dev/eslint,
   C:/dev/typescript-eslint) for the semantics, messages and test cases.
2. Add `source/fastlint/rules/<name>.cc` defining `kRuleName`; declare it in
   rules/rules.h, register it in rules/builtin.cc, list the file in
   source/fastlint/CMakeLists.txt.
3. Add `source/tests/rules_<name>_test.cc` using `runRuleTests` (or
   `runTypedRuleTests` under `TEST_TAGGED(..., "integration")` for a
   type-aware rule), ported from the upstream cases, and list it in
   source/tests/CMakeLists.txt.
4. Write `docs/rules/<name>.md`: what it reports, options, fix behaviour,
   and how it differs from the upstream rule.
5. The registry test asserts every built-in rule has messages and a docs
   URL.
