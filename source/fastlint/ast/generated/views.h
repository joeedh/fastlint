// Typed views over Node. Generated from source/fastlint/ast/nodes.def by
// tools/gen-ast.ts; do not edit. `node make.ts gen-ast` regenerates it.
// clang-format off

#pragma once

#include "fastlint/ast/access.h"
#include "fastlint/ast/generated/kinds.h"

namespace fastlint::ast {

struct Program : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::Program;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::Program; }
  span<Node *> body() const { return access::tail(n, 0); }
  SourceType sourceType() const { return SourceType(access::dataByte(n, 0)); }
};

struct Identifier : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::Identifier;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::Identifier; }
  Node *typeAnnotation() const { return access::child(n, 0); }
  string_view text() const { return access::text(n); }
  bool isOptional() const { return access::hasFlag(n, Flag::Optional); }
};

struct PrivateIdentifier : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::PrivateIdentifier;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::PrivateIdentifier; }
  string_view text() const { return access::text(n); }
};

struct Literal : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::Literal;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::Literal; }
  string_view text() const { return access::text(n); }
  LiteralKind literalKind() const { return LiteralKind(access::dataByte(n, 0)); }
};

struct TemplateLiteral : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TemplateLiteral;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TemplateLiteral; }
  span<Node *> parts() const { return access::tail(n, 0); }
};

struct TemplateElement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TemplateElement;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TemplateElement; }
  string_view text() const { return access::text(n); }
  bool isTail() const { return access::hasFlag(n, Flag::Tail); }
};

struct TaggedTemplateExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TaggedTemplateExpression;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TaggedTemplateExpression; }
  Node *tag() const { return access::child(n, 0); }
  Node *typeArguments() const { return access::child(n, 1); }
  Node *quasi() const { return access::child(n, 2); }
};

struct ThisExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ThisExpression;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ThisExpression; }
};

struct Super : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::Super;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::Super; }
};

struct ArrayExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ArrayExpression;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ArrayExpression; }
  span<Node *> elements() const { return access::tail(n, 0); }
};

struct ObjectExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ObjectExpression;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ObjectExpression; }
  span<Node *> properties() const { return access::tail(n, 0); }
};

struct Property : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::Property;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::Property; }
  Node *key() const { return access::child(n, 0); }
  Node *value() const { return access::child(n, 1); }
  PropertyKind kind() const { return PropertyKind(access::dataByte(n, 0)); }
  bool isComputed() const { return access::hasFlag(n, Flag::Computed); }
  bool isShorthand() const { return access::hasFlag(n, Flag::Shorthand); }
  bool isMethod() const { return access::hasFlag(n, Flag::Method); }
};

struct SpreadElement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::SpreadElement;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::SpreadElement; }
  Node *argument() const { return access::child(n, 0); }
};

struct MemberExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::MemberExpression;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::MemberExpression; }
  Node *object() const { return access::child(n, 0); }
  Node *property() const { return access::child(n, 1); }
  bool isComputed() const { return access::hasFlag(n, Flag::Computed); }
  bool isOptional() const { return access::hasFlag(n, Flag::Optional); }
};

struct CallExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::CallExpression;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::CallExpression; }
  Node *callee() const { return access::child(n, 0); }
  Node *typeArguments() const { return access::child(n, 1); }
  span<Node *> arguments() const { return access::tail(n, 2); }
  bool isOptional() const { return access::hasFlag(n, Flag::Optional); }
};

struct NewExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::NewExpression;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::NewExpression; }
  Node *callee() const { return access::child(n, 0); }
  Node *typeArguments() const { return access::child(n, 1); }
  span<Node *> arguments() const { return access::tail(n, 2); }
};

struct ImportExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ImportExpression;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ImportExpression; }
  Node *source() const { return access::child(n, 0); }
  Node *options() const { return access::child(n, 1); }
};

struct MetaProperty : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::MetaProperty;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::MetaProperty; }
  Node *meta() const { return access::child(n, 0); }
  Node *property() const { return access::child(n, 1); }
};

struct UnaryExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::UnaryExpression;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::UnaryExpression; }
  Node *argument() const { return access::child(n, 0); }
  UnaryOperator op() const { return UnaryOperator(access::dataByte(n, 0)); }
};

struct UpdateExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::UpdateExpression;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::UpdateExpression; }
  Node *argument() const { return access::child(n, 0); }
  UpdateOperator op() const { return UpdateOperator(access::dataByte(n, 0)); }
  bool isPrefix() const { return access::hasFlag(n, Flag::Prefix); }
};

struct BinaryExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::BinaryExpression;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::BinaryExpression; }
  Node *left() const { return access::child(n, 0); }
  Node *right() const { return access::child(n, 1); }
  BinaryOperator op() const { return BinaryOperator(access::dataByte(n, 0)); }
};

struct LogicalExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::LogicalExpression;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::LogicalExpression; }
  Node *left() const { return access::child(n, 0); }
  Node *right() const { return access::child(n, 1); }
  LogicalOperator op() const { return LogicalOperator(access::dataByte(n, 0)); }
};

struct AssignmentExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::AssignmentExpression;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::AssignmentExpression; }
  Node *left() const { return access::child(n, 0); }
  Node *right() const { return access::child(n, 1); }
  AssignmentOperator op() const { return AssignmentOperator(access::dataByte(n, 0)); }
};

struct ConditionalExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ConditionalExpression;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ConditionalExpression; }
  Node *test() const { return access::child(n, 0); }
  Node *consequent() const { return access::child(n, 1); }
  Node *alternate() const { return access::child(n, 2); }
};

struct SequenceExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::SequenceExpression;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::SequenceExpression; }
  span<Node *> expressions() const { return access::tail(n, 0); }
};

struct AwaitExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::AwaitExpression;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::AwaitExpression; }
  Node *argument() const { return access::child(n, 0); }
};

struct YieldExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::YieldExpression;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::YieldExpression; }
  Node *argument() const { return access::child(n, 0); }
  bool isDelegate() const { return access::hasFlag(n, Flag::Delegate); }
};

struct ArrowFunctionExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ArrowFunctionExpression;
  static constexpr int fixedChildren = 4;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ArrowFunctionExpression; }
  Node *id() const { return access::child(n, 0); }
  Node *typeParameters() const { return access::child(n, 1); }
  Node *returnType() const { return access::child(n, 2); }
  Node *body() const { return access::child(n, 3); }
  span<Node *> params() const { return access::tail(n, 4); }
  bool isAsync() const { return access::hasFlag(n, Flag::Async); }
  bool isGenerator() const { return access::hasFlag(n, Flag::Generator); }
  bool isExpression() const { return access::hasFlag(n, Flag::Expression); }
};

struct FunctionExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::FunctionExpression;
  static constexpr int fixedChildren = 4;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::FunctionExpression; }
  Node *id() const { return access::child(n, 0); }
  Node *typeParameters() const { return access::child(n, 1); }
  Node *returnType() const { return access::child(n, 2); }
  Node *body() const { return access::child(n, 3); }
  span<Node *> params() const { return access::tail(n, 4); }
  bool isAsync() const { return access::hasFlag(n, Flag::Async); }
  bool isGenerator() const { return access::hasFlag(n, Flag::Generator); }
};

struct FunctionDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::FunctionDeclaration;
  static constexpr int fixedChildren = 4;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::FunctionDeclaration; }
  Node *id() const { return access::child(n, 0); }
  Node *typeParameters() const { return access::child(n, 1); }
  Node *returnType() const { return access::child(n, 2); }
  Node *body() const { return access::child(n, 3); }
  span<Node *> params() const { return access::tail(n, 4); }
  bool isAsync() const { return access::hasFlag(n, Flag::Async); }
  bool isGenerator() const { return access::hasFlag(n, Flag::Generator); }
  bool isDeclare() const { return access::hasFlag(n, Flag::Declare); }
};

struct TSDeclareFunction : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSDeclareFunction;
  static constexpr int fixedChildren = 4;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSDeclareFunction; }
  Node *id() const { return access::child(n, 0); }
  Node *typeParameters() const { return access::child(n, 1); }
  Node *returnType() const { return access::child(n, 2); }
  Node *body() const { return access::child(n, 3); }
  span<Node *> params() const { return access::tail(n, 4); }
  bool isAsync() const { return access::hasFlag(n, Flag::Async); }
  bool isGenerator() const { return access::hasFlag(n, Flag::Generator); }
  bool isDeclare() const { return access::hasFlag(n, Flag::Declare); }
};

struct TSEmptyBodyFunctionExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSEmptyBodyFunctionExpression;
  static constexpr int fixedChildren = 4;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSEmptyBodyFunctionExpression; }
  Node *id() const { return access::child(n, 0); }
  Node *typeParameters() const { return access::child(n, 1); }
  Node *returnType() const { return access::child(n, 2); }
  Node *body() const { return access::child(n, 3); }
  span<Node *> params() const { return access::tail(n, 4); }
  bool isAsync() const { return access::hasFlag(n, Flag::Async); }
  bool isGenerator() const { return access::hasFlag(n, Flag::Generator); }
};

struct ClassDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ClassDeclaration;
  static constexpr int fixedChildren = 6;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ClassDeclaration; }
  Node *decorators() const { return access::child(n, 0); }
  Node *id() const { return access::child(n, 1); }
  Node *typeParameters() const { return access::child(n, 2); }
  Node *superClass() const { return access::child(n, 3); }
  Node *superTypeArguments() const { return access::child(n, 4); }
  Node *body() const { return access::child(n, 5); }
  span<Node *> implements() const { return access::tail(n, 6); }
  bool isAbstract() const { return access::hasFlag(n, Flag::Abstract); }
  bool isDeclare() const { return access::hasFlag(n, Flag::Declare); }
};

struct ClassExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ClassExpression;
  static constexpr int fixedChildren = 6;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ClassExpression; }
  Node *decorators() const { return access::child(n, 0); }
  Node *id() const { return access::child(n, 1); }
  Node *typeParameters() const { return access::child(n, 2); }
  Node *superClass() const { return access::child(n, 3); }
  Node *superTypeArguments() const { return access::child(n, 4); }
  Node *body() const { return access::child(n, 5); }
  span<Node *> implements() const { return access::tail(n, 6); }
  bool isAbstract() const { return access::hasFlag(n, Flag::Abstract); }
};

struct ClassBody : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ClassBody;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ClassBody; }
  span<Node *> body() const { return access::tail(n, 0); }
};

struct Decorators : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::Decorators;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::Decorators; }
  span<Node *> decorators() const { return access::tail(n, 0); }
};

struct Decorator : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::Decorator;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::Decorator; }
  Node *expression() const { return access::child(n, 0); }
};

struct MethodDefinition : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::MethodDefinition;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::MethodDefinition; }
  Node *decorators() const { return access::child(n, 0); }
  Node *key() const { return access::child(n, 1); }
  Node *value() const { return access::child(n, 2); }
  MethodKind kind() const { return MethodKind(access::dataByte(n, 0)); }
  Accessibility accessibility() const { return Accessibility(access::dataByte(n, 1)); }
  bool isStatic() const { return access::hasFlag(n, Flag::Static); }
  bool isComputed() const { return access::hasFlag(n, Flag::Computed); }
  bool isOverride() const { return access::hasFlag(n, Flag::Override); }
  bool isOptional() const { return access::hasFlag(n, Flag::Optional); }
};

struct TSAbstractMethodDefinition : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSAbstractMethodDefinition;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSAbstractMethodDefinition; }
  Node *decorators() const { return access::child(n, 0); }
  Node *key() const { return access::child(n, 1); }
  Node *value() const { return access::child(n, 2); }
  MethodKind kind() const { return MethodKind(access::dataByte(n, 0)); }
  Accessibility accessibility() const { return Accessibility(access::dataByte(n, 1)); }
  bool isStatic() const { return access::hasFlag(n, Flag::Static); }
  bool isComputed() const { return access::hasFlag(n, Flag::Computed); }
  bool isOverride() const { return access::hasFlag(n, Flag::Override); }
  bool isOptional() const { return access::hasFlag(n, Flag::Optional); }
};

struct PropertyDefinition : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::PropertyDefinition;
  static constexpr int fixedChildren = 4;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::PropertyDefinition; }
  Node *decorators() const { return access::child(n, 0); }
  Node *key() const { return access::child(n, 1); }
  Node *typeAnnotation() const { return access::child(n, 2); }
  Node *value() const { return access::child(n, 3); }
  Accessibility accessibility() const { return Accessibility(access::dataByte(n, 0)); }
  bool isStatic() const { return access::hasFlag(n, Flag::Static); }
  bool isComputed() const { return access::hasFlag(n, Flag::Computed); }
  bool isDeclare() const { return access::hasFlag(n, Flag::Declare); }
  bool isReadonly() const { return access::hasFlag(n, Flag::Readonly); }
  bool isDefinite() const { return access::hasFlag(n, Flag::Definite); }
  bool isOptional() const { return access::hasFlag(n, Flag::Optional); }
  bool isOverride() const { return access::hasFlag(n, Flag::Override); }
};

struct TSAbstractPropertyDefinition : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSAbstractPropertyDefinition;
  static constexpr int fixedChildren = 4;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSAbstractPropertyDefinition; }
  Node *decorators() const { return access::child(n, 0); }
  Node *key() const { return access::child(n, 1); }
  Node *typeAnnotation() const { return access::child(n, 2); }
  Node *value() const { return access::child(n, 3); }
  Accessibility accessibility() const { return Accessibility(access::dataByte(n, 0)); }
  bool isStatic() const { return access::hasFlag(n, Flag::Static); }
  bool isComputed() const { return access::hasFlag(n, Flag::Computed); }
  bool isDeclare() const { return access::hasFlag(n, Flag::Declare); }
  bool isReadonly() const { return access::hasFlag(n, Flag::Readonly); }
  bool isDefinite() const { return access::hasFlag(n, Flag::Definite); }
  bool isOptional() const { return access::hasFlag(n, Flag::Optional); }
  bool isOverride() const { return access::hasFlag(n, Flag::Override); }
};

struct AccessorProperty : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::AccessorProperty;
  static constexpr int fixedChildren = 4;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::AccessorProperty; }
  Node *decorators() const { return access::child(n, 0); }
  Node *key() const { return access::child(n, 1); }
  Node *typeAnnotation() const { return access::child(n, 2); }
  Node *value() const { return access::child(n, 3); }
  Accessibility accessibility() const { return Accessibility(access::dataByte(n, 0)); }
  bool isStatic() const { return access::hasFlag(n, Flag::Static); }
  bool isComputed() const { return access::hasFlag(n, Flag::Computed); }
  bool isDeclare() const { return access::hasFlag(n, Flag::Declare); }
  bool isReadonly() const { return access::hasFlag(n, Flag::Readonly); }
  bool isDefinite() const { return access::hasFlag(n, Flag::Definite); }
  bool isOptional() const { return access::hasFlag(n, Flag::Optional); }
  bool isOverride() const { return access::hasFlag(n, Flag::Override); }
};

struct TSAbstractAccessorProperty : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSAbstractAccessorProperty;
  static constexpr int fixedChildren = 4;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSAbstractAccessorProperty; }
  Node *decorators() const { return access::child(n, 0); }
  Node *key() const { return access::child(n, 1); }
  Node *typeAnnotation() const { return access::child(n, 2); }
  Node *value() const { return access::child(n, 3); }
  Accessibility accessibility() const { return Accessibility(access::dataByte(n, 0)); }
  bool isStatic() const { return access::hasFlag(n, Flag::Static); }
  bool isComputed() const { return access::hasFlag(n, Flag::Computed); }
  bool isDeclare() const { return access::hasFlag(n, Flag::Declare); }
  bool isReadonly() const { return access::hasFlag(n, Flag::Readonly); }
  bool isDefinite() const { return access::hasFlag(n, Flag::Definite); }
  bool isOptional() const { return access::hasFlag(n, Flag::Optional); }
  bool isOverride() const { return access::hasFlag(n, Flag::Override); }
};

struct StaticBlock : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::StaticBlock;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::StaticBlock; }
  span<Node *> body() const { return access::tail(n, 0); }
};

struct TSParameterProperty : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSParameterProperty;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSParameterProperty; }
  Node *decorators() const { return access::child(n, 0); }
  Node *parameter() const { return access::child(n, 1); }
  Accessibility accessibility() const { return Accessibility(access::dataByte(n, 0)); }
  bool isReadonly() const { return access::hasFlag(n, Flag::Readonly); }
  bool isOverride() const { return access::hasFlag(n, Flag::Override); }
};

struct VariableDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::VariableDeclaration;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::VariableDeclaration; }
  span<Node *> declarations() const { return access::tail(n, 0); }
  VariableKind kind() const { return VariableKind(access::dataByte(n, 0)); }
  bool isDeclare() const { return access::hasFlag(n, Flag::Declare); }
};

struct VariableDeclarator : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::VariableDeclarator;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::VariableDeclarator; }
  Node *id() const { return access::child(n, 0); }
  Node *init() const { return access::child(n, 1); }
  bool isDefinite() const { return access::hasFlag(n, Flag::Definite); }
};

struct ObjectPattern : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ObjectPattern;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ObjectPattern; }
  Node *typeAnnotation() const { return access::child(n, 0); }
  span<Node *> properties() const { return access::tail(n, 1); }
};

struct ArrayPattern : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ArrayPattern;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ArrayPattern; }
  Node *typeAnnotation() const { return access::child(n, 0); }
  span<Node *> elements() const { return access::tail(n, 1); }
};

struct RestElement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::RestElement;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::RestElement; }
  Node *argument() const { return access::child(n, 0); }
  Node *typeAnnotation() const { return access::child(n, 1); }
};

struct AssignmentPattern : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::AssignmentPattern;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::AssignmentPattern; }
  Node *left() const { return access::child(n, 0); }
  Node *right() const { return access::child(n, 1); }
};

struct ExpressionStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ExpressionStatement;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ExpressionStatement; }
  Node *expression() const { return access::child(n, 0); }
  bool isDirective() const { return access::hasFlag(n, Flag::Directive); }
};

struct BlockStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::BlockStatement;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::BlockStatement; }
  span<Node *> body() const { return access::tail(n, 0); }
};

struct EmptyStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::EmptyStatement;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::EmptyStatement; }
};

struct DebuggerStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::DebuggerStatement;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::DebuggerStatement; }
};

struct IfStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::IfStatement;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::IfStatement; }
  Node *test() const { return access::child(n, 0); }
  Node *consequent() const { return access::child(n, 1); }
  Node *alternate() const { return access::child(n, 2); }
};

struct ForStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ForStatement;
  static constexpr int fixedChildren = 4;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ForStatement; }
  Node *init() const { return access::child(n, 0); }
  Node *test() const { return access::child(n, 1); }
  Node *update() const { return access::child(n, 2); }
  Node *body() const { return access::child(n, 3); }
};

struct ForInStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ForInStatement;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ForInStatement; }
  Node *left() const { return access::child(n, 0); }
  Node *right() const { return access::child(n, 1); }
  Node *body() const { return access::child(n, 2); }
};

struct ForOfStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ForOfStatement;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ForOfStatement; }
  Node *left() const { return access::child(n, 0); }
  Node *right() const { return access::child(n, 1); }
  Node *body() const { return access::child(n, 2); }
  bool isAwait() const { return access::hasFlag(n, Flag::Await); }
};

struct WhileStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::WhileStatement;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::WhileStatement; }
  Node *test() const { return access::child(n, 0); }
  Node *body() const { return access::child(n, 1); }
};

struct DoWhileStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::DoWhileStatement;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::DoWhileStatement; }
  Node *body() const { return access::child(n, 0); }
  Node *test() const { return access::child(n, 1); }
};

struct ReturnStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ReturnStatement;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ReturnStatement; }
  Node *argument() const { return access::child(n, 0); }
};

struct ThrowStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ThrowStatement;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ThrowStatement; }
  Node *argument() const { return access::child(n, 0); }
};

struct BreakStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::BreakStatement;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::BreakStatement; }
  Node *label() const { return access::child(n, 0); }
};

struct ContinueStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ContinueStatement;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ContinueStatement; }
  Node *label() const { return access::child(n, 0); }
};

struct LabeledStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::LabeledStatement;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::LabeledStatement; }
  Node *label() const { return access::child(n, 0); }
  Node *body() const { return access::child(n, 1); }
};

struct SwitchStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::SwitchStatement;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::SwitchStatement; }
  Node *discriminant() const { return access::child(n, 0); }
  span<Node *> cases() const { return access::tail(n, 1); }
};

struct SwitchCase : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::SwitchCase;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::SwitchCase; }
  Node *test() const { return access::child(n, 0); }
  span<Node *> consequent() const { return access::tail(n, 1); }
};

struct TryStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TryStatement;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TryStatement; }
  Node *block() const { return access::child(n, 0); }
  Node *handler() const { return access::child(n, 1); }
  Node *finalizer() const { return access::child(n, 2); }
};

struct CatchClause : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::CatchClause;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::CatchClause; }
  Node *param() const { return access::child(n, 0); }
  Node *body() const { return access::child(n, 1); }
};

struct WithStatement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::WithStatement;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::WithStatement; }
  Node *object() const { return access::child(n, 0); }
  Node *body() const { return access::child(n, 1); }
};

struct ImportDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ImportDeclaration;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ImportDeclaration; }
  Node *source() const { return access::child(n, 0); }
  Node *attributes() const { return access::child(n, 1); }
  span<Node *> specifiers() const { return access::tail(n, 2); }
  ImportKind importKind() const { return ImportKind(access::dataByte(n, 0)); }
};

struct ImportSpecifier : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ImportSpecifier;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ImportSpecifier; }
  Node *imported() const { return access::child(n, 0); }
  Node *local() const { return access::child(n, 1); }
  ImportKind importKind() const { return ImportKind(access::dataByte(n, 0)); }
};

struct ImportDefaultSpecifier : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ImportDefaultSpecifier;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ImportDefaultSpecifier; }
  Node *local() const { return access::child(n, 0); }
};

struct ImportNamespaceSpecifier : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ImportNamespaceSpecifier;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ImportNamespaceSpecifier; }
  Node *local() const { return access::child(n, 0); }
};

struct ImportAttributes : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ImportAttributes;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ImportAttributes; }
  span<Node *> attributes() const { return access::tail(n, 0); }
};

struct ImportAttribute : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ImportAttribute;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ImportAttribute; }
  Node *key() const { return access::child(n, 0); }
  Node *value() const { return access::child(n, 1); }
};

struct ExportNamedDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ExportNamedDeclaration;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ExportNamedDeclaration; }
  Node *declaration() const { return access::child(n, 0); }
  Node *source() const { return access::child(n, 1); }
  Node *attributes() const { return access::child(n, 2); }
  span<Node *> specifiers() const { return access::tail(n, 3); }
  ImportKind exportKind() const { return ImportKind(access::dataByte(n, 0)); }
};

struct ExportSpecifier : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ExportSpecifier;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ExportSpecifier; }
  Node *local() const { return access::child(n, 0); }
  Node *exported() const { return access::child(n, 1); }
  ImportKind exportKind() const { return ImportKind(access::dataByte(n, 0)); }
};

struct ExportDefaultDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ExportDefaultDeclaration;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ExportDefaultDeclaration; }
  Node *declaration() const { return access::child(n, 0); }
  ImportKind exportKind() const { return ImportKind(access::dataByte(n, 0)); }
};

struct ExportAllDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::ExportAllDeclaration;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::ExportAllDeclaration; }
  Node *exported() const { return access::child(n, 0); }
  Node *source() const { return access::child(n, 1); }
  Node *attributes() const { return access::child(n, 2); }
  ImportKind exportKind() const { return ImportKind(access::dataByte(n, 0)); }
};

struct JSXElement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXElement;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXElement; }
  Node *openingElement() const { return access::child(n, 0); }
  Node *closingElement() const { return access::child(n, 1); }
  span<Node *> children() const { return access::tail(n, 2); }
};

struct JSXFragment : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXFragment;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXFragment; }
  Node *openingFragment() const { return access::child(n, 0); }
  Node *closingFragment() const { return access::child(n, 1); }
  span<Node *> children() const { return access::tail(n, 2); }
};

struct JSXOpeningElement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXOpeningElement;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXOpeningElement; }
  Node *name() const { return access::child(n, 0); }
  Node *typeArguments() const { return access::child(n, 1); }
  span<Node *> attributes() const { return access::tail(n, 2); }
  bool isSelfClosing() const { return access::hasFlag(n, Flag::SelfClosing); }
};

struct JSXClosingElement : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXClosingElement;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXClosingElement; }
  Node *name() const { return access::child(n, 0); }
};

struct JSXOpeningFragment : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXOpeningFragment;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXOpeningFragment; }
};

struct JSXClosingFragment : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXClosingFragment;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXClosingFragment; }
};

struct JSXAttribute : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXAttribute;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXAttribute; }
  Node *name() const { return access::child(n, 0); }
  Node *value() const { return access::child(n, 1); }
};

struct JSXSpreadAttribute : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXSpreadAttribute;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXSpreadAttribute; }
  Node *argument() const { return access::child(n, 0); }
};

struct JSXExpressionContainer : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXExpressionContainer;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXExpressionContainer; }
  Node *expression() const { return access::child(n, 0); }
};

struct JSXEmptyExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXEmptyExpression;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXEmptyExpression; }
};

struct JSXSpreadChild : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXSpreadChild;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXSpreadChild; }
  Node *expression() const { return access::child(n, 0); }
};

struct JSXText : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXText;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXText; }
  string_view text() const { return access::text(n); }
};

struct JSXIdentifier : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXIdentifier;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXIdentifier; }
  string_view text() const { return access::text(n); }
};

struct JSXMemberExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXMemberExpression;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXMemberExpression; }
  Node *object() const { return access::child(n, 0); }
  Node *property() const { return access::child(n, 1); }
};

struct JSXNamespacedName : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::JSXNamespacedName;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::JSXNamespacedName; }
  Node *namespaceName() const { return access::child(n, 0); }
  Node *name() const { return access::child(n, 1); }
};

struct TSAsExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSAsExpression;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSAsExpression; }
  Node *expression() const { return access::child(n, 0); }
  Node *typeAnnotation() const { return access::child(n, 1); }
};

struct TSSatisfiesExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSSatisfiesExpression;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSSatisfiesExpression; }
  Node *expression() const { return access::child(n, 0); }
  Node *typeAnnotation() const { return access::child(n, 1); }
};

struct TSNonNullExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSNonNullExpression;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSNonNullExpression; }
  Node *expression() const { return access::child(n, 0); }
};

struct TSTypeAssertion : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSTypeAssertion;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSTypeAssertion; }
  Node *typeAnnotation() const { return access::child(n, 0); }
  Node *expression() const { return access::child(n, 1); }
};

struct TSInstantiationExpression : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSInstantiationExpression;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSInstantiationExpression; }
  Node *expression() const { return access::child(n, 0); }
  Node *typeArguments() const { return access::child(n, 1); }
};

struct TSTypeParameterDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSTypeParameterDeclaration;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSTypeParameterDeclaration; }
  span<Node *> params() const { return access::tail(n, 0); }
};

struct TSTypeParameter : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSTypeParameter;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSTypeParameter; }
  Node *constraint() const { return access::child(n, 0); }
  Node *defaultType() const { return access::child(n, 1); }
  string_view text() const { return access::text(n); }
  bool isIn() const { return access::hasFlag(n, Flag::In); }
  bool isOut() const { return access::hasFlag(n, Flag::Out); }
  bool isConst() const { return access::hasFlag(n, Flag::Const); }
};

struct TSTypeParameterInstantiation : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSTypeParameterInstantiation;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSTypeParameterInstantiation; }
  span<Node *> params() const { return access::tail(n, 0); }
};

struct TSInterfaceDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSInterfaceDeclaration;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSInterfaceDeclaration; }
  Node *id() const { return access::child(n, 0); }
  Node *typeParameters() const { return access::child(n, 1); }
  Node *body() const { return access::child(n, 2); }
  span<Node *> extends() const { return access::tail(n, 3); }
  bool isDeclare() const { return access::hasFlag(n, Flag::Declare); }
};

struct TSInterfaceBody : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSInterfaceBody;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSInterfaceBody; }
  span<Node *> body() const { return access::tail(n, 0); }
};

struct TSInterfaceHeritage : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSInterfaceHeritage;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSInterfaceHeritage; }
  Node *expression() const { return access::child(n, 0); }
  Node *typeArguments() const { return access::child(n, 1); }
};

struct TSClassImplements : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSClassImplements;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSClassImplements; }
  Node *expression() const { return access::child(n, 0); }
  Node *typeArguments() const { return access::child(n, 1); }
};

struct TSTypeAliasDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSTypeAliasDeclaration;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSTypeAliasDeclaration; }
  Node *id() const { return access::child(n, 0); }
  Node *typeParameters() const { return access::child(n, 1); }
  Node *typeAnnotation() const { return access::child(n, 2); }
  bool isDeclare() const { return access::hasFlag(n, Flag::Declare); }
};

struct TSEnumDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSEnumDeclaration;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSEnumDeclaration; }
  Node *id() const { return access::child(n, 0); }
  span<Node *> members() const { return access::tail(n, 1); }
  bool isConst() const { return access::hasFlag(n, Flag::Const); }
  bool isDeclare() const { return access::hasFlag(n, Flag::Declare); }
};

struct TSEnumMember : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSEnumMember;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSEnumMember; }
  Node *id() const { return access::child(n, 0); }
  Node *initializer() const { return access::child(n, 1); }
  bool isComputed() const { return access::hasFlag(n, Flag::Computed); }
};

struct TSModuleDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSModuleDeclaration;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSModuleDeclaration; }
  Node *id() const { return access::child(n, 0); }
  Node *body() const { return access::child(n, 1); }
  ModuleKind kind() const { return ModuleKind(access::dataByte(n, 0)); }
  bool isDeclare() const { return access::hasFlag(n, Flag::Declare); }
};

struct TSModuleBlock : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSModuleBlock;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSModuleBlock; }
  span<Node *> body() const { return access::tail(n, 0); }
};

struct TSImportEqualsDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSImportEqualsDeclaration;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSImportEqualsDeclaration; }
  Node *id() const { return access::child(n, 0); }
  Node *moduleReference() const { return access::child(n, 1); }
  ImportKind importKind() const { return ImportKind(access::dataByte(n, 0)); }
};

struct TSExternalModuleReference : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSExternalModuleReference;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSExternalModuleReference; }
  Node *expression() const { return access::child(n, 0); }
};

struct TSExportAssignment : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSExportAssignment;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSExportAssignment; }
  Node *expression() const { return access::child(n, 0); }
};

struct TSNamespaceExportDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSNamespaceExportDeclaration;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSNamespaceExportDeclaration; }
  Node *id() const { return access::child(n, 0); }
};

struct TSKeywordType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSKeywordType;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSKeywordType; }
  Keyword keyword() const { return Keyword(access::dataByte(n, 0)); }
};

struct TSThisType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSThisType;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSThisType; }
};

struct TSTypeReference : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSTypeReference;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSTypeReference; }
  Node *typeName() const { return access::child(n, 0); }
  Node *typeArguments() const { return access::child(n, 1); }
};

struct TSQualifiedName : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSQualifiedName;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSQualifiedName; }
  Node *left() const { return access::child(n, 0); }
  Node *right() const { return access::child(n, 1); }
};

struct TSUnionType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSUnionType;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSUnionType; }
  span<Node *> types() const { return access::tail(n, 0); }
};

struct TSIntersectionType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSIntersectionType;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSIntersectionType; }
  span<Node *> types() const { return access::tail(n, 0); }
};

struct TSFunctionType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSFunctionType;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSFunctionType; }
  Node *typeParameters() const { return access::child(n, 0); }
  Node *returnType() const { return access::child(n, 1); }
  span<Node *> params() const { return access::tail(n, 2); }
};

struct TSConstructorType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSConstructorType;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSConstructorType; }
  Node *typeParameters() const { return access::child(n, 0); }
  Node *returnType() const { return access::child(n, 1); }
  span<Node *> params() const { return access::tail(n, 2); }
  bool isAbstract() const { return access::hasFlag(n, Flag::Abstract); }
};

struct TSConditionalType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSConditionalType;
  static constexpr int fixedChildren = 4;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSConditionalType; }
  Node *checkType() const { return access::child(n, 0); }
  Node *extendsType() const { return access::child(n, 1); }
  Node *trueType() const { return access::child(n, 2); }
  Node *falseType() const { return access::child(n, 3); }
};

struct TSInferType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSInferType;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSInferType; }
  Node *typeParameter() const { return access::child(n, 0); }
};

struct TSMappedType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSMappedType;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSMappedType; }
  Node *typeParameter() const { return access::child(n, 0); }
  Node *nameType() const { return access::child(n, 1); }
  Node *typeAnnotation() const { return access::child(n, 2); }
  Modifier readonlyModifier() const { return Modifier(access::dataByte(n, 0)); }
  Modifier optionalModifier() const { return Modifier(access::dataByte(n, 1)); }
};

struct TSIndexedAccessType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSIndexedAccessType;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSIndexedAccessType; }
  Node *objectType() const { return access::child(n, 0); }
  Node *indexType() const { return access::child(n, 1); }
};

struct TSTypeLiteral : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSTypeLiteral;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSTypeLiteral; }
  span<Node *> members() const { return access::tail(n, 0); }
};

struct TSArrayType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSArrayType;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSArrayType; }
  Node *elementType() const { return access::child(n, 0); }
};

struct TSTupleType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSTupleType;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSTupleType; }
  span<Node *> elementTypes() const { return access::tail(n, 0); }
};

struct TSNamedTupleMember : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSNamedTupleMember;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSNamedTupleMember; }
  Node *label() const { return access::child(n, 0); }
  Node *elementType() const { return access::child(n, 1); }
  bool isOptional() const { return access::hasFlag(n, Flag::Optional); }
};

struct TSOptionalType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSOptionalType;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSOptionalType; }
  Node *typeAnnotation() const { return access::child(n, 0); }
};

struct TSRestType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSRestType;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSRestType; }
  Node *typeAnnotation() const { return access::child(n, 0); }
};

struct TSTypeOperator : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSTypeOperator;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSTypeOperator; }
  Node *typeAnnotation() const { return access::child(n, 0); }
  TypeOperator op() const { return TypeOperator(access::dataByte(n, 0)); }
};

struct TSTypeQuery : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSTypeQuery;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSTypeQuery; }
  Node *exprName() const { return access::child(n, 0); }
  Node *typeArguments() const { return access::child(n, 1); }
};

struct TSTypePredicate : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSTypePredicate;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSTypePredicate; }
  Node *parameterName() const { return access::child(n, 0); }
  Node *typeAnnotation() const { return access::child(n, 1); }
  bool isAsserts() const { return access::hasFlag(n, Flag::Asserts); }
};

struct TSLiteralType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSLiteralType;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSLiteralType; }
  Node *literal() const { return access::child(n, 0); }
};

struct TSTemplateLiteralType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSTemplateLiteralType;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSTemplateLiteralType; }
  span<Node *> parts() const { return access::tail(n, 0); }
};

struct TSImportType : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSImportType;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSImportType; }
  Node *argument() const { return access::child(n, 0); }
  Node *qualifier() const { return access::child(n, 1); }
  Node *typeArguments() const { return access::child(n, 2); }
};

struct TSPropertySignature : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSPropertySignature;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSPropertySignature; }
  Node *key() const { return access::child(n, 0); }
  Node *typeAnnotation() const { return access::child(n, 1); }
  bool isComputed() const { return access::hasFlag(n, Flag::Computed); }
  bool isOptional() const { return access::hasFlag(n, Flag::Optional); }
  bool isReadonly() const { return access::hasFlag(n, Flag::Readonly); }
};

struct TSMethodSignature : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSMethodSignature;
  static constexpr int fixedChildren = 3;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSMethodSignature; }
  Node *key() const { return access::child(n, 0); }
  Node *typeParameters() const { return access::child(n, 1); }
  Node *returnType() const { return access::child(n, 2); }
  span<Node *> params() const { return access::tail(n, 3); }
  MethodKind kind() const { return MethodKind(access::dataByte(n, 0)); }
  bool isComputed() const { return access::hasFlag(n, Flag::Computed); }
  bool isOptional() const { return access::hasFlag(n, Flag::Optional); }
};

struct TSCallSignatureDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSCallSignatureDeclaration;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSCallSignatureDeclaration; }
  Node *typeParameters() const { return access::child(n, 0); }
  Node *returnType() const { return access::child(n, 1); }
  span<Node *> params() const { return access::tail(n, 2); }
};

struct TSConstructSignatureDeclaration : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSConstructSignatureDeclaration;
  static constexpr int fixedChildren = 2;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSConstructSignatureDeclaration; }
  Node *typeParameters() const { return access::child(n, 0); }
  Node *returnType() const { return access::child(n, 1); }
  span<Node *> params() const { return access::tail(n, 2); }
};

struct TSIndexSignature : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::TSIndexSignature;
  static constexpr int fixedChildren = 1;
  static constexpr bool hasList = true;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::TSIndexSignature; }
  Node *typeAnnotation() const { return access::child(n, 0); }
  span<Node *> parameters() const { return access::tail(n, 1); }
  bool isReadonly() const { return access::hasFlag(n, Flag::Readonly); }
  bool isStatic() const { return access::hasFlag(n, Flag::Static); }
};

struct Error : View {
  using View::View;
  static constexpr NodeKind nodeKind = NodeKind::Error;
  static constexpr int fixedChildren = 0;
  static constexpr bool hasList = false;
  static constexpr bool matches(NodeKind k) { return k == NodeKind::Error; }
};

struct FunctionLike : View {
  using View::View;
  static constexpr bool matches(NodeKind k) {
    return k == NodeKind::FunctionDeclaration || k == NodeKind::FunctionExpression || k == NodeKind::ArrowFunctionExpression || k == NodeKind::TSDeclareFunction || k == NodeKind::TSEmptyBodyFunctionExpression;
  }
  Node *id() const { return access::child(n, 0); }
  Node *typeParameters() const { return access::child(n, 1); }
  Node *returnType() const { return access::child(n, 2); }
  Node *body() const { return access::child(n, 3); }
  span<Node *> params() const { return access::tail(n, 4); }
  bool isAsync() const { return access::hasFlag(n, Flag::Async); }
  bool isGenerator() const { return access::hasFlag(n, Flag::Generator); }
};

struct ClassLike : View {
  using View::View;
  static constexpr bool matches(NodeKind k) {
    return k == NodeKind::ClassDeclaration || k == NodeKind::ClassExpression;
  }
  Node *decorators() const { return access::child(n, 0); }
  Node *id() const { return access::child(n, 1); }
  Node *typeParameters() const { return access::child(n, 2); }
  Node *superClass() const { return access::child(n, 3); }
  Node *superTypeArguments() const { return access::child(n, 4); }
  Node *body() const { return access::child(n, 5); }
  span<Node *> implements() const { return access::tail(n, 6); }
  bool isAbstract() const { return access::hasFlag(n, Flag::Abstract); }
};

struct Loop : View {
  using View::View;
  static constexpr bool matches(NodeKind k) {
    return k == NodeKind::ForStatement || k == NodeKind::ForInStatement || k == NodeKind::ForOfStatement || k == NodeKind::WhileStatement || k == NodeKind::DoWhileStatement;
  }
  Node *body() const {
    switch (access::kind(n)) {
      case NodeKind::ForStatement: return access::child(n, 3);
      case NodeKind::ForInStatement: return access::child(n, 2);
      case NodeKind::ForOfStatement: return access::child(n, 2);
      case NodeKind::WhileStatement: return access::child(n, 1);
      case NodeKind::DoWhileStatement: return access::child(n, 0);
      default: return {};
    }
  }
};

struct NamedDeclaration : View {
  using View::View;
  static constexpr bool matches(NodeKind k) {
    return k == NodeKind::FunctionDeclaration || k == NodeKind::ClassDeclaration || k == NodeKind::TSInterfaceDeclaration || k == NodeKind::TSTypeAliasDeclaration || k == NodeKind::TSEnumDeclaration || k == NodeKind::TSModuleDeclaration;
  }
  Node *id() const {
    switch (access::kind(n)) {
      case NodeKind::FunctionDeclaration: return access::child(n, 0);
      case NodeKind::ClassDeclaration: return access::child(n, 1);
      case NodeKind::TSInterfaceDeclaration: return access::child(n, 0);
      case NodeKind::TSTypeAliasDeclaration: return access::child(n, 0);
      case NodeKind::TSEnumDeclaration: return access::child(n, 0);
      case NodeKind::TSModuleDeclaration: return access::child(n, 0);
      default: return {};
    }
  }
  bool isDeclare() const { return access::hasFlag(n, Flag::Declare); }
};

struct ClassMember : View {
  using View::View;
  static constexpr bool matches(NodeKind k) {
    return k == NodeKind::MethodDefinition || k == NodeKind::TSAbstractMethodDefinition || k == NodeKind::PropertyDefinition || k == NodeKind::TSAbstractPropertyDefinition || k == NodeKind::AccessorProperty || k == NodeKind::TSAbstractAccessorProperty;
  }
  Node *decorators() const { return access::child(n, 0); }
  Node *key() const { return access::child(n, 1); }
  Node *value() const {
    switch (access::kind(n)) {
      case NodeKind::MethodDefinition: return access::child(n, 2);
      case NodeKind::TSAbstractMethodDefinition: return access::child(n, 2);
      case NodeKind::PropertyDefinition: return access::child(n, 3);
      case NodeKind::TSAbstractPropertyDefinition: return access::child(n, 3);
      case NodeKind::AccessorProperty: return access::child(n, 3);
      case NodeKind::TSAbstractAccessorProperty: return access::child(n, 3);
      default: return {};
    }
  }
  Accessibility accessibility() const {
    switch (access::kind(n)) {
      case NodeKind::MethodDefinition: return Accessibility(access::dataByte(n, 1));
      case NodeKind::TSAbstractMethodDefinition: return Accessibility(access::dataByte(n, 1));
      case NodeKind::PropertyDefinition: return Accessibility(access::dataByte(n, 0));
      case NodeKind::TSAbstractPropertyDefinition: return Accessibility(access::dataByte(n, 0));
      case NodeKind::AccessorProperty: return Accessibility(access::dataByte(n, 0));
      case NodeKind::TSAbstractAccessorProperty: return Accessibility(access::dataByte(n, 0));
      default: return {};
    }
  }
  bool isStatic() const { return access::hasFlag(n, Flag::Static); }
  bool isComputed() const { return access::hasFlag(n, Flag::Computed); }
  bool isOverride() const { return access::hasFlag(n, Flag::Override); }
  bool isOptional() const { return access::hasFlag(n, Flag::Optional); }
};

struct SignatureLike : View {
  using View::View;
  static constexpr bool matches(NodeKind k) {
    return k == NodeKind::TSFunctionType || k == NodeKind::TSConstructorType || k == NodeKind::TSCallSignatureDeclaration || k == NodeKind::TSConstructSignatureDeclaration || k == NodeKind::TSMethodSignature;
  }
  Node *typeParameters() const {
    switch (access::kind(n)) {
      case NodeKind::TSFunctionType: return access::child(n, 0);
      case NodeKind::TSConstructorType: return access::child(n, 0);
      case NodeKind::TSCallSignatureDeclaration: return access::child(n, 0);
      case NodeKind::TSConstructSignatureDeclaration: return access::child(n, 0);
      case NodeKind::TSMethodSignature: return access::child(n, 1);
      default: return {};
    }
  }
  Node *returnType() const {
    switch (access::kind(n)) {
      case NodeKind::TSFunctionType: return access::child(n, 1);
      case NodeKind::TSConstructorType: return access::child(n, 1);
      case NodeKind::TSCallSignatureDeclaration: return access::child(n, 1);
      case NodeKind::TSConstructSignatureDeclaration: return access::child(n, 1);
      case NodeKind::TSMethodSignature: return access::child(n, 2);
      default: return {};
    }
  }
  span<Node *> params() const {
    switch (access::kind(n)) {
      case NodeKind::TSFunctionType: return access::tail(n, 2);
      case NodeKind::TSConstructorType: return access::tail(n, 2);
      case NodeKind::TSCallSignatureDeclaration: return access::tail(n, 2);
      case NodeKind::TSConstructSignatureDeclaration: return access::tail(n, 2);
      case NodeKind::TSMethodSignature: return access::tail(n, 3);
      default: return {};
    }
  }
};

} // namespace fastlint::ast
