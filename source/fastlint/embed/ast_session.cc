#include "fastlint/embed/ast_session.h"

#include "fastlint/ast/access.h"
#include "fastlint/ast/lower.h"

#include "util/alloc.h"

namespace fastlint::embed {

namespace {

bool endsWith(std::string_view text, std::string_view suffix)
{
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

syntax::Parser::Options parserOptionsFor(std::string_view filename)
{
  syntax::Parser::Options options;
  if (endsWith(filename, ".js") || endsWith(filename, ".mjs") ||
      endsWith(filename, ".cjs"))
  {
    options.javaScript = true;
  } else if (endsWith(filename, ".jsx")) {
    options.javaScript = true;
    options.jsx = true;
  } else if (endsWith(filename, ".tsx")) {
    options.jsx = true;
  }
  return options;
}

void collect(const ast::Node *node, ast::NodeKind kind, Vector<const ast::Node *> &out)
{
  int count = ast::access::childCount(node);
  for (int i = 0; i < count; i++) {
    const ast::Node *child = ast::access::child(node, i);
    if (!child) {
      continue;
    }
    if (ast::access::kind(child) == kind) {
      out.append(child);
    }
    collect(child, kind, out);
  }
}

} // namespace

AstSession *AstSession::parse(std::string_view source, std::string_view filename)
{
  AstSession *session = litestl::alloc::New<AstSession>("embed AstSession", source);
  syntax::Parser::Options options = parserOptionsFor(filename);
  syntax::Parser parser(session->m_source, options, session->m_diagnostics);
  parser.parseFile(session->m_tree);
  ast::lower(session->m_tree, session->m_file);
  ast::bind(session->m_file, session->m_bindings);
  session->m_file.buildPreorder();
  return session;
}

void AstSession::descendants(const ast::Node *node,
                             ast::NodeKind kind,
                             Vector<const ast::Node *> &out) const
{
  if (node) {
    collect(node, kind, out);
  }
}

} // namespace fastlint::embed
