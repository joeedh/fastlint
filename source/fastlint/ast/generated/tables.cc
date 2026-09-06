// Name and layout tables. Generated from source/fastlint/ast/nodes.def by
// tools/gen-ast.ts; do not edit. `node make.ts gen-ast` regenerates it.
// clang-format off

#include "fastlint/ast/kind_info.h"

namespace fastlint::ast {

namespace {

const char *const SourceTypeNames[] = {"script", "module"};
const char *const LiteralKindNames[] = {"string", "number", "bigint", "boolean", "null", "regex"};
const char *const PropertyKindNames[] = {"init", "get", "set"};
const char *const MethodKindNames[] = {"constructor", "method", "get", "set"};
const char *const AccessibilityNames[] = {"none", "public", "private", "protected"};
const char *const VariableKindNames[] = {"var", "let", "const", "using", "awaitUsing"};
const char *const ImportKindNames[] = {"value", "type"};
const char *const ModuleKindNames[] = {"module", "namespace", "global"};
const char *const ModifierNames[] = {"none", "plus", "minus"};
const char *const TypeOperatorNames[] = {"keyof", "unique", "readonly"};
const char *const KeywordNames[] = {"any", "bigint", "boolean", "intrinsic", "never", "null", "number", "object", "string", "symbol", "undefined", "unknown", "void"};
const char *const UnaryOperatorNames[] = {"minus", "plus", "not", "bitwiseNot", "typeof", "void", "delete"};
const char *const UpdateOperatorNames[] = {"increment", "decrement"};
const char *const LogicalOperatorNames[] = {"or", "and", "nullish"};
const char *const BinaryOperatorNames[] = {"equal", "notEqual", "strictEqual", "strictNotEqual", "less", "lessEqual", "greater", "greaterEqual", "shiftLeft", "shiftRight", "shiftRightUnsigned", "add", "subtract", "multiply", "divide", "remainder", "exponent", "bitwiseOr", "bitwiseXor", "bitwiseAnd", "in", "instanceof"};
const char *const AssignmentOperatorNames[] = {"assign", "addAssign", "subtractAssign", "multiplyAssign", "divideAssign", "remainderAssign", "exponentAssign", "shiftLeftAssign", "shiftRightAssign", "shiftRightUnsignedAssign", "bitwiseOrAssign", "bitwiseXorAssign", "bitwiseAndAssign", "orAssign", "andAssign", "nullishAssign"};

const char *const ProgramChildren[] = {"body"};
const EnumField ProgramEnums[] = {{"sourceType", SourceTypeNames, 2}};
const char *const IdentifierChildren[] = {"typeAnnotation"};
const EnumField LiteralEnums[] = {{"literalKind", LiteralKindNames, 6}};
const char *const TemplateLiteralChildren[] = {"parts"};
const char *const TaggedTemplateExpressionChildren[] = {"tag", "typeArguments", "quasi"};
const char *const ArrayExpressionChildren[] = {"elements"};
const char *const ObjectExpressionChildren[] = {"properties"};
const char *const PropertyChildren[] = {"key", "value"};
const EnumField PropertyEnums[] = {{"kind", PropertyKindNames, 3}};
const char *const SpreadElementChildren[] = {"argument"};
const char *const MemberExpressionChildren[] = {"object", "property"};
const char *const CallExpressionChildren[] = {"callee", "typeArguments", "arguments"};
const char *const NewExpressionChildren[] = {"callee", "typeArguments", "arguments"};
const char *const ImportExpressionChildren[] = {"source", "options"};
const char *const MetaPropertyChildren[] = {"meta", "property"};
const char *const UnaryExpressionChildren[] = {"argument"};
const EnumField UnaryExpressionEnums[] = {{"operator", UnaryOperatorNames, 7}};
const char *const UpdateExpressionChildren[] = {"argument"};
const EnumField UpdateExpressionEnums[] = {{"operator", UpdateOperatorNames, 2}};
const char *const BinaryExpressionChildren[] = {"left", "right"};
const EnumField BinaryExpressionEnums[] = {{"operator", BinaryOperatorNames, 22}};
const char *const LogicalExpressionChildren[] = {"left", "right"};
const EnumField LogicalExpressionEnums[] = {{"operator", LogicalOperatorNames, 3}};
const char *const AssignmentExpressionChildren[] = {"left", "right"};
const EnumField AssignmentExpressionEnums[] = {{"operator", AssignmentOperatorNames, 16}};
const char *const ConditionalExpressionChildren[] = {"test", "consequent", "alternate"};
const char *const SequenceExpressionChildren[] = {"expressions"};
const char *const AwaitExpressionChildren[] = {"argument"};
const char *const YieldExpressionChildren[] = {"argument"};
const char *const ArrowFunctionExpressionChildren[] = {"id", "typeParameters", "returnType", "body", "params"};
const char *const FunctionExpressionChildren[] = {"id", "typeParameters", "returnType", "body", "params"};
const char *const FunctionDeclarationChildren[] = {"id", "typeParameters", "returnType", "body", "params"};
const char *const TSDeclareFunctionChildren[] = {"id", "typeParameters", "returnType", "body", "params"};
const char *const TSEmptyBodyFunctionExpressionChildren[] = {"id", "typeParameters", "returnType", "body", "params"};
const char *const ClassDeclarationChildren[] = {"decorators", "id", "typeParameters", "superClass", "superTypeArguments", "body", "implements"};
const char *const ClassExpressionChildren[] = {"decorators", "id", "typeParameters", "superClass", "superTypeArguments", "body", "implements"};
const char *const ClassBodyChildren[] = {"body"};
const char *const DecoratorsChildren[] = {"decorators"};
const char *const DecoratorChildren[] = {"expression"};
const char *const MethodDefinitionChildren[] = {"decorators", "key", "value"};
const EnumField MethodDefinitionEnums[] = {{"kind", MethodKindNames, 4}, {"accessibility", AccessibilityNames, 4}};
const char *const TSAbstractMethodDefinitionChildren[] = {"decorators", "key", "value"};
const EnumField TSAbstractMethodDefinitionEnums[] = {{"kind", MethodKindNames, 4}, {"accessibility", AccessibilityNames, 4}};
const char *const PropertyDefinitionChildren[] = {"decorators", "key", "typeAnnotation", "value"};
const EnumField PropertyDefinitionEnums[] = {{"accessibility", AccessibilityNames, 4}};
const char *const TSAbstractPropertyDefinitionChildren[] = {"decorators", "key", "typeAnnotation", "value"};
const EnumField TSAbstractPropertyDefinitionEnums[] = {{"accessibility", AccessibilityNames, 4}};
const char *const AccessorPropertyChildren[] = {"decorators", "key", "typeAnnotation", "value"};
const EnumField AccessorPropertyEnums[] = {{"accessibility", AccessibilityNames, 4}};
const char *const TSAbstractAccessorPropertyChildren[] = {"decorators", "key", "typeAnnotation", "value"};
const EnumField TSAbstractAccessorPropertyEnums[] = {{"accessibility", AccessibilityNames, 4}};
const char *const StaticBlockChildren[] = {"body"};
const char *const TSParameterPropertyChildren[] = {"decorators", "parameter"};
const EnumField TSParameterPropertyEnums[] = {{"accessibility", AccessibilityNames, 4}};
const char *const VariableDeclarationChildren[] = {"declarations"};
const EnumField VariableDeclarationEnums[] = {{"kind", VariableKindNames, 5}};
const char *const VariableDeclaratorChildren[] = {"id", "init"};
const char *const ObjectPatternChildren[] = {"typeAnnotation", "properties"};
const char *const ArrayPatternChildren[] = {"typeAnnotation", "elements"};
const char *const RestElementChildren[] = {"argument", "typeAnnotation"};
const char *const AssignmentPatternChildren[] = {"left", "right"};
const char *const ExpressionStatementChildren[] = {"expression"};
const char *const BlockStatementChildren[] = {"body"};
const char *const IfStatementChildren[] = {"test", "consequent", "alternate"};
const char *const ForStatementChildren[] = {"init", "test", "update", "body"};
const char *const ForInStatementChildren[] = {"left", "right", "body"};
const char *const ForOfStatementChildren[] = {"left", "right", "body"};
const char *const WhileStatementChildren[] = {"test", "body"};
const char *const DoWhileStatementChildren[] = {"body", "test"};
const char *const ReturnStatementChildren[] = {"argument"};
const char *const ThrowStatementChildren[] = {"argument"};
const char *const BreakStatementChildren[] = {"label"};
const char *const ContinueStatementChildren[] = {"label"};
const char *const LabeledStatementChildren[] = {"label", "body"};
const char *const SwitchStatementChildren[] = {"discriminant", "cases"};
const char *const SwitchCaseChildren[] = {"test", "consequent"};
const char *const TryStatementChildren[] = {"block", "handler", "finalizer"};
const char *const CatchClauseChildren[] = {"param", "body"};
const char *const WithStatementChildren[] = {"object", "body"};
const char *const ImportDeclarationChildren[] = {"source", "attributes", "specifiers"};
const EnumField ImportDeclarationEnums[] = {{"importKind", ImportKindNames, 2}};
const char *const ImportSpecifierChildren[] = {"imported", "local"};
const EnumField ImportSpecifierEnums[] = {{"importKind", ImportKindNames, 2}};
const char *const ImportDefaultSpecifierChildren[] = {"local"};
const char *const ImportNamespaceSpecifierChildren[] = {"local"};
const char *const ImportAttributesChildren[] = {"attributes"};
const char *const ImportAttributeChildren[] = {"key", "value"};
const char *const ExportNamedDeclarationChildren[] = {"declaration", "source", "attributes", "specifiers"};
const EnumField ExportNamedDeclarationEnums[] = {{"exportKind", ImportKindNames, 2}};
const char *const ExportSpecifierChildren[] = {"local", "exported"};
const EnumField ExportSpecifierEnums[] = {{"exportKind", ImportKindNames, 2}};
const char *const ExportDefaultDeclarationChildren[] = {"declaration"};
const EnumField ExportDefaultDeclarationEnums[] = {{"exportKind", ImportKindNames, 2}};
const char *const ExportAllDeclarationChildren[] = {"exported", "source", "attributes"};
const EnumField ExportAllDeclarationEnums[] = {{"exportKind", ImportKindNames, 2}};
const char *const JSXElementChildren[] = {"openingElement", "closingElement", "children"};
const char *const JSXFragmentChildren[] = {"openingFragment", "closingFragment", "children"};
const char *const JSXOpeningElementChildren[] = {"name", "typeArguments", "attributes"};
const char *const JSXClosingElementChildren[] = {"name"};
const char *const JSXAttributeChildren[] = {"name", "value"};
const char *const JSXSpreadAttributeChildren[] = {"argument"};
const char *const JSXExpressionContainerChildren[] = {"expression"};
const char *const JSXSpreadChildChildren[] = {"expression"};
const char *const JSXMemberExpressionChildren[] = {"object", "property"};
const char *const JSXNamespacedNameChildren[] = {"namespace", "name"};
const char *const TSAsExpressionChildren[] = {"expression", "typeAnnotation"};
const char *const TSSatisfiesExpressionChildren[] = {"expression", "typeAnnotation"};
const char *const TSNonNullExpressionChildren[] = {"expression"};
const char *const TSTypeAssertionChildren[] = {"typeAnnotation", "expression"};
const char *const TSInstantiationExpressionChildren[] = {"expression", "typeArguments"};
const char *const TSTypeParameterDeclarationChildren[] = {"params"};
const char *const TSTypeParameterChildren[] = {"constraint", "default"};
const char *const TSTypeParameterInstantiationChildren[] = {"params"};
const char *const TSInterfaceDeclarationChildren[] = {"id", "typeParameters", "body", "extends"};
const char *const TSInterfaceBodyChildren[] = {"body"};
const char *const TSInterfaceHeritageChildren[] = {"expression", "typeArguments"};
const char *const TSClassImplementsChildren[] = {"expression", "typeArguments"};
const char *const TSTypeAliasDeclarationChildren[] = {"id", "typeParameters", "typeAnnotation"};
const char *const TSEnumDeclarationChildren[] = {"id", "members"};
const char *const TSEnumMemberChildren[] = {"id", "initializer"};
const char *const TSModuleDeclarationChildren[] = {"id", "body"};
const EnumField TSModuleDeclarationEnums[] = {{"kind", ModuleKindNames, 3}};
const char *const TSModuleBlockChildren[] = {"body"};
const char *const TSImportEqualsDeclarationChildren[] = {"id", "moduleReference"};
const EnumField TSImportEqualsDeclarationEnums[] = {{"importKind", ImportKindNames, 2}};
const char *const TSExternalModuleReferenceChildren[] = {"expression"};
const char *const TSExportAssignmentChildren[] = {"expression"};
const char *const TSNamespaceExportDeclarationChildren[] = {"id"};
const EnumField TSKeywordTypeEnums[] = {{"keyword", KeywordNames, 13}};
const char *const TSTypeReferenceChildren[] = {"typeName", "typeArguments"};
const char *const TSQualifiedNameChildren[] = {"left", "right"};
const char *const TSUnionTypeChildren[] = {"types"};
const char *const TSIntersectionTypeChildren[] = {"types"};
const char *const TSFunctionTypeChildren[] = {"typeParameters", "returnType", "params"};
const char *const TSConstructorTypeChildren[] = {"typeParameters", "returnType", "params"};
const char *const TSConditionalTypeChildren[] = {"checkType", "extendsType", "trueType", "falseType"};
const char *const TSInferTypeChildren[] = {"typeParameter"};
const char *const TSMappedTypeChildren[] = {"typeParameter", "nameType", "typeAnnotation"};
const EnumField TSMappedTypeEnums[] = {{"readonlyModifier", ModifierNames, 3}, {"optionalModifier", ModifierNames, 3}};
const char *const TSIndexedAccessTypeChildren[] = {"objectType", "indexType"};
const char *const TSTypeLiteralChildren[] = {"members"};
const char *const TSArrayTypeChildren[] = {"elementType"};
const char *const TSTupleTypeChildren[] = {"elementTypes"};
const char *const TSNamedTupleMemberChildren[] = {"label", "elementType"};
const char *const TSOptionalTypeChildren[] = {"typeAnnotation"};
const char *const TSRestTypeChildren[] = {"typeAnnotation"};
const char *const TSTypeOperatorChildren[] = {"typeAnnotation"};
const EnumField TSTypeOperatorEnums[] = {{"operator", TypeOperatorNames, 3}};
const char *const TSTypeQueryChildren[] = {"exprName", "typeArguments"};
const char *const TSTypePredicateChildren[] = {"parameterName", "typeAnnotation"};
const char *const TSLiteralTypeChildren[] = {"literal"};
const char *const TSTemplateLiteralTypeChildren[] = {"parts"};
const char *const TSImportTypeChildren[] = {"argument", "qualifier", "typeArguments"};
const char *const TSPropertySignatureChildren[] = {"key", "typeAnnotation"};
const char *const TSMethodSignatureChildren[] = {"key", "typeParameters", "returnType", "params"};
const EnumField TSMethodSignatureEnums[] = {{"kind", MethodKindNames, 4}};
const char *const TSCallSignatureDeclarationChildren[] = {"typeParameters", "returnType", "params"};
const char *const TSConstructSignatureDeclarationChildren[] = {"typeParameters", "returnType", "params"};
const char *const TSIndexSignatureChildren[] = {"typeAnnotation", "parameters"};

const KindInfo kinds[] = {
  {"Program", 0, true, false, false, ProgramChildren, 1, ProgramEnums, 1, 0},
  {"Identifier", 1, false, false, true, IdentifierChildren, 1, nullptr, 0, uint32_t(Flag::Optional)},
  {"PrivateIdentifier", 0, false, false, true, nullptr, 0, nullptr, 0, 0},
  {"Literal", 0, false, false, true, nullptr, 0, LiteralEnums, 1, 0},
  {"TemplateLiteral", 0, true, false, false, TemplateLiteralChildren, 1, nullptr, 0, 0},
  {"TemplateElement", 0, false, false, true, nullptr, 0, nullptr, 0, uint32_t(Flag::Tail)},
  {"TaggedTemplateExpression", 3, false, false, false, TaggedTemplateExpressionChildren, 3, nullptr, 0, 0},
  {"ThisExpression", 0, false, false, false, nullptr, 0, nullptr, 0, 0},
  {"Super", 0, false, false, false, nullptr, 0, nullptr, 0, 0},
  {"ArrayExpression", 0, true, true, false, ArrayExpressionChildren, 1, nullptr, 0, 0},
  {"ObjectExpression", 0, true, false, false, ObjectExpressionChildren, 1, nullptr, 0, 0},
  {"Property", 2, false, false, false, PropertyChildren, 2, PropertyEnums, 1, uint32_t(Flag::Computed) | uint32_t(Flag::Shorthand) | uint32_t(Flag::Method)},
  {"SpreadElement", 1, false, false, false, SpreadElementChildren, 1, nullptr, 0, 0},
  {"MemberExpression", 2, false, false, false, MemberExpressionChildren, 2, nullptr, 0, uint32_t(Flag::Computed) | uint32_t(Flag::Optional)},
  {"CallExpression", 2, true, false, false, CallExpressionChildren, 3, nullptr, 0, uint32_t(Flag::Optional)},
  {"NewExpression", 2, true, false, false, NewExpressionChildren, 3, nullptr, 0, 0},
  {"ImportExpression", 2, false, false, false, ImportExpressionChildren, 2, nullptr, 0, 0},
  {"MetaProperty", 2, false, false, false, MetaPropertyChildren, 2, nullptr, 0, 0},
  {"UnaryExpression", 1, false, false, false, UnaryExpressionChildren, 1, UnaryExpressionEnums, 1, 0},
  {"UpdateExpression", 1, false, false, false, UpdateExpressionChildren, 1, UpdateExpressionEnums, 1, uint32_t(Flag::Prefix)},
  {"BinaryExpression", 2, false, false, false, BinaryExpressionChildren, 2, BinaryExpressionEnums, 1, 0},
  {"LogicalExpression", 2, false, false, false, LogicalExpressionChildren, 2, LogicalExpressionEnums, 1, 0},
  {"AssignmentExpression", 2, false, false, false, AssignmentExpressionChildren, 2, AssignmentExpressionEnums, 1, 0},
  {"ConditionalExpression", 3, false, false, false, ConditionalExpressionChildren, 3, nullptr, 0, 0},
  {"SequenceExpression", 0, true, false, false, SequenceExpressionChildren, 1, nullptr, 0, 0},
  {"AwaitExpression", 1, false, false, false, AwaitExpressionChildren, 1, nullptr, 0, 0},
  {"YieldExpression", 1, false, false, false, YieldExpressionChildren, 1, nullptr, 0, uint32_t(Flag::Delegate)},
  {"ArrowFunctionExpression", 4, true, false, false, ArrowFunctionExpressionChildren, 5, nullptr, 0, uint32_t(Flag::Async) | uint32_t(Flag::Generator) | uint32_t(Flag::Expression)},
  {"FunctionExpression", 4, true, false, false, FunctionExpressionChildren, 5, nullptr, 0, uint32_t(Flag::Async) | uint32_t(Flag::Generator)},
  {"FunctionDeclaration", 4, true, false, false, FunctionDeclarationChildren, 5, nullptr, 0, uint32_t(Flag::Async) | uint32_t(Flag::Generator) | uint32_t(Flag::Declare)},
  {"TSDeclareFunction", 4, true, false, false, TSDeclareFunctionChildren, 5, nullptr, 0, uint32_t(Flag::Async) | uint32_t(Flag::Generator) | uint32_t(Flag::Declare)},
  {"TSEmptyBodyFunctionExpression", 4, true, false, false, TSEmptyBodyFunctionExpressionChildren, 5, nullptr, 0, uint32_t(Flag::Async) | uint32_t(Flag::Generator)},
  {"ClassDeclaration", 6, true, false, false, ClassDeclarationChildren, 7, nullptr, 0, uint32_t(Flag::Abstract) | uint32_t(Flag::Declare)},
  {"ClassExpression", 6, true, false, false, ClassExpressionChildren, 7, nullptr, 0, uint32_t(Flag::Abstract)},
  {"ClassBody", 0, true, false, false, ClassBodyChildren, 1, nullptr, 0, 0},
  {"Decorators", 0, true, false, false, DecoratorsChildren, 1, nullptr, 0, 0},
  {"Decorator", 1, false, false, false, DecoratorChildren, 1, nullptr, 0, 0},
  {"MethodDefinition", 3, false, false, false, MethodDefinitionChildren, 3, MethodDefinitionEnums, 2, uint32_t(Flag::Static) | uint32_t(Flag::Computed) | uint32_t(Flag::Override) | uint32_t(Flag::Optional)},
  {"TSAbstractMethodDefinition", 3, false, false, false, TSAbstractMethodDefinitionChildren, 3, TSAbstractMethodDefinitionEnums, 2, uint32_t(Flag::Static) | uint32_t(Flag::Computed) | uint32_t(Flag::Override) | uint32_t(Flag::Optional)},
  {"PropertyDefinition", 4, false, false, false, PropertyDefinitionChildren, 4, PropertyDefinitionEnums, 1, uint32_t(Flag::Static) | uint32_t(Flag::Computed) | uint32_t(Flag::Declare) | uint32_t(Flag::Readonly) | uint32_t(Flag::Definite) | uint32_t(Flag::Optional) | uint32_t(Flag::Override)},
  {"TSAbstractPropertyDefinition", 4, false, false, false, TSAbstractPropertyDefinitionChildren, 4, TSAbstractPropertyDefinitionEnums, 1, uint32_t(Flag::Static) | uint32_t(Flag::Computed) | uint32_t(Flag::Declare) | uint32_t(Flag::Readonly) | uint32_t(Flag::Definite) | uint32_t(Flag::Optional) | uint32_t(Flag::Override)},
  {"AccessorProperty", 4, false, false, false, AccessorPropertyChildren, 4, AccessorPropertyEnums, 1, uint32_t(Flag::Static) | uint32_t(Flag::Computed) | uint32_t(Flag::Declare) | uint32_t(Flag::Readonly) | uint32_t(Flag::Definite) | uint32_t(Flag::Optional) | uint32_t(Flag::Override)},
  {"TSAbstractAccessorProperty", 4, false, false, false, TSAbstractAccessorPropertyChildren, 4, TSAbstractAccessorPropertyEnums, 1, uint32_t(Flag::Static) | uint32_t(Flag::Computed) | uint32_t(Flag::Declare) | uint32_t(Flag::Readonly) | uint32_t(Flag::Definite) | uint32_t(Flag::Optional) | uint32_t(Flag::Override)},
  {"StaticBlock", 0, true, false, false, StaticBlockChildren, 1, nullptr, 0, 0},
  {"TSParameterProperty", 2, false, false, false, TSParameterPropertyChildren, 2, TSParameterPropertyEnums, 1, uint32_t(Flag::Readonly) | uint32_t(Flag::Override)},
  {"VariableDeclaration", 0, true, false, false, VariableDeclarationChildren, 1, VariableDeclarationEnums, 1, uint32_t(Flag::Declare)},
  {"VariableDeclarator", 2, false, false, false, VariableDeclaratorChildren, 2, nullptr, 0, uint32_t(Flag::Definite)},
  {"ObjectPattern", 1, true, false, false, ObjectPatternChildren, 2, nullptr, 0, 0},
  {"ArrayPattern", 1, true, true, false, ArrayPatternChildren, 2, nullptr, 0, 0},
  {"RestElement", 2, false, false, false, RestElementChildren, 2, nullptr, 0, 0},
  {"AssignmentPattern", 2, false, false, false, AssignmentPatternChildren, 2, nullptr, 0, 0},
  {"ExpressionStatement", 1, false, false, false, ExpressionStatementChildren, 1, nullptr, 0, uint32_t(Flag::Directive)},
  {"BlockStatement", 0, true, false, false, BlockStatementChildren, 1, nullptr, 0, 0},
  {"EmptyStatement", 0, false, false, false, nullptr, 0, nullptr, 0, 0},
  {"DebuggerStatement", 0, false, false, false, nullptr, 0, nullptr, 0, 0},
  {"IfStatement", 3, false, false, false, IfStatementChildren, 3, nullptr, 0, 0},
  {"ForStatement", 4, false, false, false, ForStatementChildren, 4, nullptr, 0, 0},
  {"ForInStatement", 3, false, false, false, ForInStatementChildren, 3, nullptr, 0, 0},
  {"ForOfStatement", 3, false, false, false, ForOfStatementChildren, 3, nullptr, 0, uint32_t(Flag::Await)},
  {"WhileStatement", 2, false, false, false, WhileStatementChildren, 2, nullptr, 0, 0},
  {"DoWhileStatement", 2, false, false, false, DoWhileStatementChildren, 2, nullptr, 0, 0},
  {"ReturnStatement", 1, false, false, false, ReturnStatementChildren, 1, nullptr, 0, 0},
  {"ThrowStatement", 1, false, false, false, ThrowStatementChildren, 1, nullptr, 0, 0},
  {"BreakStatement", 1, false, false, false, BreakStatementChildren, 1, nullptr, 0, 0},
  {"ContinueStatement", 1, false, false, false, ContinueStatementChildren, 1, nullptr, 0, 0},
  {"LabeledStatement", 2, false, false, false, LabeledStatementChildren, 2, nullptr, 0, 0},
  {"SwitchStatement", 1, true, false, false, SwitchStatementChildren, 2, nullptr, 0, 0},
  {"SwitchCase", 1, true, false, false, SwitchCaseChildren, 2, nullptr, 0, 0},
  {"TryStatement", 3, false, false, false, TryStatementChildren, 3, nullptr, 0, 0},
  {"CatchClause", 2, false, false, false, CatchClauseChildren, 2, nullptr, 0, 0},
  {"WithStatement", 2, false, false, false, WithStatementChildren, 2, nullptr, 0, 0},
  {"ImportDeclaration", 2, true, false, false, ImportDeclarationChildren, 3, ImportDeclarationEnums, 1, 0},
  {"ImportSpecifier", 2, false, false, false, ImportSpecifierChildren, 2, ImportSpecifierEnums, 1, 0},
  {"ImportDefaultSpecifier", 1, false, false, false, ImportDefaultSpecifierChildren, 1, nullptr, 0, 0},
  {"ImportNamespaceSpecifier", 1, false, false, false, ImportNamespaceSpecifierChildren, 1, nullptr, 0, 0},
  {"ImportAttributes", 0, true, false, false, ImportAttributesChildren, 1, nullptr, 0, 0},
  {"ImportAttribute", 2, false, false, false, ImportAttributeChildren, 2, nullptr, 0, 0},
  {"ExportNamedDeclaration", 3, true, false, false, ExportNamedDeclarationChildren, 4, ExportNamedDeclarationEnums, 1, 0},
  {"ExportSpecifier", 2, false, false, false, ExportSpecifierChildren, 2, ExportSpecifierEnums, 1, 0},
  {"ExportDefaultDeclaration", 1, false, false, false, ExportDefaultDeclarationChildren, 1, ExportDefaultDeclarationEnums, 1, 0},
  {"ExportAllDeclaration", 3, false, false, false, ExportAllDeclarationChildren, 3, ExportAllDeclarationEnums, 1, 0},
  {"JSXElement", 2, true, false, false, JSXElementChildren, 3, nullptr, 0, 0},
  {"JSXFragment", 2, true, false, false, JSXFragmentChildren, 3, nullptr, 0, 0},
  {"JSXOpeningElement", 2, true, false, false, JSXOpeningElementChildren, 3, nullptr, 0, uint32_t(Flag::SelfClosing)},
  {"JSXClosingElement", 1, false, false, false, JSXClosingElementChildren, 1, nullptr, 0, 0},
  {"JSXOpeningFragment", 0, false, false, false, nullptr, 0, nullptr, 0, 0},
  {"JSXClosingFragment", 0, false, false, false, nullptr, 0, nullptr, 0, 0},
  {"JSXAttribute", 2, false, false, false, JSXAttributeChildren, 2, nullptr, 0, 0},
  {"JSXSpreadAttribute", 1, false, false, false, JSXSpreadAttributeChildren, 1, nullptr, 0, 0},
  {"JSXExpressionContainer", 1, false, false, false, JSXExpressionContainerChildren, 1, nullptr, 0, 0},
  {"JSXEmptyExpression", 0, false, false, false, nullptr, 0, nullptr, 0, 0},
  {"JSXSpreadChild", 1, false, false, false, JSXSpreadChildChildren, 1, nullptr, 0, 0},
  {"JSXText", 0, false, false, true, nullptr, 0, nullptr, 0, 0},
  {"JSXIdentifier", 0, false, false, true, nullptr, 0, nullptr, 0, 0},
  {"JSXMemberExpression", 2, false, false, false, JSXMemberExpressionChildren, 2, nullptr, 0, 0},
  {"JSXNamespacedName", 2, false, false, false, JSXNamespacedNameChildren, 2, nullptr, 0, 0},
  {"TSAsExpression", 2, false, false, false, TSAsExpressionChildren, 2, nullptr, 0, 0},
  {"TSSatisfiesExpression", 2, false, false, false, TSSatisfiesExpressionChildren, 2, nullptr, 0, 0},
  {"TSNonNullExpression", 1, false, false, false, TSNonNullExpressionChildren, 1, nullptr, 0, 0},
  {"TSTypeAssertion", 2, false, false, false, TSTypeAssertionChildren, 2, nullptr, 0, 0},
  {"TSInstantiationExpression", 2, false, false, false, TSInstantiationExpressionChildren, 2, nullptr, 0, 0},
  {"TSTypeParameterDeclaration", 0, true, false, false, TSTypeParameterDeclarationChildren, 1, nullptr, 0, 0},
  {"TSTypeParameter", 2, false, false, true, TSTypeParameterChildren, 2, nullptr, 0, uint32_t(Flag::In) | uint32_t(Flag::Out) | uint32_t(Flag::Const)},
  {"TSTypeParameterInstantiation", 0, true, false, false, TSTypeParameterInstantiationChildren, 1, nullptr, 0, 0},
  {"TSInterfaceDeclaration", 3, true, false, false, TSInterfaceDeclarationChildren, 4, nullptr, 0, uint32_t(Flag::Declare)},
  {"TSInterfaceBody", 0, true, false, false, TSInterfaceBodyChildren, 1, nullptr, 0, 0},
  {"TSInterfaceHeritage", 2, false, false, false, TSInterfaceHeritageChildren, 2, nullptr, 0, 0},
  {"TSClassImplements", 2, false, false, false, TSClassImplementsChildren, 2, nullptr, 0, 0},
  {"TSTypeAliasDeclaration", 3, false, false, false, TSTypeAliasDeclarationChildren, 3, nullptr, 0, uint32_t(Flag::Declare)},
  {"TSEnumDeclaration", 1, true, false, false, TSEnumDeclarationChildren, 2, nullptr, 0, uint32_t(Flag::Const) | uint32_t(Flag::Declare)},
  {"TSEnumMember", 2, false, false, false, TSEnumMemberChildren, 2, nullptr, 0, uint32_t(Flag::Computed)},
  {"TSModuleDeclaration", 2, false, false, false, TSModuleDeclarationChildren, 2, TSModuleDeclarationEnums, 1, uint32_t(Flag::Declare)},
  {"TSModuleBlock", 0, true, false, false, TSModuleBlockChildren, 1, nullptr, 0, 0},
  {"TSImportEqualsDeclaration", 2, false, false, false, TSImportEqualsDeclarationChildren, 2, TSImportEqualsDeclarationEnums, 1, 0},
  {"TSExternalModuleReference", 1, false, false, false, TSExternalModuleReferenceChildren, 1, nullptr, 0, 0},
  {"TSExportAssignment", 1, false, false, false, TSExportAssignmentChildren, 1, nullptr, 0, 0},
  {"TSNamespaceExportDeclaration", 1, false, false, false, TSNamespaceExportDeclarationChildren, 1, nullptr, 0, 0},
  {"TSKeywordType", 0, false, false, false, nullptr, 0, TSKeywordTypeEnums, 1, 0},
  {"TSThisType", 0, false, false, false, nullptr, 0, nullptr, 0, 0},
  {"TSTypeReference", 2, false, false, false, TSTypeReferenceChildren, 2, nullptr, 0, 0},
  {"TSQualifiedName", 2, false, false, false, TSQualifiedNameChildren, 2, nullptr, 0, 0},
  {"TSUnionType", 0, true, false, false, TSUnionTypeChildren, 1, nullptr, 0, 0},
  {"TSIntersectionType", 0, true, false, false, TSIntersectionTypeChildren, 1, nullptr, 0, 0},
  {"TSFunctionType", 2, true, false, false, TSFunctionTypeChildren, 3, nullptr, 0, 0},
  {"TSConstructorType", 2, true, false, false, TSConstructorTypeChildren, 3, nullptr, 0, uint32_t(Flag::Abstract)},
  {"TSConditionalType", 4, false, false, false, TSConditionalTypeChildren, 4, nullptr, 0, 0},
  {"TSInferType", 1, false, false, false, TSInferTypeChildren, 1, nullptr, 0, 0},
  {"TSMappedType", 3, false, false, false, TSMappedTypeChildren, 3, TSMappedTypeEnums, 2, 0},
  {"TSIndexedAccessType", 2, false, false, false, TSIndexedAccessTypeChildren, 2, nullptr, 0, 0},
  {"TSTypeLiteral", 0, true, false, false, TSTypeLiteralChildren, 1, nullptr, 0, 0},
  {"TSArrayType", 1, false, false, false, TSArrayTypeChildren, 1, nullptr, 0, 0},
  {"TSTupleType", 0, true, false, false, TSTupleTypeChildren, 1, nullptr, 0, 0},
  {"TSNamedTupleMember", 2, false, false, false, TSNamedTupleMemberChildren, 2, nullptr, 0, uint32_t(Flag::Optional)},
  {"TSOptionalType", 1, false, false, false, TSOptionalTypeChildren, 1, nullptr, 0, 0},
  {"TSRestType", 1, false, false, false, TSRestTypeChildren, 1, nullptr, 0, 0},
  {"TSTypeOperator", 1, false, false, false, TSTypeOperatorChildren, 1, TSTypeOperatorEnums, 1, 0},
  {"TSTypeQuery", 2, false, false, false, TSTypeQueryChildren, 2, nullptr, 0, 0},
  {"TSTypePredicate", 2, false, false, false, TSTypePredicateChildren, 2, nullptr, 0, uint32_t(Flag::Asserts)},
  {"TSLiteralType", 1, false, false, false, TSLiteralTypeChildren, 1, nullptr, 0, 0},
  {"TSTemplateLiteralType", 0, true, false, false, TSTemplateLiteralTypeChildren, 1, nullptr, 0, 0},
  {"TSImportType", 3, false, false, false, TSImportTypeChildren, 3, nullptr, 0, 0},
  {"TSPropertySignature", 2, false, false, false, TSPropertySignatureChildren, 2, nullptr, 0, uint32_t(Flag::Computed) | uint32_t(Flag::Optional) | uint32_t(Flag::Readonly)},
  {"TSMethodSignature", 3, true, false, false, TSMethodSignatureChildren, 4, TSMethodSignatureEnums, 1, uint32_t(Flag::Computed) | uint32_t(Flag::Optional)},
  {"TSCallSignatureDeclaration", 2, true, false, false, TSCallSignatureDeclarationChildren, 3, nullptr, 0, 0},
  {"TSConstructSignatureDeclaration", 2, true, false, false, TSConstructSignatureDeclarationChildren, 3, nullptr, 0, 0},
  {"TSIndexSignature", 1, true, false, false, TSIndexSignatureChildren, 2, nullptr, 0, uint32_t(Flag::Readonly) | uint32_t(Flag::Static)},
  {"Error", 0, false, false, false, nullptr, 0, nullptr, 0, 0},
};

const char *const flagNames[] = {"optional", "computed", "shorthand", "method", "prefix", "delegate", "async", "generator", "expression", "declare", "abstract", "static", "override", "definite", "readonly", "directive", "await", "const", "in", "out", "tail", "selfClosing", "asserts", "global", "parenthesized", "incomplete"};

} // namespace

const KindInfo &kindInfo(NodeKind kind)
{
  return kinds[int(kind)];
}

const char *kindName(NodeKind kind)
{
  return kinds[int(kind)].name;
}

const char *flagName(int bit)
{
  return bit >= 0 && bit < flagCount ? flagNames[bit] : "";
}

} // namespace fastlint::ast
