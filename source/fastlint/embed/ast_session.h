#pragma once

// One parsed file, kept alive as a unit so an embedding can hand out node
// handles and read them lazily. The N-API addon and the WASM module both wrap
// this: they parse a buffer once, then answer the Host accessor calls the
// generated TypeScript runtime makes (plugin/generated/ts/views.ts).

#include "fastlint/ast/binder.h"
#include "fastlint/ast/file.h"
#include "fastlint/syntax/parser.h"
#include "fastlint/syntax/tree.h"
#include "util/vector.h"

#include <string>
#include <string_view>

namespace fastlint::embed {

using litestl::util::Vector;

/**
 * Owns the source buffer, the grammar tree lowered from it and the AST file, in
 * that dependency order, so every `ast::Node *` it exposes stays valid until the
 * session is destroyed. Parsing runs the same scanner, parser, lowering and
 * binder passes the linter does; type-aware information is out of scope, as an
 * embedding has no tsgo process.
 */
class AstSession {
public:
  /** Parses `source` as if saved at `filename`; the extension picks the parser
   * options. Never null: a syntax error yields a recovered tree, as the linter's
   * does. */
  static AstSession *parse(std::string_view source, std::string_view filename);

  /** Use `parse`; public only so `alloc::New` can construct it. */
  explicit AstSession(std::string_view source) : m_source(source), m_file(&m_tree)
  {
  }

  const ast::Node *root() const
  {
    return m_file.root();
  }

  /** Every descendant of `node` whose kind is `kind`, in preorder. */
  void descendants(const ast::Node *node,
                   ast::NodeKind kind,
                   Vector<const ast::Node *> &out) const;

private:
  // The embedding owns the source buffer here, at the boundary; the grammar
  // tree and every node span point into it, so it outlives them both.
  std::string m_source;
  syntax::Diagnostics m_diagnostics;
  syntax::GrammarTree m_tree;
  ast::AstFile m_file;
  ast::Bindings m_bindings;
};

} // namespace fastlint::embed
