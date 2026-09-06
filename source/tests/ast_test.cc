#include "fastlint/ast/file.h"
#include "fastlint/ast/generated/views.h"
#include "fastlint/ast/kind_info.h"
#include "testing/test.h"

#include <string>

using namespace fastlint;
using namespace fastlint::ast;

namespace {

Node *identifier(AstFile &file, std::string_view name)
{
  Node *node = file.make(NodeKind::Identifier);
  node->text = file.intern(name);
  node->appendChild(nullptr);
  return node;
}

} // namespace

TEST(ast, views_read_child_slots)
{
  AstFile file;
  Node *callee = identifier(file, "foo");
  Node *arg = identifier(file, "bar");
  Node *call = file.make(NodeKind::CallExpression);
  call->appendChild(callee);
  call->appendChild(nullptr);
  call->appendChild(arg);

  CallExpression view = call->as<CallExpression>();
  CHECK(bool(view));
  CHECK(view.callee() == callee);
  CHECK(view.typeArguments() == nullptr);
  CHECK_EQ(int(view.arguments().size()), 1);
  CHECK(view.arguments()[0] == arg);
  CHECK(!view.isOptional());
  CHECK(arg->parent == call);
  CHECK(call->is<CallExpression>());
  CHECK(!call->is<MemberExpression>());
  CHECK(!bool(call->as<MemberExpression>()));
  CHECK_EQ(int(CallExpression::fixedChildren), 2);
  CHECK(CallExpression::hasList);
}

TEST(ast, flags_and_enum_fields)
{
  AstFile file;
  Node *decl = file.make(NodeKind::VariableDeclaration);
  decl->setDataByte(0, uint8_t(VariableKind::Const));
  decl->setFlag(Flag::Declare);

  VariableDeclaration view = decl->as<VariableDeclaration>();
  CHECK(view.kind() == VariableKind::Const);
  CHECK(view.isDeclare());
  CHECK_EQ(int(view.declarations().size()), 0);

  decl->setFlag(Flag::Declare, false);
  CHECK(!view.isDeclare());

  Node *method = file.make(NodeKind::MethodDefinition);
  method->setDataByte(0, uint8_t(MethodKind::Get));
  method->setDataByte(1, uint8_t(Accessibility::Private));
  MethodDefinition m = method->as<MethodDefinition>();
  CHECK(m.kind() == MethodKind::Get);
  CHECK(m.accessibility() == Accessibility::Private);
}

TEST(ast, union_views_switch_on_kind)
{
  AstFile file;
  Node *test = identifier(file, "x");
  Node *body = file.make(NodeKind::BlockStatement);

  // `body` is slot 1 of a while loop and slot 0 of a do-while loop.
  Node *loop = file.make(NodeKind::WhileStatement);
  loop->appendChild(test);
  loop->appendChild(body);
  CHECK(loop->as<Loop>().body() == body);

  Node *doLoop = file.make(NodeKind::DoWhileStatement);
  doLoop->appendChild(body);
  doLoop->appendChild(test);
  CHECK(doLoop->as<Loop>().body() == body);
  CHECK(doLoop->is<Loop>());
  CHECK(!test->is<Loop>());

  Node *fn = file.make(NodeKind::ArrowFunctionExpression);
  for (int i = 0; i < 4; i++) {
    fn->appendChild(nullptr);
  }
  fn->appendChild(identifier(file, "p"));
  FunctionLike f = fn->as<FunctionLike>();
  CHECK(f.id() == nullptr);
  CHECK_EQ(int(f.params().size()), 1);
  CHECK(!f.isAsync());
}

TEST(ast, kind_tables)
{
  CHECK_EQ(std::string(kindName(NodeKind::CallExpression)), "CallExpression");
  const KindInfo &call = kindInfo(NodeKind::CallExpression);
  CHECK_EQ(int(call.fixedChildren), 2);
  CHECK(call.hasList);
  CHECK(!call.nullableElements);
  CHECK_EQ(int(call.childCount), 3);
  CHECK_EQ(std::string(call.childNames[2]), "arguments");
  CHECK(bool(call.flagMask & uint32_t(Flag::Optional)));

  const KindInfo &array = kindInfo(NodeKind::ArrayExpression);
  CHECK(array.nullableElements);

  const KindInfo &decl = kindInfo(NodeKind::VariableDeclaration);
  CHECK_EQ(int(decl.enumCount), 1);
  CHECK_EQ(std::string(decl.enums[0].name), "kind");
  CHECK_EQ(std::string(decl.enums[0].values[int(VariableKind::AwaitUsing)]),
           "awaitUsing");

  CHECK(kindInfo(NodeKind::Identifier).usesText);
  CHECK_EQ(std::string(flagName(int(0))), "optional");
  CHECK_EQ(std::string(flagName(flagCount)), "");
}

TEST(ast, intern_copies_text)
{
  AstFile file;
  std::string source = "hello";
  string_view interned = file.intern(source);
  source[0] = 'j';
  CHECK_EQ(std::string(interned), "hello");
  CHECK(interned.data() != source.data());

  // A string larger than one chunk gets its own.
  std::string big(10000, 'x');
  CHECK_EQ(file.intern(big).size(), big.size());
  CHECK_EQ(std::string(file.intern("after")), "after");
}
