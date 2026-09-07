#pragma once

// AST mutation for fixers (docs/ast-design.md "Mutation"). Every edit marks
// the touched node and its ancestors dirty; the printer reprints dirty
// nodes and copies clean ones verbatim.

#include "fastlint/ast/file.h"
#include "fastlint/ast/node.h"
#include "util/function.h"
#include "util/vector.h"

#include <initializer_list>

namespace fastlint::ast {

/** What `remove` does with the removed node's comments. */
enum class CommentPolicy : uint8_t {
  /**
   * Leading and dangling comments move to the next sibling (or the previous
   * sibling's trailing list when the node is last); a trailing comment on
   * the node's own line is dropped and any other trailing comment moves.
   */
  MoveLeading,
  /** As MoveLeading, but same-line trailing comments move too. Lossless. */
  KeepTrailing,
  DropAll,
};

/** Marks `node` and every ancestor dirty. */
void markDirty(Node *node);

/** Whether `node` still hangs off its file's root. */
bool isAttached(const AstFile &file, const Node *node);

class Fixer {
public:
  explicit Fixer(AstFile &file) : m_file(file)
  {
  }

  AstFile &file() const
  {
    return m_file;
  }

  // ------------------------------------------------------------ mutations
  // Each returns false on an authoring error (a required slot removed, an
  // insertion beside a fixed-slot child) and leaves the tree alone.

  /** Puts `fresh` where `old` is and moves `old`'s comments onto it. */
  bool replace(Node *old, Node *fresh);
  /** List slots only. */
  bool insertBefore(Node *sibling, Node *fresh);
  bool insertAfter(Node *sibling, Node *fresh);
  /** Appends to `parent`'s list slot. */
  bool append(Node *parent, Node *fresh);
  /** List slots and optional slots only; detaches `node` and applies `policy`. */
  bool remove(Node *node, CommentPolicy policy = CommentPolicy::MoveLeading);
  /** Sets fixed slot `index`; a null `fresh` clears an optional slot. */
  bool set(Node *parent, int index, Node *fresh);
  /**
   * Changes an enum byte of `node` (an operator, a declaration kind). The
   * node reprints from its kind template, since the token lives in its own
   * text; the children keep theirs.
   */
  void setData(Node *node, int index, uint8_t value);
  /** Sets or clears a flag of `node`, reprinting it as `setData` does. */
  void setFlag(Node *node, Flag flag, bool on);

  // ------------------------------------------------------------- builders
  // Synthesized nodes with no grammar link; the printer prints them from
  // per-kind templates.

  /** A node of `kind` with the given fixed slots and list elements; null on a
   * child count the kind cannot hold. */
  Node *build(NodeKind kind, std::initializer_list<Node *> children);
  Node *identifier(string_view name);
  /** `text` is the raw literal source, quotes included for strings. */
  Node *literal(LiteralKind kind, string_view text);
  /** Wraps `value` in double quotes; escapes are not added. */
  Node *stringLiteral(string_view value);
  Node *numberLiteral(string_view text);
  Node *booleanLiteral(bool value);
  Node *nullLiteral();
  Node *member(Node *object, Node *property, bool computed = false);
  Node *call(Node *callee, std::initializer_list<Node *> arguments);
  Node *call(Node *callee, span<Node *> arguments);
  Node *unary(UnaryOperator op, Node *argument);
  Node *binary(BinaryOperator op, Node *left, Node *right);
  Node *logical(LogicalOperator op, Node *left, Node *right);
  Node *awaitExpression(Node *argument);
  Node *expressionStatement(Node *expression);
  Node *returnStatement(Node *argument);
  Node *block(std::initializer_list<Node *> body);
  Node *variableDeclaration(VariableKind kind, Node *id, Node *init);
  Node *keywordType(Keyword keyword);
  Node *typeReference(Node *typeName);

  /**
   * Takes `node` out of its current parent, if any, so it can move. The
   * parentheses its old slot needed go with it; the slot it lands in adds
   * its own.
   */
  void detach(Node *node);

private:
  AstFile &m_file;

  /** Captures the layout of each ancestor turning dirty, then marks the chain. */
  void dirty(Node *node);
  /** Marks `node` dirty with no layout of its own, so it prints from its template. */
  void reprint(Node *node);
  /** Drops the parentheses `node` carried in its old slot; the new slot decides again. */
  void unparenthesize(Node *node);
  /** Links `fresh` under `parent` at `index` and parenthesizes it if the slot needs that.
   */
  void place(Node *parent, int index, Node *fresh);
  /** The index of `child` in `parent->children`, or -1. */
  static int indexOf(const Node *parent, const Node *child);
  static bool isListIndex(const Node *parent, int index);
  void moveRemovedComments(Node *node, Node *parent, int index, CommentPolicy policy);
};

/** One fix a rule proposed: a target node and the edits to make around it. */
struct Fix {
  Node *target;
  litestl::util::function<void(Fixer &)> apply;
};

struct FixReport {
  int applied = 0;
  /** Fixes skipped because an earlier fix in the pass had touched their target. */
  int deferred = 0;
};

/**
 * Applies `fixes` in source order of their targets. A fix whose target is
 * dirty or detached when its turn comes is skipped, so it is found again by
 * the next pass over the reprinted file.
 */
FixReport applyFixes(AstFile &file, span<Fix> fixes);

} // namespace fastlint::ast
