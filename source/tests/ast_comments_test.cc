#include "fastlint/ast/comments.h"
#include "fastlint/ast/file.h"
#include "fastlint/ast/generated/views.h"
#include "fastlint/ast/lower.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "testing/test.h"

#include <string>

using namespace fastlint;
using namespace fastlint::ast;

namespace {

struct Lowered {
  syntax::Diagnostics diagnostics;
  syntax::GrammarTree tree;
  AstFile file;
  Node *root = nullptr;
  std::string_view source;

  explicit Lowered(std::string_view text) : file(&tree), source(text)
  {
    syntax::Parser parser(text, {}, diagnostics);
    parser.parseFile(tree);
    root = lower(tree, file);
  }

  /** The comments on a node as `place:text` joined by `|`. */
  std::string describe(const Node *n) const
  {
    const CommentList *list = file.comments(n);
    std::string out;
    if (!list) {
      return out;
    }
    for (const Comment &c : *list) {
      if (!out.empty()) {
        out += "|";
      }
      switch (c.place) {
      case CommentPlace::Leading:
        out += "leading:";
        break;
      case CommentPlace::Trailing:
        out += "trailing:";
        break;
      case CommentPlace::Dangling:
        out += "dangling:";
        break;
      }
      out += std::string(source.substr(c.offset, c.length));
    }
    return out;
  }

  Node *statement(int i) const
  {
    return root->children[i];
  }
};

} // namespace

TEST(ast_comments, same_line_comment_trails_the_statement)
{
  Lowered l("foo(); // note\nbar();");
  CHECK_EQ(l.describe(l.statement(0)), "trailing:// note");
  CHECK_EQ(l.describe(l.statement(1)), "");
  CHECK_EQ(l.describe(l.root), "");
}

TEST(ast_comments, own_line_comment_leads_the_next_statement)
{
  Lowered l("foo();\n// about bar\nbar();");
  CHECK_EQ(l.describe(l.statement(0)), "");
  CHECK_EQ(l.describe(l.statement(1)), "leading:// about bar");
}

TEST(ast_comments, comment_before_a_closing_brace_trails_the_last_child)
{
  Lowered l("function f() {\n  foo();\n  // after foo\n}");
  FunctionLike fn = l.statement(0)->as<FunctionLike>();
  Node *body = fn.body();
  CHECK_EQ(l.describe(body->children[0]), "trailing:// after foo");
  CHECK_EQ(l.describe(body), "");
}

TEST(ast_comments, comment_in_an_empty_block_dangles)
{
  Lowered l("function f() {\n  // nothing here\n}");
  FunctionLike fn = l.statement(0)->as<FunctionLike>();
  CHECK_EQ(l.describe(fn.body()), "dangling:// nothing here");
}

TEST(ast_comments, inline_comment_leads_the_argument)
{
  Lowered l("foo(/* first */ a, b /* second */);");
  CallExpression call = l.statement(0)->children[0]->as<CallExpression>();
  CHECK_EQ(l.describe(call.arguments()[0]), "leading:/* first */");
  CHECK_EQ(l.describe(call.arguments()[1]), "trailing:/* second */");
}

TEST(ast_comments, end_of_file_comment_trails_program)
{
  Lowered l("foo();\n// the end\n");
  CHECK_EQ(l.describe(l.root), "trailing:// the end");
  CHECK_EQ(l.describe(l.statement(0)), "");
}

TEST(ast_comments, multi_line_comment_on_the_same_line_trails)
{
  Lowered l("foo(); /* one\ntwo */ bar();");
  CHECK_EQ(l.describe(l.statement(0)), "trailing:/* one\ntwo */");
  CHECK_EQ(l.describe(l.statement(1)), "");
}

TEST(ast_comments, leading_comment_goes_to_the_outermost_node)
{
  Lowered l("// doc\nexport const x = 1;");
  CHECK(l.statement(0)->kind == NodeKind::ExportNamedDeclaration);
  CHECK_EQ(l.describe(l.statement(0)), "leading:// doc");
  CHECK_EQ(l.describe(l.statement(0)->children[0]), "");
}

TEST(ast_comments, move_and_drop)
{
  Lowered l("// a\nfoo();\nbar();");
  Node *foo = l.statement(0);
  Node *bar = l.statement(1);
  l.file.moveComments(foo, bar);
  CHECK_EQ(l.describe(foo), "");
  CHECK_EQ(l.describe(bar), "leading:// a");
  l.file.dropComments(bar);
  CHECK_EQ(l.describe(bar), "");
}
