// Folds known, intentional shape differences between tsgo's tree and the
// grammar tree so the diff reports only real disagreements. Every rule here
// is a documented divergence; a rule that hides a parser bug is a bug.

import type { TreeNode } from "./sexp.ts";

/** tsgo keyword nodes that are real nodes for us, under our name. */
const keywordNodes: Record<string, string> = {
  ThisKeyword     : "ThisExpression",
  SuperKeyword    : "SuperExpression",
  NullKeyword     : "NullLiteral",
  TrueKeyword     : "TrueLiteral",
  FalseKeyword    : "FalseLiteral",
  AnyKeyword      : "KeywordType",
  UnknownKeyword  : "KeywordType",
  NumberKeyword   : "KeywordType",
  BigIntKeyword   : "KeywordType",
  ObjectKeyword   : "KeywordType",
  BooleanKeyword  : "KeywordType",
  StringKeyword   : "KeywordType",
  SymbolKeyword   : "KeywordType",
  VoidKeyword     : "KeywordType",
  UndefinedKeyword: "KeywordType",
  NeverKeyword    : "KeywordType",
  IntrinsicKeyword: "KeywordType",
};

/** Our kinds that tsgo spells differently. */
const ourRenames: Record<string, string> = {
  OptionalCallExpression: "CallExpression",
  ConstructorNode       : "Constructor",
  GetAccessorSignature  : "GetAccessor",
  SetAccessorSignature  : "SetAccessor",
};

/** Kinds tsgo emits as children that we store as flags or tokens. */
function tsgoDrops(kind: string): boolean {
  if (kind === "SyntaxList" || kind === "EndOfFile") return true;
  // Template chunks are tokens of the TemplateExpression / TemplateSpan for us.
  if (kind === "TemplateHead" || kind === "TemplateMiddle" || kind === "TemplateTail")
    return true;
  if (kind.endsWith("Token")) return true;
  return kind.endsWith("Keyword") && !(kind in keywordNodes);
}

export function normalizeTsgo(node: TreeNode): TreeNode {
  const children: TreeNode[] = [];
  for (const child of node.children) {
    if (tsgoDrops(child.kind)) continue;
    // The switch body has no wrapper for us; clauses hang off the statement.
    if (node.kind === "SwitchStatement" && child.kind === "CaseBlock") {
      for (const inner of child.children) children.push(normalizeTsgo(inner));
      continue;
    }
    // `-1` as a type: we keep the sign as a token of the LiteralType.
    if (node.kind === "LiteralType" && child.kind === "PrefixUnaryExpression") {
      for (const inner of child.children) children.push(normalizeTsgo(inner));
      continue;
    }
    children.push(normalizeTsgo(child));
  }
  return { ...node, kind: keywordNodes[node.kind] ?? node.kind, children };
}

/** Children of an ExportDeclaration that make it a re-export rather than an assignment. */
const exportClauses = new Set(["NamedExports", "NamespaceExport", "StringLiteral"]);

/** Declarations we wrap in ExportDeclaration where tsgo sets a modifier. */
const exportedDeclarations = new Set([
  "VariableStatement",
  "FunctionDeclaration",
  "ClassDeclaration",
  "InterfaceDeclaration",
  "TypeAliasDeclaration",
  "EnumDeclaration",
  "ModuleDeclaration",
  "ImportEqualsDeclaration",
]);

/**
 * Wrapper nodes whose children splice into the parent: list wrappers the
 * grammar tree keeps for their brackets, and the interface body we store as
 * a TypeLiteral.
 */
function isOurWrapper(parent: TreeNode, child: TreeNode): boolean {
  switch (child.kind) {
    case "TypeParameters":
    case "TypeArguments":
      return true;
    case "OmittedExpression": // the arguments run of a call
      return (
        parent.kind === "CallExpression" ||
        parent.kind === "NewExpression" ||
        parent.kind === "OptionalCallExpression"
      );
    case "TypeLiteral":
      return parent.kind === "InterfaceDeclaration";
    case "ExpressionWithTypeArguments": // `f<T>()`: tsgo hangs the type arguments off the call
      return (
        parent.kind === "CallExpression" ||
        parent.kind === "NewExpression" ||
        parent.kind === "OptionalCallExpression"
      );
    case "ExportDeclaration":
      return (
        child.children.length === 1 && exportedDeclarations.has(child.children[0].kind)
      );
    default:
      return false;
  }
}

/**
 * Children tsgo has no node for: skipped error tokens, a required body that
 * is absent (overload signatures), and the empty clauses of `for (;;)`.
 */
function isOurPlaceholder(parent: TreeNode, child: TreeNode): boolean {
  if (child.kind === "ErrorNode") return true;
  if (child.kind === "Block" && child.missing) return true;
  if (child.kind !== "OmittedExpression") return false;
  // `for (;;)` clauses, and the hole of `[, x]` (an empty BindingElement in tsgo).
  return parent.kind === "ForStatement" || parent.kind === "BindingElement";
}

/**
 * tsgo parses `implements` lists and interface `extends` lists as types: an
 * entry whose expression is an entity name becomes a TypeReference over a
 * QualifiedName, and anything else stays an ExpressionWithTypeArguments. We
 * parse `implements` as types already (a TypeReference inside the entry) and
 * interface `extends` as expressions.
 */
function heritageEntry(node: TreeNode, isInterface: boolean): TreeNode {
  if (node.kind !== "ExpressionWithTypeArguments") return node;
  const [expression, ...rest] = node.children;
  if (expression?.kind === "TypeReference") {
    return { ...expression, children: [...expression.children, ...rest] };
  }
  if (!isInterface) return node;
  const name = entityName(expression);
  if (!name) return node;
  return { ...node, kind: "TypeReference", children: [name, ...rest] };
}

function entityName(node: TreeNode | undefined): TreeNode | undefined {
  if (!node) return undefined;
  if (node.kind === "Identifier") return node;
  if (node.kind !== "PropertyAccessExpression" || node.children.length !== 2)
    return undefined;
  const left = entityName(node.children[0]);
  const right = node.children[1];
  if (!left || right.kind !== "Identifier") return undefined;
  return { ...node, kind: "QualifiedName", children: [left, right] };
}

/** `module A.B.C {}` nests one ModuleDeclaration per name in tsgo. */
function nestModuleNames(node: TreeNode): TreeNode {
  const names = node.children.filter((c) => c.kind === "Identifier");
  const rest = node.children.filter((c) => c.kind !== "Identifier");
  if (names.length < 2) return node;
  let inner: TreeNode = { ...node, children: [names[names.length - 1], ...rest] };
  for (let i = names.length - 2; i >= 0; i--) {
    inner = { ...node, children: [names[i], inner] };
  }
  return inner;
}

/** `A.B.C` is flat for us and left-nested for tsgo. */
function nestQualifiedName(node: TreeNode, parts: TreeNode[]): TreeNode {
  let result = parts[0];
  for (let i = 1; i < parts.length; i++) {
    const right = parts[i];
    result = {
      ...node,
      kind    : "QualifiedName",
      end     : right.end,
      children: [result, right],
    };
  }
  return result;
}

function renameOurs(parent: TreeNode | undefined, node: TreeNode): string {
  if (node.kind === "Block" && parent?.kind === "ModuleDeclaration") return "ModuleBlock";
  // `export default expr` and `export = expr` are ExportAssignment in tsgo.
  if (
    node.kind === "ExportDeclaration" &&
    node.children.length === 1 &&
    !exportedDeclarations.has(node.children[0].kind) &&
    !exportClauses.has(node.children[0].kind)
  ) {
    return "ExportAssignment";
  }
  return ourRenames[node.kind] ?? node.kind;
}

export function normalizeOurs(node: TreeNode, parent?: TreeNode): TreeNode {
  // A one-part QualifiedName is tsgo's bare Identifier.
  if (node.kind === "QualifiedName" && node.children.length === 1) {
    return normalizeOurs(node.children[0], parent);
  }
  const children: TreeNode[] = [];
  let source = node.children;
  // tsgo's Constructor has no name node; we keep the `constructor` identifier.
  if (node.kind === "ConstructorNode" && source[0]?.kind === "Identifier") {
    source = source.slice(1);
  }
  const push = (child: TreeNode) => {
    if (isOurPlaceholder(node, child)) return;
    if (isOurWrapper(node, child)) {
      for (const inner of child.children) push(inner);
    } else {
      children.push(normalizeOurs(child, node));
    }
  };
  for (const child of source) push(child);
  if (node.kind === "HeritageClause") {
    const isInterface = parent?.kind === "InterfaceDeclaration";
    for (let i = 0; i < children.length; i++) {
      children[i] = heritageEntry(children[i], isInterface);
    }
  }
  if (node.kind === "QualifiedName" && children.length > 2) {
    return nestQualifiedName(node, children);
  }
  const result = { ...node, kind: renameOurs(parent, node), children };
  if (node.kind === "ModuleDeclaration") return nestModuleNames(result);
  return result;
}
