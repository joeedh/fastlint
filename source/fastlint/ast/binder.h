#pragma once

// Scopes, declarations and references over the AST (docs/ast-design.md
// "Binder"). A separate pass after lowering; fixers do not update it, the
// fixpoint driver rebinds.

#include "fastlint/ast/file.h"
#include "fastlint/ast/node.h"
#include "util/map.h"
#include "util/pool.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

namespace fastlint::ast {

using litestl::util::string;

struct Scope;
struct Declaration;
struct Reference;

enum class ScopeKind : uint8_t {
  Module,
  Function,
  /** A class body: type parameters and, for a class expression, its own name. */
  Class,
  Block,
  Switch,
  /** A `for` head that declares with `let`, `const` or `using`. */
  For,
  Catch,
  StaticBlock,
  Namespace,
  Enum,
  /** Type parameters of an interface, alias, mapped, conditional or function type. */
  Type,
};

enum class DeclKind : uint8_t {
  Var,
  Let,
  Const,
  Using,
  Function,
  Class,
  Parameter,
  CatchParam,
  Import,
  Interface,
  TypeAlias,
  Enum,
  EnumMember,
  Namespace,
  TypeParameter,
};

/** The namespaces a name lives in; a declaration may hold both. */
enum class Space : uint8_t {
  Value = 1,
  Type = 2,
  Either = 3,
};

struct Declaration {
  string_view name;
  /** The Identifier (or TSTypeParameter) that spells the name. */
  Node *id;
  /** The declaring construct: declarator, function, class, specifier, parameter. */
  Node *node;
  DeclKind kind;
  uint8_t spaces;
  Scope *scope;
  Vector<Reference *, 2> references;
  /** The next declaration of the same name in the same scope, or null. */
  Declaration *nextSameName = nullptr;

  bool inSpace(Space space) const
  {
    return (spaces & uint8_t(space)) != 0;
  }
  bool isVariable() const
  {
    return kind == DeclKind::Var || kind == DeclKind::Let || kind == DeclKind::Const ||
           kind == DeclKind::Using;
  }
};

struct Reference {
  enum Flags : uint8_t {
    Read = 1,
    Write = 2,
    /** The write that initializes a declaration (`let x = 1`, `for (const x of xs)`). */
    Init = 4,
    /** The name under `typeof` in a type, which may be a type-only import. */
    TypeQuery = 8,
  };

  Node *id;
  Scope *scope;
  /** Null when no declaration in the scope chain matched; see `Bindings::unresolved`. */
  Declaration *resolved = nullptr;
  Space space;
  uint8_t flags;

  string_view name() const
  {
    return id->text;
  }
  bool isRead() const
  {
    return (flags & Read) != 0;
  }
  bool isWrite() const
  {
    return (flags & Write) != 0;
  }
  bool isInit() const
  {
    return (flags & Init) != 0;
  }
};

/** A string key with the hash and equality litestl's Map needs. */
struct Name {
  string_view text;

  litestl::hash::HashInt computeHash() const
  {
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : text) {
      h = (h ^ c) * 0x100000001b3ull;
    }
    return h;
  }
  bool operator==(const Name &b) const
  {
    return text == b.text;
  }
};

struct Scope {
  ScopeKind kind;
  /** The node that opens the scope: Program, function, block, class body owner. */
  Node *node;
  Scope *parent;
  Vector<Scope *, 2> children;
  Vector<Declaration *, 4> declarations;
  Vector<Reference *, 4> references;
  Map<Name, Declaration *> byName;

  /** Whether `var` and function declarations hoist to this scope. */
  bool isVariableScope() const
  {
    return kind == ScopeKind::Module || kind == ScopeKind::Function ||
           kind == ScopeKind::StaticBlock || kind == ScopeKind::Namespace;
  }
  /** The nearest scope, this one included, that `var` hoists to. */
  Scope *variableScope();
  /** The first declaration of `name` in this scope alone that lives in `space`. */
  Declaration *lookupLocal(string_view name, Space space = Space::Either) const;
  /** Walks the chain from this scope outward. */
  Declaration *lookup(string_view name, Space space = Space::Either);
};

class Bindings {
public:
  Bindings() = default;
  Bindings(const Bindings &) = delete;
  Bindings &operator=(const Bindings &) = delete;

  Scope *moduleScope() const
  {
    return m_module;
  }
  /** The scope `node` opens, or null when it opens none. */
  Scope *scopeOf(const Node *node) const;
  /** The declaration whose name `id` spells, or null. */
  Declaration *declarationOf(const Node *id) const;
  /** The reference `id` makes, or null when the identifier is not one. */
  Reference *referenceOf(const Node *id) const;
  /** References that reached the module scope without a match, in source order. */
  span<Reference *const> unresolved() const
  {
    return {const_cast<Vector<Reference *> &>(m_unresolved).data(), m_unresolved.size()};
  }
  int scopeCount() const
  {
    return m_scopes.live_count();
  }
  int declarationCount() const
  {
    return m_declarations.live_count();
  }
  int referenceCount() const
  {
    return m_references.live_count();
  }

  void clear();

private:
  friend class Binder;

  Pool<Scope, 32> m_scopes;
  Pool<Declaration, 64> m_declarations;
  Pool<Reference, 128> m_references;
  Scope *m_module = nullptr;
  Map<const Node *, Scope *> m_scopeByNode;
  Map<const Node *, Declaration *> m_declByNode;
  Map<const Node *, Reference *> m_refByNode;
  Vector<Reference *> m_unresolved;
};

/** Rebuilds `out` from `file`'s tree; any previous contents are dropped. */
void bind(AstFile &file, Bindings &out);

/** Writes every scope with its declarations and references, nested by indent. */
void dumpBindings(const Bindings &bindings, string &out);

} // namespace fastlint::ast
