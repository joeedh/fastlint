// AST node kinds, flags and field enums. Generated from source/fastlint/ast/nodes.def by
// tools/gen-ast.ts; do not edit. `node make.ts gen-ast` regenerates it.
// clang-format off

#pragma once

#include <cstdint>

namespace fastlint::ast {

enum class NodeKind : uint16_t {
  Program,
  Identifier,
  PrivateIdentifier,
  Literal,
  TemplateLiteral,
  TemplateElement,
  TaggedTemplateExpression,
  ThisExpression,
  Super,
  ArrayExpression,
  ObjectExpression,
  Property,
  SpreadElement,
  MemberExpression,
  CallExpression,
  NewExpression,
  ImportExpression,
  MetaProperty,
  UnaryExpression,
  UpdateExpression,
  BinaryExpression,
  LogicalExpression,
  AssignmentExpression,
  ConditionalExpression,
  SequenceExpression,
  AwaitExpression,
  YieldExpression,
  ArrowFunctionExpression,
  FunctionExpression,
  FunctionDeclaration,
  TSDeclareFunction,
  TSEmptyBodyFunctionExpression,
  ClassDeclaration,
  ClassExpression,
  ClassBody,
  Decorators,
  Decorator,
  MethodDefinition,
  TSAbstractMethodDefinition,
  PropertyDefinition,
  TSAbstractPropertyDefinition,
  AccessorProperty,
  TSAbstractAccessorProperty,
  StaticBlock,
  TSParameterProperty,
  VariableDeclaration,
  VariableDeclarator,
  ObjectPattern,
  ArrayPattern,
  RestElement,
  AssignmentPattern,
  ExpressionStatement,
  BlockStatement,
  EmptyStatement,
  DebuggerStatement,
  IfStatement,
  ForStatement,
  ForInStatement,
  ForOfStatement,
  WhileStatement,
  DoWhileStatement,
  ReturnStatement,
  ThrowStatement,
  BreakStatement,
  ContinueStatement,
  LabeledStatement,
  SwitchStatement,
  SwitchCase,
  TryStatement,
  CatchClause,
  WithStatement,
  ImportDeclaration,
  ImportSpecifier,
  ImportDefaultSpecifier,
  ImportNamespaceSpecifier,
  ImportAttributes,
  ImportAttribute,
  ExportNamedDeclaration,
  ExportSpecifier,
  ExportDefaultDeclaration,
  ExportAllDeclaration,
  JSXElement,
  JSXFragment,
  JSXOpeningElement,
  JSXClosingElement,
  JSXOpeningFragment,
  JSXClosingFragment,
  JSXAttribute,
  JSXSpreadAttribute,
  JSXExpressionContainer,
  JSXEmptyExpression,
  JSXSpreadChild,
  JSXText,
  JSXIdentifier,
  JSXMemberExpression,
  JSXNamespacedName,
  TSAsExpression,
  TSSatisfiesExpression,
  TSNonNullExpression,
  TSTypeAssertion,
  TSInstantiationExpression,
  TSTypeParameterDeclaration,
  TSTypeParameter,
  TSTypeParameterInstantiation,
  TSInterfaceDeclaration,
  TSInterfaceBody,
  TSInterfaceHeritage,
  TSClassImplements,
  TSTypeAliasDeclaration,
  TSEnumDeclaration,
  TSEnumMember,
  TSModuleDeclaration,
  TSModuleBlock,
  TSImportEqualsDeclaration,
  TSExternalModuleReference,
  TSExportAssignment,
  TSNamespaceExportDeclaration,
  TSKeywordType,
  TSThisType,
  TSTypeReference,
  TSQualifiedName,
  TSUnionType,
  TSIntersectionType,
  TSFunctionType,
  TSConstructorType,
  TSConditionalType,
  TSInferType,
  TSMappedType,
  TSIndexedAccessType,
  TSTypeLiteral,
  TSArrayType,
  TSTupleType,
  TSNamedTupleMember,
  TSOptionalType,
  TSRestType,
  TSTypeOperator,
  TSTypeQuery,
  TSTypePredicate,
  TSLiteralType,
  TSTemplateLiteralType,
  TSImportType,
  TSPropertySignature,
  TSMethodSignature,
  TSCallSignatureDeclaration,
  TSConstructSignatureDeclaration,
  TSIndexSignature,
  Error,
};

constexpr int kindCount = 147;

/** Bits of Node::flags. */
enum class Flag : uint32_t {
  Optional = 1u << 0,
  Computed = 1u << 1,
  Shorthand = 1u << 2,
  Method = 1u << 3,
  Prefix = 1u << 4,
  Delegate = 1u << 5,
  Async = 1u << 6,
  Generator = 1u << 7,
  Expression = 1u << 8,
  Declare = 1u << 9,
  Abstract = 1u << 10,
  Static = 1u << 11,
  Override = 1u << 12,
  Definite = 1u << 13,
  Readonly = 1u << 14,
  Directive = 1u << 15,
  Await = 1u << 16,
  Const = 1u << 17,
  In = 1u << 18,
  Out = 1u << 19,
  Tail = 1u << 20,
  SelfClosing = 1u << 21,
  Asserts = 1u << 22,
  Global = 1u << 23,
  Parenthesized = 1u << 24,
  Incomplete = 1u << 25,
};

constexpr int flagCount = 26;

enum class SourceType : uint8_t {
  Script,
  Module,
};

enum class LiteralKind : uint8_t {
  String,
  Number,
  Bigint,
  Boolean,
  Null,
  Regex,
};

enum class PropertyKind : uint8_t {
  Init,
  Get,
  Set,
};

enum class MethodKind : uint8_t {
  Constructor,
  Method,
  Get,
  Set,
};

enum class Accessibility : uint8_t {
  None,
  Public,
  Private,
  Protected,
};

enum class VariableKind : uint8_t {
  Var,
  Let,
  Const,
  Using,
  AwaitUsing,
};

enum class ImportKind : uint8_t {
  Value,
  Type,
};

enum class ModuleKind : uint8_t {
  Module,
  Namespace,
  Global,
};

enum class Modifier : uint8_t {
  None,
  Plus,
  Minus,
};

enum class TypeOperator : uint8_t {
  Keyof,
  Unique,
  Readonly,
};

enum class Keyword : uint8_t {
  Any,
  Bigint,
  Boolean,
  Intrinsic,
  Never,
  Null,
  Number,
  Object,
  String,
  Symbol,
  Undefined,
  Unknown,
  Void,
};

enum class UnaryOperator : uint8_t {
  Minus,
  Plus,
  Not,
  BitwiseNot,
  Typeof,
  Void,
  Delete,
};

enum class UpdateOperator : uint8_t {
  Increment,
  Decrement,
};

enum class LogicalOperator : uint8_t {
  Or,
  And,
  Nullish,
};

enum class BinaryOperator : uint8_t {
  Equal,
  NotEqual,
  StrictEqual,
  StrictNotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  ShiftLeft,
  ShiftRight,
  ShiftRightUnsigned,
  Add,
  Subtract,
  Multiply,
  Divide,
  Remainder,
  Exponent,
  BitwiseOr,
  BitwiseXor,
  BitwiseAnd,
  In,
  Instanceof,
};

enum class AssignmentOperator : uint8_t {
  Assign,
  AddAssign,
  SubtractAssign,
  MultiplyAssign,
  DivideAssign,
  RemainderAssign,
  ExponentAssign,
  ShiftLeftAssign,
  ShiftRightAssign,
  ShiftRightUnsignedAssign,
  BitwiseOrAssign,
  BitwiseXorAssign,
  BitwiseAndAssign,
  OrAssign,
  AndAssign,
  NullishAssign,
};

/** FNV-1a of nodes.def; plugins refuse a host with a different value. */
constexpr uint32_t nodesDefHash = 0x766fe1c1u;

} // namespace fastlint::ast
