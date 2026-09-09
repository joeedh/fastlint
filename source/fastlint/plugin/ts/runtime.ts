// The TypeScript rule runtime for the N-API embedding (task 7.2). It turns the
// addon's accessor exports into the `Host` the generated views read through,
// walks a parsed file once, and dispatches each node to the rules that asked
// for its kind. Rules are ESLint-shaped: `create(context)` returns visitors
// keyed by node-kind name, and `context.report` collects a problem.
//
// It runs the tuples a config resolves to (task 8.2), not a bare rule list, so
// a rule reads its own options and every problem carries the severity the config
// gave the rule.
//
// Only syntactic rules run here: an embedding has no tsgo process, so there is
// no type information (docs/embedding.md).

import { NodeKind, kindNames, wrap } from "../generated/ts/views.ts";
import type { Handle, Host, Node } from "../generated/ts/views.ts";

/** The subset of the addon this runtime calls. `parse` owns the tree; the rest
 * read a node handle it produced. */
export interface Addon {
  parse(source: string, filename?: string): Handle;
  root(session: Handle): Handle;
  kind(handle: Handle): number;
  flags(handle: Handle): number;
  parent(handle: Handle): Handle | null;
  childCount(handle: Handle): number;
  child(handle: Handle, index: number): Handle | null;
  text(handle: Handle): string;
  dataByte(handle: Handle, byte: number): number;
  start(handle: Handle): number;
  end(handle: Handle): number;
  descendants(session: Handle, handle: Handle, kind: number): Handle[];
  /** Releases a session's tree. The WASM heap has no finalizer, so its addon
   * supplies this; the N-API addon omits it and lets GC reclaim the session. */
  freeSession?(session: Handle): void;
  /** The built-in rules in one native call, returning the `--format json`
   * output. `config` is a `fastlint.config.json` document whose globs are
   * anchored at `baseDir`; without one the recommended preset applies. Both
   * embeddings expose it, and the CLI lints through it when no native binary is
   * around; the rule runtime itself does not need it. */
  lintText?(
    source: string,
    filename?: string,
    config?: string,
    baseDir?: string
  ): string;
}

/** A reported problem, in the shape a rule hands to `context.report`. */
export interface ReportDescriptor {
  node: Node;
  messageId?: string;
  message?: string;
  data?: Record<string, string>;
}

/** What a rule sees while visiting. One context is built per file. */
export interface RuleContext {
  readonly filename: string;
  readonly sourceText: string;
  /** The elements the config listed after the severity, empty for a bare one. */
  readonly options: readonly unknown[];
  report(descriptor: ReportDescriptor): void;
}

/** A visitor per node-kind name, the keys of `NodeKind`. */
export type Visitors = {
  readonly [K in keyof typeof NodeKind]?: (node: Node) => void;
};

export interface Rule {
  readonly name: string;
  /** The message templates, looked up by `messageId`; `{{name}}` is filled from
   * `report`'s `data`. Optional when a rule always passes `message`. */
  readonly messages?: Record<string, string>;
  create(context: RuleContext): Visitors;
}

/** A rule as the config resolved it, which is what the runtime runs. */
export interface ResolvedRule {
  /** The configured name, `prefix/rule`, reported on every problem. */
  readonly id: string;
  readonly rule: Rule;
  severity: "off" | "warn" | "error";
  options: readonly unknown[];
}

/** `rule` under its own name, for a caller that runs a rule without a config. */
export function resolveRule(
  rule: Rule,
  severity: "warn" | "error" = "error",
  options: readonly unknown[] = []
): ResolvedRule {
  return { id: rule.name, rule, severity, options };
}

/** One problem in the flat list `lint` returns. */
export interface LintMessage {
  ruleId: string;
  /** 2 for an error and 1 for a warning, as ESLint's JSON reports it. */
  severity: 1 | 2;
  message: string;
  line: number;
  column: number;
  endLine: number;
  endColumn: number;
  nodeType: string;
}

/** Builds the `Host` the generated views read through, bound to one session. */
function hostFor(addon: Addon, session: Handle): Host {
  return {
    kind: (h) => addon.kind(h) as NodeKind,
    flags: (h) => addon.flags(h),
    parent: (h) => addon.parent(h),
    childCount: (h) => addon.childCount(h),
    child: (h, i) => addon.child(h, i),
    text: (h) => addon.text(h),
    dataByte: (h, b) => addon.dataByte(h, b),
    start: (h) => addon.start(h),
    end: (h) => addon.end(h),
    descendants: (h, kind) => addon.descendants(session, h, kind),
  };
}

/** Fills `{{name}}` placeholders in a message template from `data`. */
function format(template: string, data: Record<string, string> | undefined): string {
  if (!data) return template;
  return template.replace(/\{\{\s*(\w+)\s*\}\}/g, (whole, key: string) =>
    key in data ? data[key]! : whole
  );
}

/** 1-based line and column of a byte offset, counting UTF-16 columns as editors
 * and ESLint's JSON do. Columns past a multibyte character still land right,
 * since the walk is over the same source the offsets index. */
function position(source: string, offset: number): { line: number; column: number } {
  let line = 1;
  let lineStart = 0;
  for (let i = 0; i < offset && i < source.length; i++) {
    if (source.charCodeAt(i) === 10) {
      line++;
      lineStart = i + 1;
    }
  }
  return { line, column: offset - lineStart + 1 };
}

/**
 * Lints one buffer with `rules` and returns the flat problem list. The file is
 * parsed once; each node is dispatched to every rule that registered a visitor
 * for its kind, in rule order. A rule the config set to `off` is not run.
 */
export function lint(
  addon: Addon,
  source: string,
  filename: string,
  rules: readonly ResolvedRule[]
): LintMessage[] {
  const session = addon.parse(source, filename);
  const host = hostFor(addon, session);
  const messages: LintMessage[] = [];
  // A visitor list per kind value, so dispatch is one array lookup per node.
  const byKind = new Map<number, { rule: Rule; visit: (node: Node) => void }[]>();

  for (const resolved of rules) {
    if (resolved.severity === "off") continue;
    const rule = resolved.rule;
    const severity: 1 | 2 = resolved.severity === "error" ? 2 : 1;
    const context: RuleContext = {
      filename,
      sourceText: source,
      options: resolved.options,
      report(descriptor) {
        const template =
          descriptor.message ??
          (descriptor.messageId ? rule.messages?.[descriptor.messageId] : undefined) ??
          descriptor.messageId ??
          "";
        const [start, end] = descriptor.node.range;
        const from = position(source, start);
        const to = position(source, end);
        messages.push({
          ruleId: resolved.id,
          severity,
          message: format(template, descriptor.data),
          line: from.line,
          column: from.column,
          endLine: to.line,
          endColumn: to.column,
          nodeType: kindNames[descriptor.node.type],
        });
      },
    };
    const visitors = rule.create(context);
    for (const key of Object.keys(visitors)) {
      const kind = NodeKind[key as keyof typeof NodeKind];
      const visit = visitors[key as keyof typeof NodeKind];
      if (visit === undefined) continue;
      const list = byKind.get(kind) ?? [];
      list.push({ rule, visit });
      byKind.set(kind, list);
    }
  }

  const walk = (node: Node): void => {
    const list = byKind.get(node.type);
    if (list) {
      for (const { visit } of list) visit(node);
    }
    const count = node.childCount;
    for (let i = 0; i < count; i++) {
      const child = node.child(i);
      if (child) walk(child);
    }
  };
  try {
    walk(wrap(host, addon.root(session)));
  } finally {
    if (addon.freeSession) addon.freeSession(session);
  }

  return messages;
}
