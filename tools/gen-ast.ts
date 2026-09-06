// Reads source/fastlint/ast/nodes.def and writes source/fastlint/ast/generated/.
// `node make.ts gen-ast` runs it; `--check` reports files that would change.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export interface Child {
  name: string;
  optional: boolean;
  list: boolean;
  nullableElements: boolean;
}

export type Field =
  | { kind: "flag"; name: string; bit: number }
  | { kind: "text"; name: string }
  | { kind: "enum"; name: string; enumName: string; byte: number };

export interface NodeDef {
  name: string;
  children: Child[];
  fields: Field[];
}

export interface EnumDef {
  name: string;
  values: string[];
}

export interface UnionDef {
  name: string;
  members: string[];
}

/** A syntactic position kinds may fill; one bit of KindInfo::categories. */
export interface CategoryDef {
  name: string;
  members: string[];
}

export interface Def {
  flags: string[];
  enums: EnumDef[];
  nodes: NodeDef[];
  unions: UnionDef[];
  categories: CategoryDef[];
  hash: string;
}

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
export const defPath = path.join(repoRoot, "source", "fastlint", "ast", "nodes.def");
export const outDir = path.join(repoRoot, "source", "fastlint", "ast", "generated");

const maxFlags = 32;
const maxEnumFields = 4;
const maxEnumValues = 256;
const maxCategories = 8;

/** Accessor names that are C++ keywords, and what the view calls them instead. */
const cppRenames: Record<string, string> = {
  default  : "defaultType",
  namespace: "namespaceName",
  operator : "op",
};

export function pascal(name: string): string {
  return name.charAt(0).toUpperCase() + name.slice(1);
}

function fnv1a(text: string): string {
  let hash = 0x811c9dc5;
  for (let i = 0; i < text.length; i++) {
    hash ^= text.charCodeAt(i);
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash.toString(16).padStart(8, "0");
}

export function parseDef(text: string): Def {
  // The hash ignores line endings so a CRLF checkout matches the LF one.
  const def: Def = {
    flags     : [],
    enums     : [],
    nodes     : [],
    unions    : [],
    categories: [],
    hash      : fnv1a(text.replace(/\r\n/g, "\n")),
  };
  const errors: string[] = [];
  const lines = text.split(/\r?\n/);

  lines.forEach((raw, index) => {
    const line = raw.trim();
    if (line === "" || line.startsWith("#")) return;
    const fail = (message: string): void => {
      errors.push(`nodes.def:${index + 1}: ${message}`);
    };
    const words = line.split(/\s+/);
    const keyword = words[0] ?? "";
    const rest = words.slice(1);

    if (keyword === "flags") {
      def.flags.push(...rest);
    } else if (keyword === "enum") {
      const [name, ...values] = rest;
      if (!name || values.length === 0) return fail("enum needs a name and values");
      if (values.length > maxEnumValues) return fail(`enum ${name} has too many values`);
      def.enums.push({ name, values });
    } else if (keyword === "union") {
      const [name, ...members] = rest;
      if (!name || members.length === 0) return fail("union needs a name and members");
      def.unions.push({ name, members });
    } else if (keyword === "category") {
      const [name, ...members] = rest;
      if (!name || members.length === 0) return fail("category needs a name and members");
      if (def.categories.length >= maxCategories) return fail("too many categories");
      def.categories.push({ name, members });
    } else if (keyword === "node") {
      const [name] = rest;
      if (!name) return fail("node needs a name");
      const colon = rest.indexOf(":");
      const childWords = colon < 0 ? rest.slice(1) : rest.slice(1, colon);
      const fieldWords = colon < 0 ? [] : rest.slice(colon + 1);

      const children: Child[] = [];
      for (const word of childWords) {
        const list = word.includes("...");
        const bare = word.replace("...", "").replace("?", "");
        const optional = !list && word.endsWith("?");
        const nullableElements = list && word.endsWith("?");
        if (children.some((c) => c.list)) fail(`${name}: the list child must be last`);
        children.push({ name: bare, optional, list, nullableElements });
      }

      const fields: Field[] = [];
      let enumBytes = 0;
      for (const word of fieldWords) {
        const eq = word.indexOf("=");
        if (eq >= 0) {
          const fieldName = word.slice(0, eq);
          const enumName = word.slice(eq + 1);
          if (enumBytes >= maxEnumFields) fail(`${name}: too many enum fields`);
          fields.push({ kind: "enum", name: fieldName, enumName, byte: enumBytes++ });
        } else if (word === "text") {
          fields.push({ kind: "text", name: "text" });
        } else {
          fields.push({ kind: "flag", name: word, bit: -1 });
        }
      }
      def.nodes.push({ name, children, fields });
    } else {
      fail(`unknown keyword ${keyword}`);
    }
  });

  if (def.flags.length > maxFlags) errors.push(`more than ${maxFlags} flags`);

  const seen = new Set<string>();
  for (const node of def.nodes) {
    if (seen.has(node.name)) errors.push(`duplicate node ${node.name}`);
    seen.add(node.name);
    const names = new Set<string>();
    for (const item of [...node.children, ...node.fields]) {
      if (names.has(item.name))
        errors.push(`${node.name}: duplicate member ${item.name}`);
      names.add(item.name);
    }
    for (const field of node.fields) {
      if (field.kind === "flag") {
        field.bit = def.flags.indexOf(field.name);
        if (field.bit < 0) errors.push(`${node.name}: undeclared flag ${field.name}`);
      } else if (field.kind === "enum") {
        if (!def.enums.some((e) => e.name === field.enumName)) {
          errors.push(`${node.name}: undeclared enum ${field.enumName}`);
        }
      }
    }
  }
  for (const union of def.unions) {
    for (const member of union.members) {
      if (!seen.has(member)) errors.push(`union ${union.name}: unknown kind ${member}`);
    }
  }
  for (const category of def.categories) {
    for (const member of category.members) {
      if (!seen.has(member))
        errors.push(`category ${category.name}: unknown kind ${member}`);
    }
  }
  if (errors.length > 0) throw new Error(errors.join("\n"));
  return def;
}

/** A declaration with the pointer star hugging the name. */
function decl(type: string, name: string): string {
  return type.endsWith("*") ? `${type}${name}` : `${type} ${name}`;
}

function accessorName(name: string): string {
  return cppRenames[name] ?? name;
}

const banner = (what: string): string =>
  `// ${what}. Generated from source/fastlint/ast/nodes.def by\n` +
  `// tools/gen-ast.ts; do not edit. \`node make.ts gen-ast\` regenerates it.\n` +
  `// clang-format off\n`;

export function emitKinds(def: Def): string {
  const out: string[] = [
    banner("AST node kinds, flags and field enums"),
    "#pragma once",
    "",
    "#include <cstdint>",
    "",
    "namespace fastlint::ast {",
    "",
  ];

  out.push("enum class NodeKind : uint16_t {");
  for (const node of def.nodes) out.push(`  ${node.name},`);
  out.push("};", "", `constexpr int kindCount = ${def.nodes.length};`, "");

  out.push("/** Bits of Node::flags. */", "enum class Flag : uint32_t {");
  def.flags.forEach((flag, bit) => out.push(`  ${pascal(flag)} = 1u << ${bit},`));
  out.push("};", "", `constexpr int flagCount = ${def.flags.length};`, "");

  out.push("/** Bits of KindInfo::categories. */", "enum class Category : uint8_t {");
  def.categories.forEach((c, bit) => out.push(`  ${c.name} = 1u << ${bit},`));
  out.push("};", "", `constexpr int categoryCount = ${def.categories.length};`, "");

  for (const e of def.enums) {
    out.push(`enum class ${e.name} : uint8_t {`);
    for (const value of e.values) out.push(`  ${pascal(value)},`);
    out.push("};", "");
  }

  out.push(`/** FNV-1a of nodes.def; plugins refuse a host with a different value. */`);
  out.push(`constexpr uint32_t nodesDefHash = 0x${def.hash}u;`, "");
  out.push("} // namespace fastlint::ast", "");
  return out.join("\n");
}

interface Member {
  name: string;
  /** C++ return type of the accessor. */
  type: string;
  /** Accessor body for a given node kind, without the return. */
  expr: (node: NodeDef) => string | undefined;
}

function childIndex(node: NodeDef, name: string): number {
  return node.children.findIndex((c) => c.name === name);
}

function memberFor(node: NodeDef, name: string): Member | undefined {
  const index = childIndex(node, name);
  if (index >= 0) {
    const child = node.children[index];
    if (!child) return undefined;
    if (child.list) {
      return {
        name,
        type: "span<Node *>",
        expr: (n) => `access::tail(n, ${childIndex(n, name)})`,
      };
    }
    return {
      name,
      type: "Node *",
      expr: (n) => `access::child(n, ${childIndex(n, name)})`,
    };
  }
  const field = node.fields.find((f) => f.name === name);
  if (!field) return undefined;
  if (field.kind === "flag") {
    return {
      name,
      type: "bool",
      expr: () => `access::hasFlag(n, Flag::${pascal(field.name)})`,
    };
  }
  if (field.kind === "text") {
    return { name, type: "string_view", expr: () => "access::text(n)" };
  }
  return {
    name,
    type: field.enumName,
    expr: (n) => {
      const f = n.fields.find((x) => x.name === name);
      return f && f.kind === "enum"
        ? `${field.enumName}(access::dataByte(n, ${f.byte}))`
        : undefined;
    },
  };
}

function emitView(def: Def, node: NodeDef, out: string[]): void {
  const fixed = node.children.filter((c) => !c.list).length;
  const list = node.children.find((c) => c.list);
  out.push(`struct ${node.name} : View {`);
  out.push(`  using View::View;`);
  out.push(`  static constexpr NodeKind nodeKind = NodeKind::${node.name};`);
  out.push(`  static constexpr int fixedChildren = ${fixed};`);
  out.push(`  static constexpr bool hasList = ${list ? "true" : "false"};`);
  out.push(
    `  static constexpr bool matches(NodeKind k) { return k == NodeKind::${node.name}; }`
  );
  for (const child of node.children) {
    const member = memberFor(node, child.name);
    if (!member) continue;
    out.push(
      `  ${decl(member.type, accessorName(child.name))}() const { return ${member.expr(node)}; }`
    );
  }
  for (const field of node.fields) {
    const member = memberFor(node, field.name);
    if (!member) continue;
    const name =
      field.kind === "flag" ? `is${pascal(field.name)}` : accessorName(field.name);
    out.push(`  ${decl(member.type, name)}() const { return ${member.expr(node)}; }`);
  }
  out.push("};", "");
}

function emitUnion(def: Def, union: UnionDef, out: string[]): void {
  const members = union.members
    .map((m) => def.nodes.find((n) => n.name === m))
    .filter((n): n is NodeDef => !!n);
  const first = members[0];
  if (!first) return;
  out.push(`struct ${union.name} : View {`);
  out.push(`  using View::View;`);
  out.push(`  static constexpr bool matches(NodeKind k) {`);
  out.push(`    return ${members.map((m) => `k == NodeKind::${m.name}`).join(" || ")};`);
  out.push(`  }`);
  const names = [
    ...first.children.map((c) => c.name),
    ...first.fields.map((f) => f.name),
  ];
  for (const name of names) {
    const shapes = members.map((m) => memberFor(m, name));
    if (shapes.some((s) => !s)) continue;
    const typed = shapes as Member[];
    const type = typed[0]?.type ?? "";
    if (typed.some((s) => s.type !== type)) continue;
    const bodies = members.map((m, i) => typed[i]?.expr(m));
    if (bodies.some((b) => b === undefined)) continue;
    const isFlag = first.fields.find((f) => f.name === name)?.kind === "flag";
    const accessor = isFlag ? `is${pascal(name)}` : accessorName(name);
    const same = bodies.every((b) => b === bodies[0]);
    if (same) {
      out.push(`  ${decl(type, accessor)}() const { return ${bodies[0]}; }`);
      continue;
    }
    out.push(`  ${decl(type, accessor)}() const {`);
    out.push(`    switch (access::kind(n)) {`);
    members.forEach((m, i) => {
      out.push(`      case NodeKind::${m.name}: return ${bodies[i]};`);
    });
    out.push(`      default: return {};`);
    out.push(`    }`);
    out.push(`  }`);
  }
  out.push("};", "");
}

export function emitViews(def: Def): string {
  const out: string[] = [
    banner("Typed views over Node"),
    "#pragma once",
    "",
    '#include "fastlint/ast/access.h"',
    '#include "fastlint/ast/generated/kinds.h"',
    "",
    "namespace fastlint::ast {",
    "",
  ];
  for (const node of def.nodes) emitView(def, node, out);
  for (const union of def.unions) emitUnion(def, union, out);
  out.push("} // namespace fastlint::ast", "");
  return out.join("\n");
}

export function emitTables(def: Def): string {
  const out: string[] = [
    banner("Name and layout tables"),
    '#include "fastlint/ast/kind_info.h"',
    "",
    "namespace fastlint::ast {",
    "",
    "namespace {",
    "",
  ];
  for (const e of def.enums) {
    out.push(
      `const char *const ${e.name}Names[] = {${e.values.map((v) => `"${v}"`).join(", ")}};`
    );
  }
  out.push("");
  for (const node of def.nodes) {
    if (node.children.length > 0) {
      out.push(
        `const char *const ${node.name}Children[] = {${node.children.map((c) => `"${c.name}"`).join(", ")}};`
      );
    }
    const enums = node.fields.filter(
      (f): f is Extract<Field, { kind: "enum" }> => f.kind === "enum"
    );
    if (enums.length > 0) {
      const items = enums.map((f) => {
        const e = def.enums.find((x) => x.name === f.enumName);
        return `{"${f.name}", ${f.enumName}Names, ${e?.values.length ?? 0}}`;
      });
      out.push(`const EnumField ${node.name}Enums[] = {${items.join(", ")}};`);
    }
  }
  out.push("", "const KindInfo kinds[] = {");
  for (const node of def.nodes) {
    const fixed = node.children.filter((c) => !c.list).length;
    const list = node.children.find((c) => c.list);
    const flags = node.fields
      .filter((f) => f.kind === "flag")
      .map((f) => `uint32_t(Flag::${pascal(f.name)})`);
    const enums = node.fields.filter((f) => f.kind === "enum").length;
    const usesText = node.fields.some((f) => f.kind === "text");
    let requiredMask = 0;
    node.children.forEach((c, i) => {
      if (!c.list && !c.optional) requiredMask |= 1 << i;
    });
    const categories = def.categories
      .filter((c) => c.members.includes(node.name))
      .map((c) => `uint8_t(Category::${c.name})`);
    out.push(
      `  {"${node.name}", ${fixed}, ${list ? "true" : "false"}, ${list?.nullableElements ? "true" : "false"}, ` +
        `${usesText ? "true" : "false"}, ${node.children.length > 0 ? `${node.name}Children` : "nullptr"}, ` +
        `${node.children.length}, ${enums > 0 ? `${node.name}Enums` : "nullptr"}, ${enums}, ` +
        `${flags.length > 0 ? flags.join(" | ") : "0"}, ${requiredMask}u, ` +
        `${categories.length > 0 ? categories.join(" | ") : "0"}},`
    );
  }
  out.push("};", "");
  out.push(
    `const char *const flagNames[] = {${def.flags.map((f) => `"${f}"`).join(", ")}};`
  );
  out.push("", "} // namespace", "");
  out.push(
    "const KindInfo &kindInfo(NodeKind kind)",
    "{",
    "  return kinds[int(kind)];",
    "}",
    ""
  );
  out.push(
    "const char *kindName(NodeKind kind)",
    "{",
    "  return kinds[int(kind)].name;",
    "}",
    ""
  );
  out.push(
    "const char *flagName(int bit)",
    "{",
    '  return bit >= 0 && bit < flagCount ? flagNames[bit] : "";',
    "}",
    ""
  );
  out.push("} // namespace fastlint::ast", "");
  return out.join("\n");
}

export interface GenerateResult {
  /** Files written, or (in check mode) files that would change. */
  changed: string[];
}

export function generate(check: boolean): GenerateResult {
  const def = parseDef(fs.readFileSync(defPath, "utf8"));
  const files: Record<string, string> = {
    "kinds.h"  : emitKinds(def),
    "views.h"  : emitViews(def),
    "tables.cc": emitTables(def),
  };
  const changed: string[] = [];
  fs.mkdirSync(outDir, { recursive: true });
  for (const [name, content] of Object.entries(files)) {
    const file = path.join(outDir, name);
    const current = fs.existsSync(file) ? fs.readFileSync(file, "utf8") : undefined;
    if (current === content) continue;
    changed.push(path.relative(repoRoot, file));
    if (!check) fs.writeFileSync(file, content);
  }
  return { changed };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const check = process.argv.includes("--check");
  const result = generate(check);
  for (const file of result.changed) console.log(`${check ? "stale" : "wrote"} ${file}`);
  if (check && result.changed.length > 0) process.exit(1);
}
