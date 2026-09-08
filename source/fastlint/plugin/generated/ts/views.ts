// TypeScript node views. Generated from source/fastlint/ast/nodes.def by
// tools/gen-ast.ts; do not edit. `node make.ts gen-ast` regenerates it.

export const NodeKind = {
  Program: 0,
  Identifier: 1,
  PrivateIdentifier: 2,
  Literal: 3,
  TemplateLiteral: 4,
  TemplateElement: 5,
  TaggedTemplateExpression: 6,
  ThisExpression: 7,
  Super: 8,
  ArrayExpression: 9,
  ObjectExpression: 10,
  Property: 11,
  SpreadElement: 12,
  MemberExpression: 13,
  CallExpression: 14,
  NewExpression: 15,
  ImportExpression: 16,
  MetaProperty: 17,
  UnaryExpression: 18,
  UpdateExpression: 19,
  BinaryExpression: 20,
  LogicalExpression: 21,
  AssignmentExpression: 22,
  ConditionalExpression: 23,
  SequenceExpression: 24,
  AwaitExpression: 25,
  YieldExpression: 26,
  ArrowFunctionExpression: 27,
  FunctionExpression: 28,
  FunctionDeclaration: 29,
  TSDeclareFunction: 30,
  TSEmptyBodyFunctionExpression: 31,
  ClassDeclaration: 32,
  ClassExpression: 33,
  ClassBody: 34,
  Decorators: 35,
  Decorator: 36,
  MethodDefinition: 37,
  TSAbstractMethodDefinition: 38,
  PropertyDefinition: 39,
  TSAbstractPropertyDefinition: 40,
  AccessorProperty: 41,
  TSAbstractAccessorProperty: 42,
  StaticBlock: 43,
  TSParameterProperty: 44,
  VariableDeclaration: 45,
  VariableDeclarator: 46,
  ObjectPattern: 47,
  ArrayPattern: 48,
  RestElement: 49,
  AssignmentPattern: 50,
  ExpressionStatement: 51,
  BlockStatement: 52,
  EmptyStatement: 53,
  DebuggerStatement: 54,
  IfStatement: 55,
  ForStatement: 56,
  ForInStatement: 57,
  ForOfStatement: 58,
  WhileStatement: 59,
  DoWhileStatement: 60,
  ReturnStatement: 61,
  ThrowStatement: 62,
  BreakStatement: 63,
  ContinueStatement: 64,
  LabeledStatement: 65,
  SwitchStatement: 66,
  SwitchCase: 67,
  TryStatement: 68,
  CatchClause: 69,
  WithStatement: 70,
  ImportDeclaration: 71,
  ImportSpecifier: 72,
  ImportDefaultSpecifier: 73,
  ImportNamespaceSpecifier: 74,
  ImportAttributes: 75,
  ImportAttribute: 76,
  ExportNamedDeclaration: 77,
  ExportSpecifier: 78,
  ExportDefaultDeclaration: 79,
  ExportAllDeclaration: 80,
  JSXElement: 81,
  JSXFragment: 82,
  JSXOpeningElement: 83,
  JSXClosingElement: 84,
  JSXOpeningFragment: 85,
  JSXClosingFragment: 86,
  JSXAttribute: 87,
  JSXSpreadAttribute: 88,
  JSXExpressionContainer: 89,
  JSXEmptyExpression: 90,
  JSXSpreadChild: 91,
  JSXText: 92,
  JSXIdentifier: 93,
  JSXMemberExpression: 94,
  JSXNamespacedName: 95,
  TSAsExpression: 96,
  TSSatisfiesExpression: 97,
  TSNonNullExpression: 98,
  TSTypeAssertion: 99,
  TSInstantiationExpression: 100,
  TSTypeParameterDeclaration: 101,
  TSTypeParameter: 102,
  TSTypeParameterInstantiation: 103,
  TSInterfaceDeclaration: 104,
  TSInterfaceBody: 105,
  TSInterfaceHeritage: 106,
  TSClassImplements: 107,
  TSTypeAliasDeclaration: 108,
  TSEnumDeclaration: 109,
  TSEnumMember: 110,
  TSModuleDeclaration: 111,
  TSModuleBlock: 112,
  TSImportEqualsDeclaration: 113,
  TSExternalModuleReference: 114,
  TSExportAssignment: 115,
  TSNamespaceExportDeclaration: 116,
  TSKeywordType: 117,
  TSThisType: 118,
  TSTypeReference: 119,
  TSQualifiedName: 120,
  TSUnionType: 121,
  TSIntersectionType: 122,
  TSFunctionType: 123,
  TSConstructorType: 124,
  TSConditionalType: 125,
  TSInferType: 126,
  TSMappedType: 127,
  TSIndexedAccessType: 128,
  TSTypeLiteral: 129,
  TSArrayType: 130,
  TSTupleType: 131,
  TSNamedTupleMember: 132,
  TSOptionalType: 133,
  TSRestType: 134,
  TSTypeOperator: 135,
  TSTypeQuery: 136,
  TSTypePredicate: 137,
  TSLiteralType: 138,
  TSTemplateLiteralType: 139,
  TSImportType: 140,
  TSPropertySignature: 141,
  TSMethodSignature: 142,
  TSCallSignatureDeclaration: 143,
  TSConstructSignatureDeclaration: 144,
  TSIndexSignature: 145,
  Error: 146,
} as const;
export type NodeKind = (typeof NodeKind)[keyof typeof NodeKind];

export const kindNames = ["Program", "Identifier", "PrivateIdentifier", "Literal", "TemplateLiteral", "TemplateElement", "TaggedTemplateExpression", "ThisExpression", "Super", "ArrayExpression", "ObjectExpression", "Property", "SpreadElement", "MemberExpression", "CallExpression", "NewExpression", "ImportExpression", "MetaProperty", "UnaryExpression", "UpdateExpression", "BinaryExpression", "LogicalExpression", "AssignmentExpression", "ConditionalExpression", "SequenceExpression", "AwaitExpression", "YieldExpression", "ArrowFunctionExpression", "FunctionExpression", "FunctionDeclaration", "TSDeclareFunction", "TSEmptyBodyFunctionExpression", "ClassDeclaration", "ClassExpression", "ClassBody", "Decorators", "Decorator", "MethodDefinition", "TSAbstractMethodDefinition", "PropertyDefinition", "TSAbstractPropertyDefinition", "AccessorProperty", "TSAbstractAccessorProperty", "StaticBlock", "TSParameterProperty", "VariableDeclaration", "VariableDeclarator", "ObjectPattern", "ArrayPattern", "RestElement", "AssignmentPattern", "ExpressionStatement", "BlockStatement", "EmptyStatement", "DebuggerStatement", "IfStatement", "ForStatement", "ForInStatement", "ForOfStatement", "WhileStatement", "DoWhileStatement", "ReturnStatement", "ThrowStatement", "BreakStatement", "ContinueStatement", "LabeledStatement", "SwitchStatement", "SwitchCase", "TryStatement", "CatchClause", "WithStatement", "ImportDeclaration", "ImportSpecifier", "ImportDefaultSpecifier", "ImportNamespaceSpecifier", "ImportAttributes", "ImportAttribute", "ExportNamedDeclaration", "ExportSpecifier", "ExportDefaultDeclaration", "ExportAllDeclaration", "JSXElement", "JSXFragment", "JSXOpeningElement", "JSXClosingElement", "JSXOpeningFragment", "JSXClosingFragment", "JSXAttribute", "JSXSpreadAttribute", "JSXExpressionContainer", "JSXEmptyExpression", "JSXSpreadChild", "JSXText", "JSXIdentifier", "JSXMemberExpression", "JSXNamespacedName", "TSAsExpression", "TSSatisfiesExpression", "TSNonNullExpression", "TSTypeAssertion", "TSInstantiationExpression", "TSTypeParameterDeclaration", "TSTypeParameter", "TSTypeParameterInstantiation", "TSInterfaceDeclaration", "TSInterfaceBody", "TSInterfaceHeritage", "TSClassImplements", "TSTypeAliasDeclaration", "TSEnumDeclaration", "TSEnumMember", "TSModuleDeclaration", "TSModuleBlock", "TSImportEqualsDeclaration", "TSExternalModuleReference", "TSExportAssignment", "TSNamespaceExportDeclaration", "TSKeywordType", "TSThisType", "TSTypeReference", "TSQualifiedName", "TSUnionType", "TSIntersectionType", "TSFunctionType", "TSConstructorType", "TSConditionalType", "TSInferType", "TSMappedType", "TSIndexedAccessType", "TSTypeLiteral", "TSArrayType", "TSTupleType", "TSNamedTupleMember", "TSOptionalType", "TSRestType", "TSTypeOperator", "TSTypeQuery", "TSTypePredicate", "TSLiteralType", "TSTemplateLiteralType", "TSImportType", "TSPropertySignature", "TSMethodSignature", "TSCallSignatureDeclaration", "TSConstructSignatureDeclaration", "TSIndexSignature", "Error"] as const;

export const Flag = {
  Optional: 1 << 0,
  Computed: 1 << 1,
  Shorthand: 1 << 2,
  Method: 1 << 3,
  Prefix: 1 << 4,
  Delegate: 1 << 5,
  Async: 1 << 6,
  Generator: 1 << 7,
  Expression: 1 << 8,
  Declare: 1 << 9,
  Abstract: 1 << 10,
  Static: 1 << 11,
  Override: 1 << 12,
  Definite: 1 << 13,
  Readonly: 1 << 14,
  Directive: 1 << 15,
  Await: 1 << 16,
  Const: 1 << 17,
  In: 1 << 18,
  Out: 1 << 19,
  Tail: 1 << 20,
  SelfClosing: 1 << 21,
  Asserts: 1 << 22,
  Global: 1 << 23,
  Parenthesized: 1 << 24,
  Incomplete: 1 << 25,
} as const;
export type Flag = (typeof Flag)[keyof typeof Flag];

export const SourceType = {
  Script: 0,
  Module: 1,
} as const;
export type SourceType = (typeof SourceType)[keyof typeof SourceType];

export const LiteralKind = {
  String: 0,
  Number: 1,
  Bigint: 2,
  Boolean: 3,
  Null: 4,
  Regex: 5,
} as const;
export type LiteralKind = (typeof LiteralKind)[keyof typeof LiteralKind];

export const PropertyKind = {
  Init: 0,
  Get: 1,
  Set: 2,
} as const;
export type PropertyKind = (typeof PropertyKind)[keyof typeof PropertyKind];

export const MethodKind = {
  Constructor: 0,
  Method: 1,
  Get: 2,
  Set: 3,
} as const;
export type MethodKind = (typeof MethodKind)[keyof typeof MethodKind];

export const Accessibility = {
  None: 0,
  Public: 1,
  Private: 2,
  Protected: 3,
} as const;
export type Accessibility = (typeof Accessibility)[keyof typeof Accessibility];

export const VariableKind = {
  Var: 0,
  Let: 1,
  Const: 2,
  Using: 3,
  AwaitUsing: 4,
} as const;
export type VariableKind = (typeof VariableKind)[keyof typeof VariableKind];

export const ImportKind = {
  Value: 0,
  Type: 1,
} as const;
export type ImportKind = (typeof ImportKind)[keyof typeof ImportKind];

export const ModuleKind = {
  Module: 0,
  Namespace: 1,
  Global: 2,
} as const;
export type ModuleKind = (typeof ModuleKind)[keyof typeof ModuleKind];

export const Modifier = {
  None: 0,
  Plus: 1,
  Minus: 2,
} as const;
export type Modifier = (typeof Modifier)[keyof typeof Modifier];

export const TypeOperator = {
  Keyof: 0,
  Unique: 1,
  Readonly: 2,
} as const;
export type TypeOperator = (typeof TypeOperator)[keyof typeof TypeOperator];

export const Keyword = {
  Any: 0,
  Bigint: 1,
  Boolean: 2,
  Intrinsic: 3,
  Never: 4,
  Null: 5,
  Number: 6,
  Object: 7,
  String: 8,
  Symbol: 9,
  Undefined: 10,
  Unknown: 11,
  Void: 12,
} as const;
export type Keyword = (typeof Keyword)[keyof typeof Keyword];

export const UnaryOperator = {
  Minus: 0,
  Plus: 1,
  Not: 2,
  BitwiseNot: 3,
  Typeof: 4,
  Void: 5,
  Delete: 6,
} as const;
export type UnaryOperator = (typeof UnaryOperator)[keyof typeof UnaryOperator];

export const UpdateOperator = {
  Increment: 0,
  Decrement: 1,
} as const;
export type UpdateOperator = (typeof UpdateOperator)[keyof typeof UpdateOperator];

export const LogicalOperator = {
  Or: 0,
  And: 1,
  Nullish: 2,
} as const;
export type LogicalOperator = (typeof LogicalOperator)[keyof typeof LogicalOperator];

export const BinaryOperator = {
  Equal: 0,
  NotEqual: 1,
  StrictEqual: 2,
  StrictNotEqual: 3,
  Less: 4,
  LessEqual: 5,
  Greater: 6,
  GreaterEqual: 7,
  ShiftLeft: 8,
  ShiftRight: 9,
  ShiftRightUnsigned: 10,
  Add: 11,
  Subtract: 12,
  Multiply: 13,
  Divide: 14,
  Remainder: 15,
  Exponent: 16,
  BitwiseOr: 17,
  BitwiseXor: 18,
  BitwiseAnd: 19,
  In: 20,
  Instanceof: 21,
} as const;
export type BinaryOperator = (typeof BinaryOperator)[keyof typeof BinaryOperator];

export const AssignmentOperator = {
  Assign: 0,
  AddAssign: 1,
  SubtractAssign: 2,
  MultiplyAssign: 3,
  DivideAssign: 4,
  RemainderAssign: 5,
  ExponentAssign: 6,
  ShiftLeftAssign: 7,
  ShiftRightAssign: 8,
  ShiftRightUnsignedAssign: 9,
  BitwiseOrAssign: 10,
  BitwiseXorAssign: 11,
  BitwiseAndAssign: 12,
  OrAssign: 13,
  AndAssign: 14,
  NullishAssign: 15,
} as const;
export type AssignmentOperator = (typeof AssignmentOperator)[keyof typeof AssignmentOperator];

/** The declared child slots of each kind, in order. */
export const childNames: { readonly [kind: number]: readonly string[] } = {
  0: ["body"],
  1: ["typeAnnotation"],
  4: ["parts"],
  6: ["tag", "typeArguments", "quasi"],
  9: ["elements"],
  10: ["properties"],
  11: ["key", "value"],
  12: ["argument"],
  13: ["object", "property"],
  14: ["callee", "typeArguments", "arguments"],
  15: ["callee", "typeArguments", "arguments"],
  16: ["source", "options"],
  17: ["meta", "property"],
  18: ["argument"],
  19: ["argument"],
  20: ["left", "right"],
  21: ["left", "right"],
  22: ["left", "right"],
  23: ["test", "consequent", "alternate"],
  24: ["expressions"],
  25: ["argument"],
  26: ["argument"],
  27: ["id", "typeParameters", "returnType", "body", "params"],
  28: ["id", "typeParameters", "returnType", "body", "params"],
  29: ["id", "typeParameters", "returnType", "body", "params"],
  30: ["id", "typeParameters", "returnType", "body", "params"],
  31: ["id", "typeParameters", "returnType", "body", "params"],
  32: ["decorators", "id", "typeParameters", "superClass", "superTypeArguments", "body", "implements"],
  33: ["decorators", "id", "typeParameters", "superClass", "superTypeArguments", "body", "implements"],
  34: ["body"],
  35: ["decorators"],
  36: ["expression"],
  37: ["decorators", "key", "value"],
  38: ["decorators", "key", "value"],
  39: ["decorators", "key", "typeAnnotation", "value"],
  40: ["decorators", "key", "typeAnnotation", "value"],
  41: ["decorators", "key", "typeAnnotation", "value"],
  42: ["decorators", "key", "typeAnnotation", "value"],
  43: ["body"],
  44: ["decorators", "parameter"],
  45: ["declarations"],
  46: ["id", "init"],
  47: ["typeAnnotation", "properties"],
  48: ["typeAnnotation", "elements"],
  49: ["argument", "typeAnnotation"],
  50: ["left", "right"],
  51: ["expression"],
  52: ["body"],
  55: ["test", "consequent", "alternate"],
  56: ["init", "test", "update", "body"],
  57: ["left", "right", "body"],
  58: ["left", "right", "body"],
  59: ["test", "body"],
  60: ["body", "test"],
  61: ["argument"],
  62: ["argument"],
  63: ["label"],
  64: ["label"],
  65: ["label", "body"],
  66: ["discriminant", "cases"],
  67: ["test", "consequent"],
  68: ["block", "handler", "finalizer"],
  69: ["param", "body"],
  70: ["object", "body"],
  71: ["source", "attributes", "specifiers"],
  72: ["imported", "local"],
  73: ["local"],
  74: ["local"],
  75: ["attributes"],
  76: ["key", "value"],
  77: ["declaration", "source", "attributes", "specifiers"],
  78: ["local", "exported"],
  79: ["declaration"],
  80: ["exported", "source", "attributes"],
  81: ["openingElement", "closingElement", "children"],
  82: ["openingFragment", "closingFragment", "children"],
  83: ["name", "typeArguments", "attributes"],
  84: ["name"],
  87: ["name", "value"],
  88: ["argument"],
  89: ["expression"],
  91: ["expression"],
  94: ["object", "property"],
  95: ["namespace", "name"],
  96: ["expression", "typeAnnotation"],
  97: ["expression", "typeAnnotation"],
  98: ["expression"],
  99: ["typeAnnotation", "expression"],
  100: ["expression", "typeArguments"],
  101: ["params"],
  102: ["constraint", "default"],
  103: ["params"],
  104: ["id", "typeParameters", "body", "extends"],
  105: ["body"],
  106: ["expression", "typeArguments"],
  107: ["expression", "typeArguments"],
  108: ["id", "typeParameters", "typeAnnotation"],
  109: ["id", "members"],
  110: ["id", "initializer"],
  111: ["id", "body"],
  112: ["body"],
  113: ["id", "moduleReference"],
  114: ["expression"],
  115: ["expression"],
  116: ["id"],
  119: ["typeName", "typeArguments"],
  120: ["left", "right"],
  121: ["types"],
  122: ["types"],
  123: ["typeParameters", "returnType", "params"],
  124: ["typeParameters", "returnType", "params"],
  125: ["checkType", "extendsType", "trueType", "falseType"],
  126: ["typeParameter"],
  127: ["typeParameter", "nameType", "typeAnnotation"],
  128: ["objectType", "indexType"],
  129: ["members"],
  130: ["elementType"],
  131: ["elementTypes"],
  132: ["label", "elementType"],
  133: ["typeAnnotation"],
  134: ["typeAnnotation"],
  135: ["typeAnnotation"],
  136: ["exprName", "typeArguments"],
  137: ["parameterName", "typeAnnotation"],
  138: ["literal"],
  139: ["parts"],
  140: ["argument", "qualifier", "typeArguments"],
  141: ["key", "typeAnnotation"],
  142: ["key", "typeParameters", "returnType", "params"],
  143: ["typeParameters", "returnType", "params"],
  144: ["typeParameters", "returnType", "params"],
  145: ["typeAnnotation", "parameters"],
};

/** A node handle. Accessors read the host tree; a rule never holds the layout. */
export interface Node {
  /** The node kind, the discriminant every view narrows. */
  readonly type: NodeKind;
  readonly flags: number;
  readonly parent: Node | null;
  readonly childCount: number;
  child(index: number): Node | null;
  readonly text: string;
  /** Byte offsets `[start, end)` into the source, for reports. */
  readonly range: readonly [number, number];
  hasFlag(flag: Flag): boolean;
  is<K extends NodeKind>(kind: K): this is KindNode<K>;
  /** Every descendant of `kind` in preorder, as one typed array. */
  descendants<K extends NodeKind>(kind: K): readonly KindNode<K>[];
}

export interface Program extends Node {
  readonly type: typeof NodeKind.Program;
  readonly body: readonly Node[];
  readonly sourceType: SourceType;
}

export interface Identifier extends Node {
  readonly type: typeof NodeKind.Identifier;
  readonly typeAnnotation: Node | null;
  readonly isOptional: boolean;
}

export interface PrivateIdentifier extends Node {
  readonly type: typeof NodeKind.PrivateIdentifier;
}

export interface Literal extends Node {
  readonly type: typeof NodeKind.Literal;
  readonly literalKind: LiteralKind;
}

export interface TemplateLiteral extends Node {
  readonly type: typeof NodeKind.TemplateLiteral;
  readonly parts: readonly Node[];
}

export interface TemplateElement extends Node {
  readonly type: typeof NodeKind.TemplateElement;
  readonly isTail: boolean;
}

export interface TaggedTemplateExpression extends Node {
  readonly type: typeof NodeKind.TaggedTemplateExpression;
  readonly tag: Node;
  readonly typeArguments: Node | null;
  readonly quasi: Node;
}

export interface ThisExpression extends Node {
  readonly type: typeof NodeKind.ThisExpression;
}

export interface Super extends Node {
  readonly type: typeof NodeKind.Super;
}

export interface ArrayExpression extends Node {
  readonly type: typeof NodeKind.ArrayExpression;
  readonly elements: readonly (Node | null)[];
}

export interface ObjectExpression extends Node {
  readonly type: typeof NodeKind.ObjectExpression;
  readonly properties: readonly Node[];
}

export interface Property extends Node {
  readonly type: typeof NodeKind.Property;
  readonly key: Node;
  readonly value: Node;
  readonly kind: PropertyKind;
  readonly isComputed: boolean;
  readonly isShorthand: boolean;
  readonly isMethod: boolean;
}

export interface SpreadElement extends Node {
  readonly type: typeof NodeKind.SpreadElement;
  readonly argument: Node;
}

export interface MemberExpression extends Node {
  readonly type: typeof NodeKind.MemberExpression;
  readonly object: Node;
  readonly property: Node;
  readonly isComputed: boolean;
  readonly isOptional: boolean;
}

export interface CallExpression extends Node {
  readonly type: typeof NodeKind.CallExpression;
  readonly callee: Node;
  readonly typeArguments: Node | null;
  readonly arguments: readonly Node[];
  readonly isOptional: boolean;
}

export interface NewExpression extends Node {
  readonly type: typeof NodeKind.NewExpression;
  readonly callee: Node;
  readonly typeArguments: Node | null;
  readonly arguments: readonly Node[];
}

export interface ImportExpression extends Node {
  readonly type: typeof NodeKind.ImportExpression;
  readonly source: Node;
  readonly options: Node | null;
}

export interface MetaProperty extends Node {
  readonly type: typeof NodeKind.MetaProperty;
  readonly meta: Node;
  readonly property: Node;
}

export interface UnaryExpression extends Node {
  readonly type: typeof NodeKind.UnaryExpression;
  readonly argument: Node;
  readonly op: UnaryOperator;
}

export interface UpdateExpression extends Node {
  readonly type: typeof NodeKind.UpdateExpression;
  readonly argument: Node;
  readonly op: UpdateOperator;
  readonly isPrefix: boolean;
}

export interface BinaryExpression extends Node {
  readonly type: typeof NodeKind.BinaryExpression;
  readonly left: Node;
  readonly right: Node;
  readonly op: BinaryOperator;
}

export interface LogicalExpression extends Node {
  readonly type: typeof NodeKind.LogicalExpression;
  readonly left: Node;
  readonly right: Node;
  readonly op: LogicalOperator;
}

export interface AssignmentExpression extends Node {
  readonly type: typeof NodeKind.AssignmentExpression;
  readonly left: Node;
  readonly right: Node;
  readonly op: AssignmentOperator;
}

export interface ConditionalExpression extends Node {
  readonly type: typeof NodeKind.ConditionalExpression;
  readonly test: Node;
  readonly consequent: Node;
  readonly alternate: Node;
}

export interface SequenceExpression extends Node {
  readonly type: typeof NodeKind.SequenceExpression;
  readonly expressions: readonly Node[];
}

export interface AwaitExpression extends Node {
  readonly type: typeof NodeKind.AwaitExpression;
  readonly argument: Node;
}

export interface YieldExpression extends Node {
  readonly type: typeof NodeKind.YieldExpression;
  readonly argument: Node | null;
  readonly isDelegate: boolean;
}

export interface ArrowFunctionExpression extends Node {
  readonly type: typeof NodeKind.ArrowFunctionExpression;
  readonly id: Node | null;
  readonly typeParameters: Node | null;
  readonly returnType: Node | null;
  readonly body: Node | null;
  readonly params: readonly Node[];
  readonly isAsync: boolean;
  readonly isGenerator: boolean;
  readonly isExpression: boolean;
}

export interface FunctionExpression extends Node {
  readonly type: typeof NodeKind.FunctionExpression;
  readonly id: Node | null;
  readonly typeParameters: Node | null;
  readonly returnType: Node | null;
  readonly body: Node | null;
  readonly params: readonly Node[];
  readonly isAsync: boolean;
  readonly isGenerator: boolean;
}

export interface FunctionDeclaration extends Node {
  readonly type: typeof NodeKind.FunctionDeclaration;
  readonly id: Node | null;
  readonly typeParameters: Node | null;
  readonly returnType: Node | null;
  readonly body: Node | null;
  readonly params: readonly Node[];
  readonly isAsync: boolean;
  readonly isGenerator: boolean;
  readonly isDeclare: boolean;
}

export interface TSDeclareFunction extends Node {
  readonly type: typeof NodeKind.TSDeclareFunction;
  readonly id: Node | null;
  readonly typeParameters: Node | null;
  readonly returnType: Node | null;
  readonly body: Node | null;
  readonly params: readonly Node[];
  readonly isAsync: boolean;
  readonly isGenerator: boolean;
  readonly isDeclare: boolean;
}

export interface TSEmptyBodyFunctionExpression extends Node {
  readonly type: typeof NodeKind.TSEmptyBodyFunctionExpression;
  readonly id: Node | null;
  readonly typeParameters: Node | null;
  readonly returnType: Node | null;
  readonly body: Node | null;
  readonly params: readonly Node[];
  readonly isAsync: boolean;
  readonly isGenerator: boolean;
}

export interface ClassDeclaration extends Node {
  readonly type: typeof NodeKind.ClassDeclaration;
  readonly decorators: Node | null;
  readonly id: Node | null;
  readonly typeParameters: Node | null;
  readonly superClass: Node | null;
  readonly superTypeArguments: Node | null;
  readonly body: Node;
  readonly implements: readonly Node[];
  readonly isAbstract: boolean;
  readonly isDeclare: boolean;
}

export interface ClassExpression extends Node {
  readonly type: typeof NodeKind.ClassExpression;
  readonly decorators: Node | null;
  readonly id: Node | null;
  readonly typeParameters: Node | null;
  readonly superClass: Node | null;
  readonly superTypeArguments: Node | null;
  readonly body: Node;
  readonly implements: readonly Node[];
  readonly isAbstract: boolean;
}

export interface ClassBody extends Node {
  readonly type: typeof NodeKind.ClassBody;
  readonly body: readonly Node[];
}

export interface Decorators extends Node {
  readonly type: typeof NodeKind.Decorators;
  readonly decorators: readonly Node[];
}

export interface Decorator extends Node {
  readonly type: typeof NodeKind.Decorator;
  readonly expression: Node;
}

export interface MethodDefinition extends Node {
  readonly type: typeof NodeKind.MethodDefinition;
  readonly decorators: Node | null;
  readonly key: Node;
  readonly value: Node;
  readonly kind: MethodKind;
  readonly accessibility: Accessibility;
  readonly isStatic: boolean;
  readonly isComputed: boolean;
  readonly isOverride: boolean;
  readonly isOptional: boolean;
}

export interface TSAbstractMethodDefinition extends Node {
  readonly type: typeof NodeKind.TSAbstractMethodDefinition;
  readonly decorators: Node | null;
  readonly key: Node;
  readonly value: Node;
  readonly kind: MethodKind;
  readonly accessibility: Accessibility;
  readonly isStatic: boolean;
  readonly isComputed: boolean;
  readonly isOverride: boolean;
  readonly isOptional: boolean;
}

export interface PropertyDefinition extends Node {
  readonly type: typeof NodeKind.PropertyDefinition;
  readonly decorators: Node | null;
  readonly key: Node;
  readonly typeAnnotation: Node | null;
  readonly value: Node | null;
  readonly accessibility: Accessibility;
  readonly isStatic: boolean;
  readonly isComputed: boolean;
  readonly isDeclare: boolean;
  readonly isReadonly: boolean;
  readonly isDefinite: boolean;
  readonly isOptional: boolean;
  readonly isOverride: boolean;
}

export interface TSAbstractPropertyDefinition extends Node {
  readonly type: typeof NodeKind.TSAbstractPropertyDefinition;
  readonly decorators: Node | null;
  readonly key: Node;
  readonly typeAnnotation: Node | null;
  readonly value: Node | null;
  readonly accessibility: Accessibility;
  readonly isStatic: boolean;
  readonly isComputed: boolean;
  readonly isDeclare: boolean;
  readonly isReadonly: boolean;
  readonly isDefinite: boolean;
  readonly isOptional: boolean;
  readonly isOverride: boolean;
}

export interface AccessorProperty extends Node {
  readonly type: typeof NodeKind.AccessorProperty;
  readonly decorators: Node | null;
  readonly key: Node;
  readonly typeAnnotation: Node | null;
  readonly value: Node | null;
  readonly accessibility: Accessibility;
  readonly isStatic: boolean;
  readonly isComputed: boolean;
  readonly isDeclare: boolean;
  readonly isReadonly: boolean;
  readonly isDefinite: boolean;
  readonly isOptional: boolean;
  readonly isOverride: boolean;
}

export interface TSAbstractAccessorProperty extends Node {
  readonly type: typeof NodeKind.TSAbstractAccessorProperty;
  readonly decorators: Node | null;
  readonly key: Node;
  readonly typeAnnotation: Node | null;
  readonly value: Node | null;
  readonly accessibility: Accessibility;
  readonly isStatic: boolean;
  readonly isComputed: boolean;
  readonly isDeclare: boolean;
  readonly isReadonly: boolean;
  readonly isDefinite: boolean;
  readonly isOptional: boolean;
  readonly isOverride: boolean;
}

export interface StaticBlock extends Node {
  readonly type: typeof NodeKind.StaticBlock;
  readonly body: readonly Node[];
}

export interface TSParameterProperty extends Node {
  readonly type: typeof NodeKind.TSParameterProperty;
  readonly decorators: Node | null;
  readonly parameter: Node;
  readonly accessibility: Accessibility;
  readonly isReadonly: boolean;
  readonly isOverride: boolean;
}

export interface VariableDeclaration extends Node {
  readonly type: typeof NodeKind.VariableDeclaration;
  readonly declarations: readonly Node[];
  readonly kind: VariableKind;
  readonly isDeclare: boolean;
}

export interface VariableDeclarator extends Node {
  readonly type: typeof NodeKind.VariableDeclarator;
  readonly id: Node;
  readonly init: Node | null;
  readonly isDefinite: boolean;
}

export interface ObjectPattern extends Node {
  readonly type: typeof NodeKind.ObjectPattern;
  readonly typeAnnotation: Node | null;
  readonly properties: readonly Node[];
}

export interface ArrayPattern extends Node {
  readonly type: typeof NodeKind.ArrayPattern;
  readonly typeAnnotation: Node | null;
  readonly elements: readonly (Node | null)[];
}

export interface RestElement extends Node {
  readonly type: typeof NodeKind.RestElement;
  readonly argument: Node;
  readonly typeAnnotation: Node | null;
}

export interface AssignmentPattern extends Node {
  readonly type: typeof NodeKind.AssignmentPattern;
  readonly left: Node;
  readonly right: Node;
}

export interface ExpressionStatement extends Node {
  readonly type: typeof NodeKind.ExpressionStatement;
  readonly expression: Node;
  readonly isDirective: boolean;
}

export interface BlockStatement extends Node {
  readonly type: typeof NodeKind.BlockStatement;
  readonly body: readonly Node[];
}

export interface EmptyStatement extends Node {
  readonly type: typeof NodeKind.EmptyStatement;
}

export interface DebuggerStatement extends Node {
  readonly type: typeof NodeKind.DebuggerStatement;
}

export interface IfStatement extends Node {
  readonly type: typeof NodeKind.IfStatement;
  readonly test: Node;
  readonly consequent: Node;
  readonly alternate: Node | null;
}

export interface ForStatement extends Node {
  readonly type: typeof NodeKind.ForStatement;
  readonly init: Node | null;
  readonly test: Node | null;
  readonly update: Node | null;
  readonly body: Node;
}

export interface ForInStatement extends Node {
  readonly type: typeof NodeKind.ForInStatement;
  readonly left: Node;
  readonly right: Node;
  readonly body: Node;
}

export interface ForOfStatement extends Node {
  readonly type: typeof NodeKind.ForOfStatement;
  readonly left: Node;
  readonly right: Node;
  readonly body: Node;
  readonly isAwait: boolean;
}

export interface WhileStatement extends Node {
  readonly type: typeof NodeKind.WhileStatement;
  readonly test: Node;
  readonly body: Node;
}

export interface DoWhileStatement extends Node {
  readonly type: typeof NodeKind.DoWhileStatement;
  readonly body: Node;
  readonly test: Node;
}

export interface ReturnStatement extends Node {
  readonly type: typeof NodeKind.ReturnStatement;
  readonly argument: Node | null;
}

export interface ThrowStatement extends Node {
  readonly type: typeof NodeKind.ThrowStatement;
  readonly argument: Node;
}

export interface BreakStatement extends Node {
  readonly type: typeof NodeKind.BreakStatement;
  readonly label: Node | null;
}

export interface ContinueStatement extends Node {
  readonly type: typeof NodeKind.ContinueStatement;
  readonly label: Node | null;
}

export interface LabeledStatement extends Node {
  readonly type: typeof NodeKind.LabeledStatement;
  readonly label: Node;
  readonly body: Node;
}

export interface SwitchStatement extends Node {
  readonly type: typeof NodeKind.SwitchStatement;
  readonly discriminant: Node;
  readonly cases: readonly Node[];
}

export interface SwitchCase extends Node {
  readonly type: typeof NodeKind.SwitchCase;
  readonly test: Node | null;
  readonly consequent: readonly Node[];
}

export interface TryStatement extends Node {
  readonly type: typeof NodeKind.TryStatement;
  readonly block: Node;
  readonly handler: Node | null;
  readonly finalizer: Node | null;
}

export interface CatchClause extends Node {
  readonly type: typeof NodeKind.CatchClause;
  readonly param: Node | null;
  readonly body: Node;
}

export interface WithStatement extends Node {
  readonly type: typeof NodeKind.WithStatement;
  readonly object: Node;
  readonly body: Node;
}

export interface ImportDeclaration extends Node {
  readonly type: typeof NodeKind.ImportDeclaration;
  readonly source: Node;
  readonly attributes: Node | null;
  readonly specifiers: readonly Node[];
  readonly importKind: ImportKind;
}

export interface ImportSpecifier extends Node {
  readonly type: typeof NodeKind.ImportSpecifier;
  readonly imported: Node;
  readonly local: Node;
  readonly importKind: ImportKind;
}

export interface ImportDefaultSpecifier extends Node {
  readonly type: typeof NodeKind.ImportDefaultSpecifier;
  readonly local: Node;
}

export interface ImportNamespaceSpecifier extends Node {
  readonly type: typeof NodeKind.ImportNamespaceSpecifier;
  readonly local: Node;
}

export interface ImportAttributes extends Node {
  readonly type: typeof NodeKind.ImportAttributes;
  readonly attributes: readonly Node[];
}

export interface ImportAttribute extends Node {
  readonly type: typeof NodeKind.ImportAttribute;
  readonly key: Node;
  readonly value: Node;
}

export interface ExportNamedDeclaration extends Node {
  readonly type: typeof NodeKind.ExportNamedDeclaration;
  readonly declaration: Node | null;
  readonly source: Node | null;
  readonly attributes: Node | null;
  readonly specifiers: readonly Node[];
  readonly exportKind: ImportKind;
}

export interface ExportSpecifier extends Node {
  readonly type: typeof NodeKind.ExportSpecifier;
  readonly local: Node;
  readonly exported: Node;
  readonly exportKind: ImportKind;
}

export interface ExportDefaultDeclaration extends Node {
  readonly type: typeof NodeKind.ExportDefaultDeclaration;
  readonly declaration: Node;
  readonly exportKind: ImportKind;
}

export interface ExportAllDeclaration extends Node {
  readonly type: typeof NodeKind.ExportAllDeclaration;
  readonly exported: Node | null;
  readonly source: Node;
  readonly attributes: Node | null;
  readonly exportKind: ImportKind;
}

export interface JSXElement extends Node {
  readonly type: typeof NodeKind.JSXElement;
  readonly openingElement: Node;
  readonly closingElement: Node | null;
  readonly children: readonly Node[];
}

export interface JSXFragment extends Node {
  readonly type: typeof NodeKind.JSXFragment;
  readonly openingFragment: Node;
  readonly closingFragment: Node;
  readonly children: readonly Node[];
}

export interface JSXOpeningElement extends Node {
  readonly type: typeof NodeKind.JSXOpeningElement;
  readonly name: Node;
  readonly typeArguments: Node | null;
  readonly attributes: readonly Node[];
  readonly isSelfClosing: boolean;
}

export interface JSXClosingElement extends Node {
  readonly type: typeof NodeKind.JSXClosingElement;
  readonly name: Node;
}

export interface JSXOpeningFragment extends Node {
  readonly type: typeof NodeKind.JSXOpeningFragment;
}

export interface JSXClosingFragment extends Node {
  readonly type: typeof NodeKind.JSXClosingFragment;
}

export interface JSXAttribute extends Node {
  readonly type: typeof NodeKind.JSXAttribute;
  readonly name: Node;
  readonly value: Node | null;
}

export interface JSXSpreadAttribute extends Node {
  readonly type: typeof NodeKind.JSXSpreadAttribute;
  readonly argument: Node;
}

export interface JSXExpressionContainer extends Node {
  readonly type: typeof NodeKind.JSXExpressionContainer;
  readonly expression: Node;
}

export interface JSXEmptyExpression extends Node {
  readonly type: typeof NodeKind.JSXEmptyExpression;
}

export interface JSXSpreadChild extends Node {
  readonly type: typeof NodeKind.JSXSpreadChild;
  readonly expression: Node;
}

export interface JSXText extends Node {
  readonly type: typeof NodeKind.JSXText;
}

export interface JSXIdentifier extends Node {
  readonly type: typeof NodeKind.JSXIdentifier;
}

export interface JSXMemberExpression extends Node {
  readonly type: typeof NodeKind.JSXMemberExpression;
  readonly object: Node;
  readonly property: Node;
}

export interface JSXNamespacedName extends Node {
  readonly type: typeof NodeKind.JSXNamespacedName;
  readonly namespaceName: Node;
  readonly name: Node;
}

export interface TSAsExpression extends Node {
  readonly type: typeof NodeKind.TSAsExpression;
  readonly expression: Node;
  readonly typeAnnotation: Node;
}

export interface TSSatisfiesExpression extends Node {
  readonly type: typeof NodeKind.TSSatisfiesExpression;
  readonly expression: Node;
  readonly typeAnnotation: Node;
}

export interface TSNonNullExpression extends Node {
  readonly type: typeof NodeKind.TSNonNullExpression;
  readonly expression: Node;
}

export interface TSTypeAssertion extends Node {
  readonly type: typeof NodeKind.TSTypeAssertion;
  readonly typeAnnotation: Node;
  readonly expression: Node;
}

export interface TSInstantiationExpression extends Node {
  readonly type: typeof NodeKind.TSInstantiationExpression;
  readonly expression: Node;
  readonly typeArguments: Node;
}

export interface TSTypeParameterDeclaration extends Node {
  readonly type: typeof NodeKind.TSTypeParameterDeclaration;
  readonly params: readonly Node[];
}

export interface TSTypeParameter extends Node {
  readonly type: typeof NodeKind.TSTypeParameter;
  readonly constraint: Node | null;
  readonly defaultType: Node | null;
  readonly isIn: boolean;
  readonly isOut: boolean;
  readonly isConst: boolean;
}

export interface TSTypeParameterInstantiation extends Node {
  readonly type: typeof NodeKind.TSTypeParameterInstantiation;
  readonly params: readonly Node[];
}

export interface TSInterfaceDeclaration extends Node {
  readonly type: typeof NodeKind.TSInterfaceDeclaration;
  readonly id: Node;
  readonly typeParameters: Node | null;
  readonly body: Node;
  readonly extends: readonly Node[];
  readonly isDeclare: boolean;
}

export interface TSInterfaceBody extends Node {
  readonly type: typeof NodeKind.TSInterfaceBody;
  readonly body: readonly Node[];
}

export interface TSInterfaceHeritage extends Node {
  readonly type: typeof NodeKind.TSInterfaceHeritage;
  readonly expression: Node;
  readonly typeArguments: Node | null;
}

export interface TSClassImplements extends Node {
  readonly type: typeof NodeKind.TSClassImplements;
  readonly expression: Node;
  readonly typeArguments: Node | null;
}

export interface TSTypeAliasDeclaration extends Node {
  readonly type: typeof NodeKind.TSTypeAliasDeclaration;
  readonly id: Node;
  readonly typeParameters: Node | null;
  readonly typeAnnotation: Node;
  readonly isDeclare: boolean;
}

export interface TSEnumDeclaration extends Node {
  readonly type: typeof NodeKind.TSEnumDeclaration;
  readonly id: Node;
  readonly members: readonly Node[];
  readonly isConst: boolean;
  readonly isDeclare: boolean;
}

export interface TSEnumMember extends Node {
  readonly type: typeof NodeKind.TSEnumMember;
  readonly id: Node;
  readonly initializer: Node | null;
  readonly isComputed: boolean;
}

export interface TSModuleDeclaration extends Node {
  readonly type: typeof NodeKind.TSModuleDeclaration;
  readonly id: Node;
  readonly body: Node | null;
  readonly kind: ModuleKind;
  readonly isDeclare: boolean;
}

export interface TSModuleBlock extends Node {
  readonly type: typeof NodeKind.TSModuleBlock;
  readonly body: readonly Node[];
}

export interface TSImportEqualsDeclaration extends Node {
  readonly type: typeof NodeKind.TSImportEqualsDeclaration;
  readonly id: Node;
  readonly moduleReference: Node;
  readonly importKind: ImportKind;
}

export interface TSExternalModuleReference extends Node {
  readonly type: typeof NodeKind.TSExternalModuleReference;
  readonly expression: Node;
}

export interface TSExportAssignment extends Node {
  readonly type: typeof NodeKind.TSExportAssignment;
  readonly expression: Node;
}

export interface TSNamespaceExportDeclaration extends Node {
  readonly type: typeof NodeKind.TSNamespaceExportDeclaration;
  readonly id: Node;
}

export interface TSKeywordType extends Node {
  readonly type: typeof NodeKind.TSKeywordType;
  readonly keyword: Keyword;
}

export interface TSThisType extends Node {
  readonly type: typeof NodeKind.TSThisType;
}

export interface TSTypeReference extends Node {
  readonly type: typeof NodeKind.TSTypeReference;
  readonly typeName: Node;
  readonly typeArguments: Node | null;
}

export interface TSQualifiedName extends Node {
  readonly type: typeof NodeKind.TSQualifiedName;
  readonly left: Node;
  readonly right: Node;
}

export interface TSUnionType extends Node {
  readonly type: typeof NodeKind.TSUnionType;
  readonly types: readonly Node[];
}

export interface TSIntersectionType extends Node {
  readonly type: typeof NodeKind.TSIntersectionType;
  readonly types: readonly Node[];
}

export interface TSFunctionType extends Node {
  readonly type: typeof NodeKind.TSFunctionType;
  readonly typeParameters: Node | null;
  readonly returnType: Node | null;
  readonly params: readonly Node[];
}

export interface TSConstructorType extends Node {
  readonly type: typeof NodeKind.TSConstructorType;
  readonly typeParameters: Node | null;
  readonly returnType: Node | null;
  readonly params: readonly Node[];
  readonly isAbstract: boolean;
}

export interface TSConditionalType extends Node {
  readonly type: typeof NodeKind.TSConditionalType;
  readonly checkType: Node;
  readonly extendsType: Node;
  readonly trueType: Node;
  readonly falseType: Node;
}

export interface TSInferType extends Node {
  readonly type: typeof NodeKind.TSInferType;
  readonly typeParameter: Node;
}

export interface TSMappedType extends Node {
  readonly type: typeof NodeKind.TSMappedType;
  readonly typeParameter: Node;
  readonly nameType: Node | null;
  readonly typeAnnotation: Node | null;
  readonly readonlyModifier: Modifier;
  readonly optionalModifier: Modifier;
}

export interface TSIndexedAccessType extends Node {
  readonly type: typeof NodeKind.TSIndexedAccessType;
  readonly objectType: Node;
  readonly indexType: Node;
}

export interface TSTypeLiteral extends Node {
  readonly type: typeof NodeKind.TSTypeLiteral;
  readonly members: readonly Node[];
}

export interface TSArrayType extends Node {
  readonly type: typeof NodeKind.TSArrayType;
  readonly elementType: Node;
}

export interface TSTupleType extends Node {
  readonly type: typeof NodeKind.TSTupleType;
  readonly elementTypes: readonly Node[];
}

export interface TSNamedTupleMember extends Node {
  readonly type: typeof NodeKind.TSNamedTupleMember;
  readonly label: Node;
  readonly elementType: Node;
  readonly isOptional: boolean;
}

export interface TSOptionalType extends Node {
  readonly type: typeof NodeKind.TSOptionalType;
  readonly typeAnnotation: Node;
}

export interface TSRestType extends Node {
  readonly type: typeof NodeKind.TSRestType;
  readonly typeAnnotation: Node;
}

export interface TSTypeOperator extends Node {
  readonly type: typeof NodeKind.TSTypeOperator;
  readonly typeAnnotation: Node;
  readonly op: TypeOperator;
}

export interface TSTypeQuery extends Node {
  readonly type: typeof NodeKind.TSTypeQuery;
  readonly exprName: Node;
  readonly typeArguments: Node | null;
}

export interface TSTypePredicate extends Node {
  readonly type: typeof NodeKind.TSTypePredicate;
  readonly parameterName: Node;
  readonly typeAnnotation: Node | null;
  readonly isAsserts: boolean;
}

export interface TSLiteralType extends Node {
  readonly type: typeof NodeKind.TSLiteralType;
  readonly literal: Node;
}

export interface TSTemplateLiteralType extends Node {
  readonly type: typeof NodeKind.TSTemplateLiteralType;
  readonly parts: readonly Node[];
}

export interface TSImportType extends Node {
  readonly type: typeof NodeKind.TSImportType;
  readonly argument: Node;
  readonly qualifier: Node | null;
  readonly typeArguments: Node | null;
}

export interface TSPropertySignature extends Node {
  readonly type: typeof NodeKind.TSPropertySignature;
  readonly key: Node;
  readonly typeAnnotation: Node | null;
  readonly isComputed: boolean;
  readonly isOptional: boolean;
  readonly isReadonly: boolean;
}

export interface TSMethodSignature extends Node {
  readonly type: typeof NodeKind.TSMethodSignature;
  readonly key: Node;
  readonly typeParameters: Node | null;
  readonly returnType: Node | null;
  readonly params: readonly Node[];
  readonly kind: MethodKind;
  readonly isComputed: boolean;
  readonly isOptional: boolean;
}

export interface TSCallSignatureDeclaration extends Node {
  readonly type: typeof NodeKind.TSCallSignatureDeclaration;
  readonly typeParameters: Node | null;
  readonly returnType: Node | null;
  readonly params: readonly Node[];
}

export interface TSConstructSignatureDeclaration extends Node {
  readonly type: typeof NodeKind.TSConstructSignatureDeclaration;
  readonly typeParameters: Node | null;
  readonly returnType: Node | null;
  readonly params: readonly Node[];
}

export interface TSIndexSignature extends Node {
  readonly type: typeof NodeKind.TSIndexSignature;
  readonly typeAnnotation: Node | null;
  readonly parameters: readonly Node[];
  readonly isReadonly: boolean;
  readonly isStatic: boolean;
}

export interface Error extends Node {
  readonly type: typeof NodeKind.Error;
}

export type FunctionLike = FunctionDeclaration | FunctionExpression | ArrowFunctionExpression | TSDeclareFunction | TSEmptyBodyFunctionExpression;

export type ClassLike = ClassDeclaration | ClassExpression;

export type Loop = ForStatement | ForInStatement | ForOfStatement | WhileStatement | DoWhileStatement;

export type NamedDeclaration = FunctionDeclaration | ClassDeclaration | TSInterfaceDeclaration | TSTypeAliasDeclaration | TSEnumDeclaration | TSModuleDeclaration;

export type ClassMember = MethodDefinition | TSAbstractMethodDefinition | PropertyDefinition | TSAbstractPropertyDefinition | AccessorProperty | TSAbstractAccessorProperty;

export type SignatureLike = TSFunctionType | TSConstructorType | TSCallSignatureDeclaration | TSConstructSignatureDeclaration | TSMethodSignature;

/** Maps a kind value to its view, so `is` and `descendants` narrow. */
export interface NodeByKind {
  0: Program;
  1: Identifier;
  2: PrivateIdentifier;
  3: Literal;
  4: TemplateLiteral;
  5: TemplateElement;
  6: TaggedTemplateExpression;
  7: ThisExpression;
  8: Super;
  9: ArrayExpression;
  10: ObjectExpression;
  11: Property;
  12: SpreadElement;
  13: MemberExpression;
  14: CallExpression;
  15: NewExpression;
  16: ImportExpression;
  17: MetaProperty;
  18: UnaryExpression;
  19: UpdateExpression;
  20: BinaryExpression;
  21: LogicalExpression;
  22: AssignmentExpression;
  23: ConditionalExpression;
  24: SequenceExpression;
  25: AwaitExpression;
  26: YieldExpression;
  27: ArrowFunctionExpression;
  28: FunctionExpression;
  29: FunctionDeclaration;
  30: TSDeclareFunction;
  31: TSEmptyBodyFunctionExpression;
  32: ClassDeclaration;
  33: ClassExpression;
  34: ClassBody;
  35: Decorators;
  36: Decorator;
  37: MethodDefinition;
  38: TSAbstractMethodDefinition;
  39: PropertyDefinition;
  40: TSAbstractPropertyDefinition;
  41: AccessorProperty;
  42: TSAbstractAccessorProperty;
  43: StaticBlock;
  44: TSParameterProperty;
  45: VariableDeclaration;
  46: VariableDeclarator;
  47: ObjectPattern;
  48: ArrayPattern;
  49: RestElement;
  50: AssignmentPattern;
  51: ExpressionStatement;
  52: BlockStatement;
  53: EmptyStatement;
  54: DebuggerStatement;
  55: IfStatement;
  56: ForStatement;
  57: ForInStatement;
  58: ForOfStatement;
  59: WhileStatement;
  60: DoWhileStatement;
  61: ReturnStatement;
  62: ThrowStatement;
  63: BreakStatement;
  64: ContinueStatement;
  65: LabeledStatement;
  66: SwitchStatement;
  67: SwitchCase;
  68: TryStatement;
  69: CatchClause;
  70: WithStatement;
  71: ImportDeclaration;
  72: ImportSpecifier;
  73: ImportDefaultSpecifier;
  74: ImportNamespaceSpecifier;
  75: ImportAttributes;
  76: ImportAttribute;
  77: ExportNamedDeclaration;
  78: ExportSpecifier;
  79: ExportDefaultDeclaration;
  80: ExportAllDeclaration;
  81: JSXElement;
  82: JSXFragment;
  83: JSXOpeningElement;
  84: JSXClosingElement;
  85: JSXOpeningFragment;
  86: JSXClosingFragment;
  87: JSXAttribute;
  88: JSXSpreadAttribute;
  89: JSXExpressionContainer;
  90: JSXEmptyExpression;
  91: JSXSpreadChild;
  92: JSXText;
  93: JSXIdentifier;
  94: JSXMemberExpression;
  95: JSXNamespacedName;
  96: TSAsExpression;
  97: TSSatisfiesExpression;
  98: TSNonNullExpression;
  99: TSTypeAssertion;
  100: TSInstantiationExpression;
  101: TSTypeParameterDeclaration;
  102: TSTypeParameter;
  103: TSTypeParameterInstantiation;
  104: TSInterfaceDeclaration;
  105: TSInterfaceBody;
  106: TSInterfaceHeritage;
  107: TSClassImplements;
  108: TSTypeAliasDeclaration;
  109: TSEnumDeclaration;
  110: TSEnumMember;
  111: TSModuleDeclaration;
  112: TSModuleBlock;
  113: TSImportEqualsDeclaration;
  114: TSExternalModuleReference;
  115: TSExportAssignment;
  116: TSNamespaceExportDeclaration;
  117: TSKeywordType;
  118: TSThisType;
  119: TSTypeReference;
  120: TSQualifiedName;
  121: TSUnionType;
  122: TSIntersectionType;
  123: TSFunctionType;
  124: TSConstructorType;
  125: TSConditionalType;
  126: TSInferType;
  127: TSMappedType;
  128: TSIndexedAccessType;
  129: TSTypeLiteral;
  130: TSArrayType;
  131: TSTupleType;
  132: TSNamedTupleMember;
  133: TSOptionalType;
  134: TSRestType;
  135: TSTypeOperator;
  136: TSTypeQuery;
  137: TSTypePredicate;
  138: TSLiteralType;
  139: TSTemplateLiteralType;
  140: TSImportType;
  141: TSPropertySignature;
  142: TSMethodSignature;
  143: TSCallSignatureDeclaration;
  144: TSConstructSignatureDeclaration;
  145: TSIndexSignature;
  146: Error;
}

export type KindNode<K extends NodeKind> =
  K extends keyof NodeByKind ? NodeByKind[K] : Node;

/** An opaque node handle owned by the host; a rule passes it back, never reads it. */
export type Handle = unknown;

/** The accessor surface each embedding implements over its own tree memory. */
export interface Host {
  kind(handle: Handle): NodeKind;
  flags(handle: Handle): number;
  parent(handle: Handle): Handle | null;
  childCount(handle: Handle): number;
  child(handle: Handle, index: number): Handle | null;
  text(handle: Handle): string;
  dataByte(handle: Handle, byte: number): number;
  start(handle: Handle): number;
  end(handle: Handle): number;
  descendants(handle: Handle, kind: NodeKind): Handle[];
}

// A member's backing store: a fixed child slot, the tail list, a flag bit or
// a data byte. The generated `members` table pairs each with its slot index.
const CHILD = 0;
const LIST = 1;
const FLAG = 2;
const ENUM = 3;

type MemberSpec = readonly [name: string, code: number, slot: number];

/** Named members per kind, in view order: `[name, code, slot]`. */
const members: { readonly [kind: number]: readonly MemberSpec[] } = {
  0: [["body", LIST, 0], ["sourceType", ENUM, 0]],
  1: [["typeAnnotation", CHILD, 0], ["isOptional", FLAG, 0]],
  3: [["literalKind", ENUM, 0]],
  4: [["parts", LIST, 0]],
  5: [["isTail", FLAG, 20]],
  6: [["tag", CHILD, 0], ["typeArguments", CHILD, 1], ["quasi", CHILD, 2]],
  9: [["elements", LIST, 0]],
  10: [["properties", LIST, 0]],
  11: [["key", CHILD, 0], ["value", CHILD, 1], ["kind", ENUM, 0], ["isComputed", FLAG, 1], ["isShorthand", FLAG, 2], ["isMethod", FLAG, 3]],
  12: [["argument", CHILD, 0]],
  13: [["object", CHILD, 0], ["property", CHILD, 1], ["isComputed", FLAG, 1], ["isOptional", FLAG, 0]],
  14: [["callee", CHILD, 0], ["typeArguments", CHILD, 1], ["arguments", LIST, 2], ["isOptional", FLAG, 0]],
  15: [["callee", CHILD, 0], ["typeArguments", CHILD, 1], ["arguments", LIST, 2]],
  16: [["source", CHILD, 0], ["options", CHILD, 1]],
  17: [["meta", CHILD, 0], ["property", CHILD, 1]],
  18: [["argument", CHILD, 0], ["op", ENUM, 0]],
  19: [["argument", CHILD, 0], ["op", ENUM, 0], ["isPrefix", FLAG, 4]],
  20: [["left", CHILD, 0], ["right", CHILD, 1], ["op", ENUM, 0]],
  21: [["left", CHILD, 0], ["right", CHILD, 1], ["op", ENUM, 0]],
  22: [["left", CHILD, 0], ["right", CHILD, 1], ["op", ENUM, 0]],
  23: [["test", CHILD, 0], ["consequent", CHILD, 1], ["alternate", CHILD, 2]],
  24: [["expressions", LIST, 0]],
  25: [["argument", CHILD, 0]],
  26: [["argument", CHILD, 0], ["isDelegate", FLAG, 5]],
  27: [["id", CHILD, 0], ["typeParameters", CHILD, 1], ["returnType", CHILD, 2], ["body", CHILD, 3], ["params", LIST, 4], ["isAsync", FLAG, 6], ["isGenerator", FLAG, 7], ["isExpression", FLAG, 8]],
  28: [["id", CHILD, 0], ["typeParameters", CHILD, 1], ["returnType", CHILD, 2], ["body", CHILD, 3], ["params", LIST, 4], ["isAsync", FLAG, 6], ["isGenerator", FLAG, 7]],
  29: [["id", CHILD, 0], ["typeParameters", CHILD, 1], ["returnType", CHILD, 2], ["body", CHILD, 3], ["params", LIST, 4], ["isAsync", FLAG, 6], ["isGenerator", FLAG, 7], ["isDeclare", FLAG, 9]],
  30: [["id", CHILD, 0], ["typeParameters", CHILD, 1], ["returnType", CHILD, 2], ["body", CHILD, 3], ["params", LIST, 4], ["isAsync", FLAG, 6], ["isGenerator", FLAG, 7], ["isDeclare", FLAG, 9]],
  31: [["id", CHILD, 0], ["typeParameters", CHILD, 1], ["returnType", CHILD, 2], ["body", CHILD, 3], ["params", LIST, 4], ["isAsync", FLAG, 6], ["isGenerator", FLAG, 7]],
  32: [["decorators", CHILD, 0], ["id", CHILD, 1], ["typeParameters", CHILD, 2], ["superClass", CHILD, 3], ["superTypeArguments", CHILD, 4], ["body", CHILD, 5], ["implements", LIST, 6], ["isAbstract", FLAG, 10], ["isDeclare", FLAG, 9]],
  33: [["decorators", CHILD, 0], ["id", CHILD, 1], ["typeParameters", CHILD, 2], ["superClass", CHILD, 3], ["superTypeArguments", CHILD, 4], ["body", CHILD, 5], ["implements", LIST, 6], ["isAbstract", FLAG, 10]],
  34: [["body", LIST, 0]],
  35: [["decorators", LIST, 0]],
  36: [["expression", CHILD, 0]],
  37: [["decorators", CHILD, 0], ["key", CHILD, 1], ["value", CHILD, 2], ["kind", ENUM, 0], ["accessibility", ENUM, 1], ["isStatic", FLAG, 11], ["isComputed", FLAG, 1], ["isOverride", FLAG, 12], ["isOptional", FLAG, 0]],
  38: [["decorators", CHILD, 0], ["key", CHILD, 1], ["value", CHILD, 2], ["kind", ENUM, 0], ["accessibility", ENUM, 1], ["isStatic", FLAG, 11], ["isComputed", FLAG, 1], ["isOverride", FLAG, 12], ["isOptional", FLAG, 0]],
  39: [["decorators", CHILD, 0], ["key", CHILD, 1], ["typeAnnotation", CHILD, 2], ["value", CHILD, 3], ["accessibility", ENUM, 0], ["isStatic", FLAG, 11], ["isComputed", FLAG, 1], ["isDeclare", FLAG, 9], ["isReadonly", FLAG, 14], ["isDefinite", FLAG, 13], ["isOptional", FLAG, 0], ["isOverride", FLAG, 12]],
  40: [["decorators", CHILD, 0], ["key", CHILD, 1], ["typeAnnotation", CHILD, 2], ["value", CHILD, 3], ["accessibility", ENUM, 0], ["isStatic", FLAG, 11], ["isComputed", FLAG, 1], ["isDeclare", FLAG, 9], ["isReadonly", FLAG, 14], ["isDefinite", FLAG, 13], ["isOptional", FLAG, 0], ["isOverride", FLAG, 12]],
  41: [["decorators", CHILD, 0], ["key", CHILD, 1], ["typeAnnotation", CHILD, 2], ["value", CHILD, 3], ["accessibility", ENUM, 0], ["isStatic", FLAG, 11], ["isComputed", FLAG, 1], ["isDeclare", FLAG, 9], ["isReadonly", FLAG, 14], ["isDefinite", FLAG, 13], ["isOptional", FLAG, 0], ["isOverride", FLAG, 12]],
  42: [["decorators", CHILD, 0], ["key", CHILD, 1], ["typeAnnotation", CHILD, 2], ["value", CHILD, 3], ["accessibility", ENUM, 0], ["isStatic", FLAG, 11], ["isComputed", FLAG, 1], ["isDeclare", FLAG, 9], ["isReadonly", FLAG, 14], ["isDefinite", FLAG, 13], ["isOptional", FLAG, 0], ["isOverride", FLAG, 12]],
  43: [["body", LIST, 0]],
  44: [["decorators", CHILD, 0], ["parameter", CHILD, 1], ["accessibility", ENUM, 0], ["isReadonly", FLAG, 14], ["isOverride", FLAG, 12]],
  45: [["declarations", LIST, 0], ["kind", ENUM, 0], ["isDeclare", FLAG, 9]],
  46: [["id", CHILD, 0], ["init", CHILD, 1], ["isDefinite", FLAG, 13]],
  47: [["typeAnnotation", CHILD, 0], ["properties", LIST, 1]],
  48: [["typeAnnotation", CHILD, 0], ["elements", LIST, 1]],
  49: [["argument", CHILD, 0], ["typeAnnotation", CHILD, 1]],
  50: [["left", CHILD, 0], ["right", CHILD, 1]],
  51: [["expression", CHILD, 0], ["isDirective", FLAG, 15]],
  52: [["body", LIST, 0]],
  55: [["test", CHILD, 0], ["consequent", CHILD, 1], ["alternate", CHILD, 2]],
  56: [["init", CHILD, 0], ["test", CHILD, 1], ["update", CHILD, 2], ["body", CHILD, 3]],
  57: [["left", CHILD, 0], ["right", CHILD, 1], ["body", CHILD, 2]],
  58: [["left", CHILD, 0], ["right", CHILD, 1], ["body", CHILD, 2], ["isAwait", FLAG, 16]],
  59: [["test", CHILD, 0], ["body", CHILD, 1]],
  60: [["body", CHILD, 0], ["test", CHILD, 1]],
  61: [["argument", CHILD, 0]],
  62: [["argument", CHILD, 0]],
  63: [["label", CHILD, 0]],
  64: [["label", CHILD, 0]],
  65: [["label", CHILD, 0], ["body", CHILD, 1]],
  66: [["discriminant", CHILD, 0], ["cases", LIST, 1]],
  67: [["test", CHILD, 0], ["consequent", LIST, 1]],
  68: [["block", CHILD, 0], ["handler", CHILD, 1], ["finalizer", CHILD, 2]],
  69: [["param", CHILD, 0], ["body", CHILD, 1]],
  70: [["object", CHILD, 0], ["body", CHILD, 1]],
  71: [["source", CHILD, 0], ["attributes", CHILD, 1], ["specifiers", LIST, 2], ["importKind", ENUM, 0]],
  72: [["imported", CHILD, 0], ["local", CHILD, 1], ["importKind", ENUM, 0]],
  73: [["local", CHILD, 0]],
  74: [["local", CHILD, 0]],
  75: [["attributes", LIST, 0]],
  76: [["key", CHILD, 0], ["value", CHILD, 1]],
  77: [["declaration", CHILD, 0], ["source", CHILD, 1], ["attributes", CHILD, 2], ["specifiers", LIST, 3], ["exportKind", ENUM, 0]],
  78: [["local", CHILD, 0], ["exported", CHILD, 1], ["exportKind", ENUM, 0]],
  79: [["declaration", CHILD, 0], ["exportKind", ENUM, 0]],
  80: [["exported", CHILD, 0], ["source", CHILD, 1], ["attributes", CHILD, 2], ["exportKind", ENUM, 0]],
  81: [["openingElement", CHILD, 0], ["closingElement", CHILD, 1], ["children", LIST, 2]],
  82: [["openingFragment", CHILD, 0], ["closingFragment", CHILD, 1], ["children", LIST, 2]],
  83: [["name", CHILD, 0], ["typeArguments", CHILD, 1], ["attributes", LIST, 2], ["isSelfClosing", FLAG, 21]],
  84: [["name", CHILD, 0]],
  87: [["name", CHILD, 0], ["value", CHILD, 1]],
  88: [["argument", CHILD, 0]],
  89: [["expression", CHILD, 0]],
  91: [["expression", CHILD, 0]],
  94: [["object", CHILD, 0], ["property", CHILD, 1]],
  95: [["namespaceName", CHILD, 0], ["name", CHILD, 1]],
  96: [["expression", CHILD, 0], ["typeAnnotation", CHILD, 1]],
  97: [["expression", CHILD, 0], ["typeAnnotation", CHILD, 1]],
  98: [["expression", CHILD, 0]],
  99: [["typeAnnotation", CHILD, 0], ["expression", CHILD, 1]],
  100: [["expression", CHILD, 0], ["typeArguments", CHILD, 1]],
  101: [["params", LIST, 0]],
  102: [["constraint", CHILD, 0], ["defaultType", CHILD, 1], ["isIn", FLAG, 18], ["isOut", FLAG, 19], ["isConst", FLAG, 17]],
  103: [["params", LIST, 0]],
  104: [["id", CHILD, 0], ["typeParameters", CHILD, 1], ["body", CHILD, 2], ["extends", LIST, 3], ["isDeclare", FLAG, 9]],
  105: [["body", LIST, 0]],
  106: [["expression", CHILD, 0], ["typeArguments", CHILD, 1]],
  107: [["expression", CHILD, 0], ["typeArguments", CHILD, 1]],
  108: [["id", CHILD, 0], ["typeParameters", CHILD, 1], ["typeAnnotation", CHILD, 2], ["isDeclare", FLAG, 9]],
  109: [["id", CHILD, 0], ["members", LIST, 1], ["isConst", FLAG, 17], ["isDeclare", FLAG, 9]],
  110: [["id", CHILD, 0], ["initializer", CHILD, 1], ["isComputed", FLAG, 1]],
  111: [["id", CHILD, 0], ["body", CHILD, 1], ["kind", ENUM, 0], ["isDeclare", FLAG, 9]],
  112: [["body", LIST, 0]],
  113: [["id", CHILD, 0], ["moduleReference", CHILD, 1], ["importKind", ENUM, 0]],
  114: [["expression", CHILD, 0]],
  115: [["expression", CHILD, 0]],
  116: [["id", CHILD, 0]],
  117: [["keyword", ENUM, 0]],
  119: [["typeName", CHILD, 0], ["typeArguments", CHILD, 1]],
  120: [["left", CHILD, 0], ["right", CHILD, 1]],
  121: [["types", LIST, 0]],
  122: [["types", LIST, 0]],
  123: [["typeParameters", CHILD, 0], ["returnType", CHILD, 1], ["params", LIST, 2]],
  124: [["typeParameters", CHILD, 0], ["returnType", CHILD, 1], ["params", LIST, 2], ["isAbstract", FLAG, 10]],
  125: [["checkType", CHILD, 0], ["extendsType", CHILD, 1], ["trueType", CHILD, 2], ["falseType", CHILD, 3]],
  126: [["typeParameter", CHILD, 0]],
  127: [["typeParameter", CHILD, 0], ["nameType", CHILD, 1], ["typeAnnotation", CHILD, 2], ["readonlyModifier", ENUM, 0], ["optionalModifier", ENUM, 1]],
  128: [["objectType", CHILD, 0], ["indexType", CHILD, 1]],
  129: [["members", LIST, 0]],
  130: [["elementType", CHILD, 0]],
  131: [["elementTypes", LIST, 0]],
  132: [["label", CHILD, 0], ["elementType", CHILD, 1], ["isOptional", FLAG, 0]],
  133: [["typeAnnotation", CHILD, 0]],
  134: [["typeAnnotation", CHILD, 0]],
  135: [["typeAnnotation", CHILD, 0], ["op", ENUM, 0]],
  136: [["exprName", CHILD, 0], ["typeArguments", CHILD, 1]],
  137: [["parameterName", CHILD, 0], ["typeAnnotation", CHILD, 1], ["isAsserts", FLAG, 22]],
  138: [["literal", CHILD, 0]],
  139: [["parts", LIST, 0]],
  140: [["argument", CHILD, 0], ["qualifier", CHILD, 1], ["typeArguments", CHILD, 2]],
  141: [["key", CHILD, 0], ["typeAnnotation", CHILD, 1], ["isComputed", FLAG, 1], ["isOptional", FLAG, 0], ["isReadonly", FLAG, 14]],
  142: [["key", CHILD, 0], ["typeParameters", CHILD, 1], ["returnType", CHILD, 2], ["params", LIST, 3], ["kind", ENUM, 0], ["isComputed", FLAG, 1], ["isOptional", FLAG, 0]],
  143: [["typeParameters", CHILD, 0], ["returnType", CHILD, 1], ["params", LIST, 2]],
  144: [["typeParameters", CHILD, 0], ["returnType", CHILD, 1], ["params", LIST, 2]],
  145: [["typeAnnotation", CHILD, 0], ["parameters", LIST, 1], ["isReadonly", FLAG, 14], ["isStatic", FLAG, 11]],
};

class NodeHandle {
  host: Host;
  handle: Handle;
  constructor(host: Host, handle: Handle) {
    this.host = host;
    this.handle = handle;
  }
  get type(): NodeKind {
    return this.host.kind(this.handle);
  }
  get flags(): number {
    return this.host.flags(this.handle);
  }
  get parent(): Node | null {
    const p = this.host.parent(this.handle);
    return p == null ? null : wrap(this.host, p);
  }
  get childCount(): number {
    return this.host.childCount(this.handle);
  }
  child(index: number): Node | null {
    const c = this.host.child(this.handle, index);
    return c == null ? null : wrap(this.host, c);
  }
  get text(): string {
    return this.host.text(this.handle);
  }
  get range(): readonly [number, number] {
    return [this.host.start(this.handle), this.host.end(this.handle)];
  }
  hasFlag(flag: Flag): boolean {
    return (this.host.flags(this.handle) & flag) !== 0;
  }
  is<K extends NodeKind>(kind: K): this is KindNode<K> {
    return (this.host.kind(this.handle) as NodeKind) === kind;
  }
  descendants<K extends NodeKind>(kind: K): readonly KindNode<K>[] {
    return this.host
      .descendants(this.handle, kind)
      .map((h) => wrap(this.host, h)) as unknown as readonly KindNode<K>[];
  }
}

type Getter = (this: NodeHandle) => unknown;

/** Builds one named-member getter from its `[code, slot]` backing. */
function getter(code: number, slot: number): Getter {
  if (code === CHILD) {
    return function (this: NodeHandle) {
      const c = this.host.child(this.handle, slot);
      return c == null ? null : wrap(this.host, c);
    };
  }
  if (code === LIST) {
    return function (this: NodeHandle) {
      const out: (Node | null)[] = [];
      const n = this.host.childCount(this.handle);
      for (let i = slot; i < n; i++) {
        const c = this.host.child(this.handle, i);
        out.push(c == null ? null : wrap(this.host, c));
      }
      return out;
    };
  }
  if (code === FLAG) {
    return function (this: NodeHandle) {
      return (this.host.flags(this.handle) & (1 << slot)) !== 0;
    };
  }
  return function (this: NodeHandle) {
    return this.host.dataByte(this.handle, slot);
  };
}

/** One prototype per kind, cloned from NodeHandle's and given the kind's getters. */
const prototypes: { [kind: number]: object } = {};
for (const key of Object.keys(members)) {
  const kind = Number(key);
  const proto = Object.create(NodeHandle.prototype) as object;
  for (const [name, code, slot] of members[kind] ?? []) {
    Object.defineProperty(proto, name, { get: getter(code, slot), enumerable: true });
  }
  prototypes[kind] = proto;
}

/** Wraps a host handle as the typed view for its kind. */
export function wrap(host: Host, handle: Handle): Node {
  const proto = prototypes[host.kind(handle)] ?? NodeHandle.prototype;
  const node = Object.create(proto) as NodeHandle;
  node.host = host;
  node.handle = handle;
  return node as unknown as Node;
}
