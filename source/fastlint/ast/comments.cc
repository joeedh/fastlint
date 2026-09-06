#include "fastlint/ast/comments.h"

#include "fastlint/ast/file.h"
#include "fastlint/ast/node.h"

namespace fastlint::ast {

namespace {

using litestl::util::Vector;
using syntax::GrammarTree;
using syntax::Token;
using syntax::Trivia;

class Attacher {
public:
  Attacher(const GrammarTree &tree, AstFile &file)
      : f(file), tokens(tree.tokens()), trivia(tree.trivia())
  {
  }

  void run()
  {
    Node *root = f.root();
    if (!root) {
      return;
    }
    startsAt.resize(tokens.size() + 1);
    endsAt.resize(tokens.size() + 1);
    for (size_t i = 0; i < startsAt.size(); i++) {
      startsAt[int(i)] = nullptr;
      endsAt[int(i)] = nullptr;
    }
    for (Node *c : root->children) {
      if (c) {
        index(c);
      }
    }
    for (size_t i = 0; i < tokens.size(); i++) {
      const Token &token = tokens[i];
      bool sawNewline = false;
      for (uint32_t r = 0; r < token.leadingTriviaCount; r++) {
        const Trivia &run = trivia[token.leadingTriviaStart + r];
        switch (run.kind) {
        case Trivia::Kind::NewLine:
          sawNewline = true;
          break;
        case Trivia::Kind::SingleLineComment:
        case Trivia::Kind::MultiLineComment:
          attach(run, uint32_t(i), sawNewline);
          if (run.lineBreak) {
            sawNewline = true;
          }
          break;
        default:
          break;
        }
      }
    }
  }

private:
  AstFile &f;
  span<const Token> tokens;
  span<const Trivia> trivia;
  /** Outermost node (below Program) starting or ending at each token. */
  Vector<Node *> startsAt;
  Vector<Node *> endsAt;

  /** Index of the token at `offset`, or kNoToken. */
  uint32_t tokenAt(uint32_t offset, bool byEnd) const
  {
    size_t lo = 0;
    size_t hi = tokens.size();
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      uint32_t key = byEnd ? tokens[mid].offset + tokens[mid].length : tokens[mid].offset;
      if (key < offset) {
        lo = mid + 1;
      } else if (key > offset) {
        hi = mid;
      } else {
        return uint32_t(mid);
      }
    }
    return kNoToken;
  }

  /** Records the outermost node at each start and end token, preorder. */
  void index(Node *n)
  {
    if (n->end > n->start) {
      uint32_t first = tokenAt(n->start, false);
      uint32_t last = tokenAt(n->end, true);
      if (first != kNoToken && !startsAt[int(first)]) {
        startsAt[int(first)] = n;
      }
      if (last != kNoToken && !endsAt[int(last)]) {
        endsAt[int(last)] = n;
      }
    }
    for (Node *c : n->children) {
      if (c) {
        index(c);
      }
    }
  }

  /** The deepest node whose span contains `offset`. */
  Node *innermost(uint32_t offset) const
  {
    Node *n = f.root();
    for (;;) {
      Node *next = nullptr;
      for (Node *c : n->children) {
        if (c && c->start <= offset && offset < c->end && c->end > c->start) {
          next = c;
          break;
        }
      }
      if (!next) {
        return n;
      }
      n = next;
    }
  }

  void attach(const Trivia &run, uint32_t nextToken, bool sawNewline)
  {
    bool eof = nextToken + 1 == tokens.size();
    // The node before the comment; a list separator between them does not count.
    Node *prev = nullptr;
    for (uint32_t j = nextToken; j > 0;) {
      j--;
      prev = endsAt[int(j)];
      if (prev || tokens[j].kind != syntax::TokenKind::CommaToken) {
        break;
      }
    }
    Node *next = startsAt[int(nextToken)];
    Node *target = nullptr;
    CommentPlace place = CommentPlace::Leading;
    if (prev && !sawNewline) {
      target = prev;
      place = CommentPlace::Trailing;
    } else if (eof) {
      target = f.root();
      place = CommentPlace::Trailing;
    } else if (next) {
      target = next;
      place = CommentPlace::Leading;
    } else if (prev) {
      target = prev;
      place = CommentPlace::Trailing;
    } else {
      target = innermost(run.offset);
      place = CommentPlace::Dangling;
    }
    Comment comment{
        run.offset, run.length, run.kind == Trivia::Kind::MultiLineComment, place};
    f.commentsFor(target).append(comment);
  }
};

} // namespace

void attachComments(const syntax::GrammarTree &tree, AstFile &file)
{
  Attacher attacher(tree, file);
  attacher.run();
}

} // namespace fastlint::ast
