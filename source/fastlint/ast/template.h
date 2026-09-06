#pragma once

// Code templates for fixers (docs/ast-design.md "Templates"): a snippet with
// `$name` placeholders, parsed once, then instantiated into a file or matched
// against a subtree.

#include "fastlint/ast/file.h"
#include "fastlint/ast/node.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/tree.h"
#include "util/string.h"
#include "util/vector.h"

#include <initializer_list>
#include <string_view>

namespace fastlint::ast {

using litestl::util::string;

/** The syntactic position a placeholder occupies in its template. */
enum class SlotKind : uint8_t {
  /** Any expression. */
  Expression,
  /** A whole statement, or an expression that becomes an expression statement. */
  Statement,
  /** Any type, or an identifier or qualified name standing for a type name. */
  Type,
  /** An identifier only: a declaration name, label, type name or specifier. */
  Name,
  /** An identifier or private identifier after a dot. */
  Property,
  /** A non-computed key: identifier, private identifier or literal. */
  PropertyName,
  /** A binding pattern. */
  Pattern,
  /** An assignment target: a pattern or a member expression. */
  Assignable,
  /** A list whose elements are not checked, such as object members. */
  Any,
};

/** One argument for `instantiate` or one result of `match`. */
struct TemplateArgItem {
  string_view name;
  Node *node;
};

struct TemplateArg {
  string_view name;
  Vector<Node *, 2> nodes;
};

/** Placeholder names and the nodes bound to them; a splice binds several. */
class TemplateArgs {
public:
  TemplateArgs() = default;
  TemplateArgs(std::initializer_list<TemplateArgItem> items)
  {
    for (const TemplateArgItem &item : items) {
      set(item.name, item.node);
    }
  }

  void set(string_view name, Node *node);
  void set(string_view name, span<Node *const> nodes);
  /** The single node bound to `name`, or null when none or several are. */
  Node *get(string_view name) const;
  span<Node *const> list(string_view name) const;
  bool has(string_view name) const;
  void clear()
  {
    m_items.clear();
  }
  span<const TemplateArg> items() const
  {
    return {const_cast<Vector<TemplateArg, 4> &>(m_items).data(), m_items.size()};
  }

private:
  Vector<TemplateArg, 4> m_items;

  TemplateArg *find(string_view name) const;
};

struct Placeholder {
  /** The text after the leading `$`, without a splice's trailing `$`. */
  string_view name;
  SlotKind slot;
  bool splice;
  /** The placeholder identifier in the prototype. */
  const Node *id;
};

class Template {
public:
  enum class Mode : uint8_t {
    /** Expression for a lone expression statement, Statement for one other
     * statement, Statements otherwise. */
    Auto,
    Expression,
    Statement,
    /** A list of statements; instantiate with `instantiateAll`. */
    Statements,
    Type,
  };

  /**
   * Parses `text` once per mode and keeps the result for the life of the
   * process. A template that fails to parse (or whose placeholders sit in
   * positions the engine cannot fill) reports `ok() == false`.
   */
  static const Template *compile(string_view text, Mode mode = Mode::Auto);

  bool ok() const
  {
    return m_ok;
  }
  Mode mode() const
  {
    return m_mode;
  }
  const Node *prototype() const
  {
    return m_proto;
  }
  span<const Placeholder> placeholders() const
  {
    return {const_cast<Vector<Placeholder, 4> &>(m_placeholders).data(),
            m_placeholders.size()};
  }
  const syntax::Diagnostics &diagnostics() const
  {
    return m_diagnostics;
  }

  /**
   * Clones the prototype into `file` with every placeholder replaced by its
   * binding, reparenting the bound nodes. Returns null, and moves nothing,
   * when a binding is missing or its kind does not fit the placeholder's
   * slot. Arguments that need parentheses in their slot get the
   * `parenthesized` flag.
   */
  Node *instantiate(AstFile &file, const TemplateArgs &bindings) const;

  /** As `instantiate` for a Statements template: the statements land in `out`. */
  bool instantiateAll(AstFile &file,
                      const TemplateArgs &bindings,
                      Vector<Node *, 4> &out) const;

  /**
   * Whether `node` has the prototype's shape, ignoring trivia and the
   * `parenthesized` flag. Binds each placeholder; a name that occurs twice
   * must meet structurally equal subtrees. `bindings` is left partially
   * filled on failure.
   */
  bool match(Node *node, TemplateArgs &bindings) const;

private:
  Template(string_view text, Mode mode);
  Template(const Template &) = delete;
  Template &operator=(const Template &) = delete;

  string m_text;
  string m_source;
  /** The mode `compile` was called with; `m_mode` is the resolved one. */
  Mode m_requested;
  Mode m_mode;
  bool m_ok = false;
  syntax::Diagnostics m_diagnostics;
  syntax::GrammarTree m_tree;
  AstFile m_file;
  const Node *m_proto = nullptr;
  Vector<Placeholder, 4> m_placeholders;
  /** The next template whose key hashes to the same cache bucket. */
  Template *m_nextInBucket = nullptr;

  void collect(const Node *n);
  int placeholderIndex(const Node *id) const;
  const Placeholder *placeholderFor(const Node *id) const;
  bool matchNode(const Node *proto, Node *node, TemplateArgs &bindings) const;
  bool matchList(const Node *proto, Node *node, int from, TemplateArgs &bindings) const;
  Node *build(AstFile &file, const TemplateArgs &bindings) const;
};

/** Whether two subtrees are equal in kind, flags, data, text and children. */
bool equivalent(const Node *a, const Node *b);

/** A deep copy of `n` into `file`, keeping grammar links and spans. */
Node *cloneNode(AstFile &file, const Node *n);

} // namespace fastlint::ast
