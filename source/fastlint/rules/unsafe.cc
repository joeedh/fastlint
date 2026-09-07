#include "fastlint/rules/unsafe.h"

#include "fastlint/ast/generated/views.h"
#include "fastlint/tsgo/generated/enums.h"

namespace fastlint::rules::unsafe {

using ast::Node;
using ast::NodeKind;

Node *thisExpressionOf(Node *node)
{
  while (node) {
    switch (node->kind) {
    case NodeKind::CallExpression:
      node = ast::CallExpression(node).callee();
      break;
    case NodeKind::MemberExpression:
      node = ast::MemberExpression(node).object();
      break;
    case NodeKind::ThisExpression:
      return node;
    default:
      return nullptr;
    }
  }
  return nullptr;
}

TypeId constrained(TypeFacts &facts, TypeId type)
{
  if (!facts.isTypeParameter(type)) {
    return type;
  }
  TypeId constraint = facts.constraintOf(type);
  return constraint ? constraint : type;
}

void unionParts(TypeFacts &facts, TypeId type, Vector<TypeId, 4> &out)
{
  out.clear();
  litestl::util::span<const TypeId> members = facts.unionMembers(type);
  if (members.size() == 0) {
    out.append(type);
    return;
  }
  for (TypeId member : members) {
    out.append(member);
  }
}

bool isAnyArray(TypeFacts &facts, TypeId type)
{
  if (!facts.isArray(type)) {
    return false;
  }
  litestl::util::span<const TypeId> args = facts.typeArguments(type);
  return args.size() > 0 && facts.isAny(args[0]);
}

bool isUnknownArray(TypeFacts &facts, TypeId type)
{
  if (!facts.isArray(type)) {
    return false;
  }
  litestl::util::span<const TypeId> args = facts.typeArguments(type);
  return args.size() > 0 && facts.isUnknown(args[0]);
}

namespace {

bool isBareNewMap(const Node *node)
{
  if (!node || node->kind != NodeKind::NewExpression) {
    return false;
  }
  ast::NewExpression view(const_cast<Node *>(node));
  Node *callee = view.callee();
  return callee->kind == NodeKind::Identifier && callee->text == "Map" &&
         view.arguments().size() == 0 && !view.typeArguments();
}

bool isUnsafeAssignmentWorker(TypeFacts &facts,
                              TypeId sender,
                              TypeId receiver,
                              const Node *senderNode,
                              Vector<UnsafePair, 8> &visited,
                              int depth,
                              UnsafePair &out)
{
  // A recursive generic (`type T = [number, T[]]`) can intern to fresh rows each hop, so
  // the visited set alone will not stop the walk.
  if (depth > 8) {
    return false;
  }
  if (facts.isAny(sender)) {
    if (facts.isUnknown(receiver)) {
      return false;
    }
    if (!facts.isAny(receiver)) {
      out = {sender, receiver};
      return true;
    }
  }
  for (const UnsafePair &seen : visited) {
    if (seen.sender == sender && seen.receiver == receiver) {
      return false;
    }
  }
  visited.append({sender, receiver});

  constexpr uint32_t kReference = tsgo::ObjectFlags::Reference;
  if (!(facts.objectFlags(sender) & kReference) ||
      !(facts.objectFlags(receiver) & kReference))
  {
    return false;
  }
  // Different generics are assumed safe: their argument positions need not line up.
  TypeId target = facts.targetOf(sender);
  if (!target || target != facts.targetOf(receiver)) {
    return false;
  }
  if (isBareNewMap(senderNode)) {
    return false;
  }
  litestl::util::span<const TypeId> senderArgs = facts.typeArguments(sender);
  litestl::util::span<const TypeId> receiverArgs = facts.typeArguments(receiver);
  for (size_t i = 0; i < senderArgs.size() && i < receiverArgs.size(); i++) {
    UnsafePair inner;
    if (isUnsafeAssignmentWorker(
            facts, senderArgs[i], receiverArgs[i], senderNode, visited, depth + 1, inner))
    {
      out = {sender, receiver};
      return true;
    }
  }
  return false;
}

/** Union and intersection members of `type`, or `type` itself. */
void constituents(TypeFacts &facts, TypeId type, Vector<TypeId, 4> &out)
{
  out.clear();
  litestl::util::span<const TypeId> members = facts.unionMembers(type);
  if (members.size() == 0) {
    out.append(type);
    return;
  }
  for (TypeId member : members) {
    out.append(member);
  }
}

AnyKind discriminateAnyWorker(TypeFacts &facts, TypeId type, Vector<TypeId, 8> &visited)
{
  for (TypeId seen : visited) {
    if (seen == type) {
      return AnyKind::Safe;
    }
  }
  visited.append(type);
  if (facts.isAny(type)) {
    return AnyKind::Any;
  }
  if (isAnyArray(facts, type)) {
    return AnyKind::AnyArray;
  }
  Vector<TypeId, 4> parts;
  constituents(facts, type, parts);
  for (TypeId part : parts) {
    if (!facts.isThenable(part, 1)) {
      continue;
    }
    TypeId awaited = facts.awaitedType(part);
    if (awaited && discriminateAnyWorker(facts, awaited, visited) == AnyKind::Any) {
      return AnyKind::PromiseAny;
    }
  }
  return AnyKind::Safe;
}

} // namespace

bool isUnsafeAssignment(TypeFacts &facts,
                        TypeId sender,
                        TypeId receiver,
                        const Node *senderNode,
                        UnsafePair &out)
{
  out = UnsafePair();
  if (!sender || !receiver) {
    return false;
  }
  Vector<UnsafePair, 8> visited;
  return isUnsafeAssignmentWorker(facts, sender, receiver, senderNode, visited, 0, out);
}

AnyKind discriminateAny(TypeFacts &facts, TypeId type)
{
  if (!type) {
    return AnyKind::Safe;
  }
  Vector<TypeId, 8> visited;
  return discriminateAnyWorker(facts, type, visited);
}

std::string_view Texts::describe(TypeFacts &facts, TypeId type)
{
  if (facts.isErrorType(type)) {
    return "error typed";
  }
  return quoted(facts, type);
}

std::string_view Texts::quoted(TypeFacts &facts, TypeId type)
{
  string text;
  text += '`';
  text += facts.typeToString(type);
  text += '`';
  return intern(text);
}

} // namespace fastlint::rules::unsafe
