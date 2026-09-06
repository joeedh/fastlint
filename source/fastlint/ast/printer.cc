#include "fastlint/ast/printer.h"

#include "fastlint/ast/generated/views.h"
#include "fastlint/ast/kind_info.h"
#include "fastlint/syntax/tokens.h"

namespace fastlint::ast {

using syntax::GrammarTree;

// ------------------------------------------------------------------ style

Style sniffStyle(const GrammarTree &tree)
{
  Style style;
  size_t semicolons = 0;
  size_t singleQuotes = 0;
  size_t doubleQuotes = 0;
  string_view source = tree.source();
  for (const syntax::Token &token : tree.tokens()) {
    if (token.kind == syntax::TokenKind::SemicolonToken) {
      semicolons++;
    } else if (token.kind == syntax::TokenKind::StringLiteral && token.length > 0) {
      if (source[token.offset] == '\'') {
        singleQuotes++;
      } else {
        doubleQuotes++;
      }
    }
  }
  style.semicolons = semicolons > 0 || tree.tokens().size() < 3;
  style.quote = singleQuotes > doubleQuotes ? '\'' : '"';
  for (uint32_t start : tree.lineStarts()) {
    if (start >= source.size()) {
      break;
    }
    if (source[start] == '\t') {
      style.indent = "\t";
      break;
    }
    if (source[start] == ' ') {
      size_t width = 0;
      while (start + width < source.size() && source[start + width] == ' ') {
        width++;
      }
      if (width == 4) {
        style.indent = "    ";
      }
      break;
    }
  }
  if (source.find("\r\n") != string_view::npos) {
    style.newline = "\r\n";
  }
  return style;
}

namespace {

// -------------------------------------------------------------- operators

const char *unaryText(UnaryOperator op)
{
  switch (op) {
  case UnaryOperator::Minus:
    return "-";
  case UnaryOperator::Plus:
    return "+";
  case UnaryOperator::Not:
    return "!";
  case UnaryOperator::BitwiseNot:
    return "~";
  case UnaryOperator::Typeof:
    return "typeof";
  case UnaryOperator::Void:
    return "void";
  case UnaryOperator::Delete:
    return "delete";
  }
  return "";
}

const char *binaryText(BinaryOperator op)
{
  switch (op) {
  case BinaryOperator::Equal:
    return "==";
  case BinaryOperator::NotEqual:
    return "!=";
  case BinaryOperator::StrictEqual:
    return "===";
  case BinaryOperator::StrictNotEqual:
    return "!==";
  case BinaryOperator::Less:
    return "<";
  case BinaryOperator::LessEqual:
    return "<=";
  case BinaryOperator::Greater:
    return ">";
  case BinaryOperator::GreaterEqual:
    return ">=";
  case BinaryOperator::ShiftLeft:
    return "<<";
  case BinaryOperator::ShiftRight:
    return ">>";
  case BinaryOperator::ShiftRightUnsigned:
    return ">>>";
  case BinaryOperator::Add:
    return "+";
  case BinaryOperator::Subtract:
    return "-";
  case BinaryOperator::Multiply:
    return "*";
  case BinaryOperator::Divide:
    return "/";
  case BinaryOperator::Remainder:
    return "%";
  case BinaryOperator::Exponent:
    return "**";
  case BinaryOperator::BitwiseOr:
    return "|";
  case BinaryOperator::BitwiseXor:
    return "^";
  case BinaryOperator::BitwiseAnd:
    return "&";
  case BinaryOperator::In:
    return "in";
  case BinaryOperator::Instanceof:
    return "instanceof";
  }
  return "";
}

const char *logicalText(LogicalOperator op)
{
  switch (op) {
  case LogicalOperator::Or:
    return "||";
  case LogicalOperator::And:
    return "&&";
  case LogicalOperator::Nullish:
    return "??";
  }
  return "";
}

const char *assignmentText(AssignmentOperator op)
{
  switch (op) {
  case AssignmentOperator::Assign:
    return "=";
  case AssignmentOperator::AddAssign:
    return "+=";
  case AssignmentOperator::SubtractAssign:
    return "-=";
  case AssignmentOperator::MultiplyAssign:
    return "*=";
  case AssignmentOperator::DivideAssign:
    return "/=";
  case AssignmentOperator::RemainderAssign:
    return "%=";
  case AssignmentOperator::ExponentAssign:
    return "**=";
  case AssignmentOperator::ShiftLeftAssign:
    return "<<=";
  case AssignmentOperator::ShiftRightAssign:
    return ">>=";
  case AssignmentOperator::ShiftRightUnsignedAssign:
    return ">>>=";
  case AssignmentOperator::BitwiseOrAssign:
    return "|=";
  case AssignmentOperator::BitwiseXorAssign:
    return "^=";
  case AssignmentOperator::BitwiseAndAssign:
    return "&=";
  case AssignmentOperator::OrAssign:
    return "||=";
  case AssignmentOperator::AndAssign:
    return "&&=";
  case AssignmentOperator::NullishAssign:
    return "??=";
  }
  return "";
}

const char *accessibilityText(Accessibility a)
{
  switch (a) {
  case Accessibility::Public:
    return "public ";
  case Accessibility::Private:
    return "private ";
  case Accessibility::Protected:
    return "protected ";
  default:
    return "";
  }
}

const char *variableKindText(VariableKind kind)
{
  switch (kind) {
  case VariableKind::Var:
    return "var";
  case VariableKind::Let:
    return "let";
  case VariableKind::Const:
    return "const";
  case VariableKind::Using:
    return "using";
  case VariableKind::AwaitUsing:
    return "await using";
  }
  return "let";
}

bool isHorizontalSpace(char c)
{
  return c == ' ' || c == '\t';
}

size_t countCommas(string_view text)
{
  size_t count = 0;
  for (char c : text) {
    if (c == ',') {
      count++;
    }
  }
  return count;
}

// ---------------------------------------------------------------- printer

class Printer {
public:
  Printer(AstFile &file, string &out, PrintOptions options)
      : f(file), out(out), updateSpans(options.updateSpans)
  {
    if (f.grammar()) {
      style = sniffStyle(*f.grammar());
    }
    for (const DeadRange &d : f.deadRanges()) {
      dead.append(d);
    }
    dead.sort([](const DeadRange &a, const DeadRange &b) {
      return a.offset < b.offset ? -1 : a.offset > b.offset ? 1 : 0;
    });
  }

  void run()
  {
    Node *root = f.root();
    if (!root) {
      return;
    }
    // The file's leading trivia precedes the first token, where Program starts.
    if (root->grammar.tree == f.grammar() && f.grammar()) {
      slice(f.grammar(), 0, root->start);
    }
    print(root);
    flushNewline();
  }

private:
  AstFile &f;
  string &out;
  bool updateSpans;
  Style style;
  Vector<DeadRange> dead;
  /** Set after a moved `//` comment; the next text must start a new line. */
  bool pendingNewline = false;

  // ------------------------------------------------------------- output

  void put(string_view text)
  {
    if (text.empty()) {
      return;
    }
    if (pendingNewline) {
      pendingNewline = false;
      if (text[0] != '\n' && text[0] != '\r') {
        string indent = lineIndent();
        put(style.newline);
        out += indent;
      }
    }
    for (char c : text) {
      out += c;
    }
  }

  void put(char c)
  {
    put(string_view(&c, 1));
  }

  void flushNewline()
  {
    if (pendingNewline) {
      pendingNewline = false;
      put(style.newline);
    }
  }

  /** The whitespace that opens the current output line. */
  string lineIndent() const
  {
    int end = int(out.size());
    int lineStart = end;
    while (lineStart > 0 && out[lineStart - 1] != '\n') {
      lineStart--;
    }
    string indent;
    for (int i = lineStart; i < end && isHorizontalSpace(out[i]); i++) {
      indent += out[i];
    }
    return indent;
  }

  /** Copies `tree`'s source in [from, to), skipping dead ranges of the file's tree. */
  void slice(const GrammarTree *tree, uint32_t from, uint32_t to)
  {
    if (!tree || to <= from) {
      return;
    }
    string_view source = tree->source();
    if (to > source.size()) {
      to = uint32_t(source.size());
    }
    if (tree != f.grammar() || dead.isEmpty()) {
      put(source.substr(from, to - from));
      return;
    }
    uint32_t at = from;
    for (const DeadRange &d : dead) {
      uint32_t dEnd = d.offset + d.length;
      if (dEnd <= at) {
        continue;
      }
      if (d.offset >= to) {
        break;
      }
      uint32_t cut = d.offset > at ? d.offset : at;
      // The spaces that separated the comment from the code go with it.
      while (cut > at && isHorizontalSpace(source[cut - 1])) {
        cut--;
      }
      put(source.substr(at, cut - at));
      at = dEnd > to ? to : dEnd;
      // A comment on its own line takes its line break with it.
      if (cut == 0 || source[cut - 1] == '\n') {
        if (at < to && source[at] == '\r') {
          at++;
        }
        if (at < to && source[at] == '\n') {
          at++;
        }
      }
    }
    if (at < to) {
      put(source.substr(at, to - at));
    }
  }

  // ------------------------------------------------------------ comments

  void movedComments(const Node *n, CommentPlace place)
  {
    const CommentList *list = f.comments(n);
    if (!list) {
      return;
    }
    string_view source = f.grammar() ? f.grammar()->source() : string_view();
    for (const Comment &c : *list) {
      if (!c.moved || c.place != place || c.offset + c.length > source.size()) {
        continue;
      }
      string_view text = source.substr(c.offset, c.length);
      switch (place) {
      case CommentPlace::Leading: {
        string indent = lineIndent();
        put(text);
        if (c.multiLine) {
          put(' ');
        } else {
          put(style.newline);
          put(string_view(indent.c_str(), indent.size()));
        }
        break;
      }
      case CommentPlace::Trailing:
      case CommentPlace::Dangling:
        put(' ');
        put(text);
        if (!c.multiLine) {
          pendingNewline = true;
        }
        break;
      }
    }
  }

  // --------------------------------------------------------------- spans

  void shiftSubtree(Node *n, int64_t delta)
  {
    n->start = uint32_t(int64_t(n->start) + delta);
    n->end = uint32_t(int64_t(n->end) + delta);
    for (Node *c : n->children) {
      if (c) {
        shiftSubtree(c, delta);
      }
    }
  }

  // ---------------------------------------------------------------- print

  void print(Node *n)
  {
    if (!n) {
      return;
    }
    movedComments(n, CommentPlace::Leading);
    uint32_t begin = uint32_t(out.size());
    bool parens = n->hasFlag(Flag::Parenthesized);
    const GrammarTree *tree = n->grammar.tree;
    if (!n->dirty && tree && n->end > n->start) {
      string_view source = tree->source();
      bool hasParens = n->start < source.size() && source[n->start] == '(';
      if (parens && !hasParens) {
        put('(');
      }
      uint32_t at = uint32_t(out.size());
      slice(tree, n->start, n->end);
      if (updateSpans) {
        shiftSubtree(n, int64_t(at) - int64_t(n->start));
      }
      if (parens && !hasParens) {
        put(')');
      }
    } else {
      const Layout *layout = n->dirty && tree ? f.layout(n) : nullptr;
      bool own = layout && layout->usable && layoutCovers(n, *layout);
      // A captured layout carries the node's own parentheses.
      if (parens && !own) {
        put('(');
      }
      if (own) {
        printLayout(n, *layout);
      } else {
        printTemplate(n);
      }
      if (parens && !own) {
        put(')');
      }
      if (updateSpans) {
        n->start = begin;
        n->end = uint32_t(out.size());
      }
    }
    movedComments(n, CommentPlace::Trailing);
  }

  // --------------------------------------------------------------- layout

  /** The current child in a fixed slot, or null. */
  static Node *slotChild(const Node *n, int slot)
  {
    return slot >= 0 && size_t(slot) < n->children.size() ? n->children[slot] : nullptr;
  }

  /**
   * Whether every filled fixed slot has a place in the layout. A slot that
   * was empty at capture has no glue text around it, so the kind template
   * prints the node instead.
   */
  static bool layoutCovers(const Node *n, const Layout &layout)
  {
    int fixed = kindInfo(n->kind).fixedChildren;
    if (n->children.size() > size_t(fixed)) {
      // A list that was empty at capture has no place for its elements.
      bool anyList = false;
      for (const LayoutItem &item : layout.items) {
        if (item.child && item.slot >= fixed) {
          anyList = true;
          break;
        }
      }
      if (!anyList) {
        return false;
      }
    }
    for (int slot = 0; slot < fixed && size_t(slot) < n->children.size(); slot++) {
      if (!n->children[slot]) {
        continue;
      }
      bool found = false;
      for (const LayoutItem &item : layout.items) {
        if (item.child && item.slot == slot) {
          found = true;
          break;
        }
      }
      if (!found) {
        return false;
      }
    }
    return true;
  }

  void printLayout(Node *n, const Layout &layout)
  {
    const KindInfo &info = kindInfo(n->kind);
    int fixed = info.fixedChildren;
    const GrammarTree *tree = n->grammar.tree;
    span<const LayoutItem> items(const_cast<Vector<LayoutItem, 4> &>(layout.items).data(),
                                 layout.items.size());
    bool dangling = false;
    size_t i = 0;
    while (i < items.size()) {
      const LayoutItem &item = items[i];
      if (!item.child) {
        // Own text. Whitespace before a slot that was cleared goes with it,
        // as does trailing indentation before a list that emptied.
        bool trim = false;
        if (i + 1 < items.size() && items[i + 1].child) {
          const LayoutItem &next = items[i + 1];
          trim = next.slot < fixed ? !slotChild(n, next.slot)
                                   : n->children.size() <= size_t(fixed);
        }
        uint32_t to = item.to;
        if (trim) {
          string_view source = tree->source();
          while (to > item.from && isHorizontalSpace(source[to - 1])) {
            to--;
          }
        }
        slice(tree, item.from, to);
        if (!dangling) {
          dangling = true;
          movedComments(n, CommentPlace::Dangling);
        }
        i++;
        continue;
      }
      if (item.slot < fixed) {
        print(slotChild(n, item.slot));
        i++;
        continue;
      }
      // The list region: every consecutive list child with the gaps between.
      size_t regionEnd = i;
      while (regionEnd < items.size() &&
             (items[regionEnd].child
                  ? items[regionEnd].slot >= fixed
                  : regionEnd + 1 < items.size() && items[regionEnd + 1].child &&
                        items[regionEnd + 1].slot >= fixed))
      {
        regionEnd++;
      }
      printListRegion(n, items.subspan(i, regionEnd - i), fixed);
      i = regionEnd;
    }
    if (!dangling) {
      movedComments(n, CommentPlace::Dangling);
    }
  }

  /** Prints the current list elements using the captured gaps as separators. */
  void printListRegion(Node *n, span<const LayoutItem> region, int fixed)
  {
    const GrammarTree *tree = n->grammar.tree;
    Vector<Node *, 8> kept;
    Vector<LayoutItem, 8> gaps;
    for (size_t i = 0; i < region.size(); i++) {
      if (region[i].child) {
        kept.append(region[i].child);
        // The gap after this element, empty when the next element abuts it.
        if (i + 1 < region.size() && !region[i + 1].child) {
          gaps.append(region[i + 1]);
        } else if (i + 1 < region.size()) {
          gaps.append({region[i].to, region[i].to, nullptr, -1});
        }
      }
    }
    auto keptIndex = [&](const Node *c) {
      for (size_t k = 0; k < kept.size(); k++) {
        if (kept[int(k)] == c) {
          return int(k);
        }
      }
      return -1;
    };
    auto gapAfter = [&](int k) -> const LayoutItem * {
      return k >= 0 && size_t(k) < gaps.size() ? &gaps[k] : nullptr;
    };
    auto emitGap = [&](const LayoutItem *gap) {
      if (gap) {
        slice(tree, gap->from, gap->to);
      } else {
        defaultSeparator(n);
      }
    };
    bool printedAny = false;
    int prevKept = -1;
    // Holes are not layout items; the gap between their neighbours carries
    // their commas, and extra holes get an extra comma each.
    size_t holes = 0;
    span<Node *> current = access::tail(n, fixed);
    for (size_t j = 0; j < current.size(); j++) {
      Node *c = current[j];
      if (!c) {
        holes++;
        continue;
      }
      int k = keptIndex(c);
      if (k >= 0) {
        if (printedAny) {
          const LayoutItem *gap = k > 0 ? gapAfter(k - 1) : gapAfter(0);
          if (holes > 0) {
            size_t commas =
                gap ? countCommas(tree->source().substr(gap->from, gap->to - gap->from))
                    : 1;
            for (size_t h = commas; h < holes + 1; h++) {
              put(", ");
            }
          }
          emitGap(gap);
        } else {
          for (size_t h = 0; h < holes; h++) {
            put(", ");
          }
        }
        holes = 0;
        prevKept = k;
      } else if (printedAny) {
        for (size_t h = 0; h < holes; h++) {
          put(", ");
        }
        holes = 0;
        const LayoutItem *gap = gapAfter(prevKept);
        if (!gap && prevKept > 0) {
          gap = gapAfter(prevKept - 1);
        }
        if (!gap) {
          // Nothing kept before it: borrow the gap before the next kept element.
          for (size_t m = j + 1; m < current.size() && !gap; m++) {
            int nk = current[m] ? keptIndex(current[m]) : -1;
            if (nk > 0) {
              gap = gapAfter(nk - 1);
            }
          }
        }
        emitGap(gap);
      }
      print(c);
      printedAny = true;
    }
  }

  /** The separator between list elements when no captured gap applies. */
  void defaultSeparator(const Node *list)
  {
    switch (list->kind) {
    case NodeKind::Program:
    case NodeKind::BlockStatement:
    case NodeKind::ClassBody:
    case NodeKind::StaticBlock:
    case NodeKind::SwitchCase:
    case NodeKind::TSModuleBlock:
    case NodeKind::TSInterfaceBody: {
      string indent = lineIndent();
      put(style.newline);
      put(string_view(indent.c_str(), indent.size()));
      break;
    }
    case NodeKind::SwitchStatement:
    case NodeKind::TSTypeLiteral:
      put(' ');
      break;
    case NodeKind::TemplateLiteral:
    case NodeKind::TSTemplateLiteralType:
    case NodeKind::Decorators:
    case NodeKind::TSUnionType:
    case NodeKind::TSIntersectionType:
      break;
    default:
      put(", ");
      break;
    }
  }

  // ------------------------------------------------------------ templates

  void semi()
  {
    if (style.semicolons) {
      put(';');
    }
  }

  void list(span<Node *> nodes, string_view separator)
  {
    for (size_t i = 0; i < nodes.size(); i++) {
      if (i > 0) {
        put(separator);
      }
      print(nodes[i]);
    }
  }

  /** Statements on their own lines, one indent level in, inside braces. */
  void bracedStatements(span<Node *> nodes)
  {
    if (nodes.size() == 0) {
      put("{}");
      return;
    }
    string indent = lineIndent();
    put('{');
    for (Node *s : nodes) {
      put(style.newline);
      put(string_view(indent.c_str(), indent.size()));
      put(style.indent);
      print(s);
    }
    put(style.newline);
    put(string_view(indent.c_str(), indent.size()));
    put('}');
  }

  void optional(string_view prefix, Node *child, string_view suffix = {})
  {
    if (child) {
      put(prefix);
      print(child);
      put(suffix);
    }
  }

  void key(Node *k, bool computed)
  {
    if (computed) {
      put('[');
      print(k);
      put(']');
    } else {
      print(k);
    }
  }

  /** Everything after the name of a function: type parameters, parameters, return type,
   * body. */
  void functionTail(Node *fn)
  {
    FunctionLike view(fn);
    print(view.typeParameters());
    put('(');
    list(view.params(), ", ");
    put(')');
    optional(": ", view.returnType());
    if (view.body()) {
      put(' ');
      print(view.body());
    } else {
      semi();
    }
  }

  void functionHead(Node *fn)
  {
    FunctionLike view(fn);
    if (view.isAsync()) {
      put("async ");
    }
    put("function");
    if (view.isGenerator()) {
      put('*');
    }
    optional(" ", view.id());
  }

  void classLike(Node *n)
  {
    ClassLike view(n);
    print(view.decorators());
    if (n->hasFlag(Flag::Declare)) {
      put("declare ");
    }
    if (view.isAbstract()) {
      put("abstract ");
    }
    put("class");
    optional(" ", view.id());
    print(view.typeParameters());
    optional(" extends ", view.superClass());
    print(view.superTypeArguments());
    if (view.implements().size() > 0) {
      put(" implements ");
      list(view.implements(), ", ");
    }
    put(' ');
    print(view.body());
  }

  void memberModifiers(Node *n)
  {
    print(ClassMember(n).decorators());
    put(accessibilityText(Accessibility(n->dataByte(0))));
    if (n->hasFlag(Flag::Declare)) {
      put("declare ");
    }
    if (n->hasFlag(Flag::Static)) {
      put("static ");
    }
    if (n->hasFlag(Flag::Abstract) || n->kind == NodeKind::TSAbstractMethodDefinition ||
        n->kind == NodeKind::TSAbstractPropertyDefinition ||
        n->kind == NodeKind::TSAbstractAccessorProperty)
    {
      put("abstract ");
    }
    if (n->hasFlag(Flag::Override)) {
      put("override ");
    }
    if (n->hasFlag(Flag::Readonly)) {
      put("readonly ");
    }
  }

  void methodDefinition(Node *n)
  {
    MethodDefinition view(n);
    memberModifiers(n);
    Node *value = view.value();
    FunctionLike fn = value ? value->as<FunctionLike>() : FunctionLike();
    if (fn && fn.isAsync()) {
      put("async ");
    }
    if (view.kind() == MethodKind::Get) {
      put("get ");
    } else if (view.kind() == MethodKind::Set) {
      put("set ");
    }
    if (fn && fn.isGenerator()) {
      put('*');
    }
    key(view.key(), view.isComputed());
    if (view.isOptional()) {
      put('?');
    }
    if (value) {
      functionTail(value);
    } else {
      semi();
    }
  }

  void propertyDefinition(Node *n, bool accessor)
  {
    PropertyDefinition view(n);
    memberModifiers(n);
    if (accessor) {
      put("accessor ");
    }
    key(view.key(), view.isComputed());
    if (view.isOptional()) {
      put('?');
    } else if (view.isDefinite()) {
      put('!');
    }
    optional(": ", view.typeAnnotation());
    optional(" = ", view.value());
    semi();
  }

  void variableDeclaration(Node *n, bool terminator)
  {
    VariableDeclaration view(n);
    if (view.isDeclare()) {
      put("declare ");
    }
    put(variableKindText(view.kind()));
    put(' ');
    list(view.declarations(), ", ");
    if (terminator) {
      semi();
    }
  }

  void loopHead(Node *head)
  {
    if (head && head->kind == NodeKind::VariableDeclaration) {
      variableDeclaration(head, false);
    } else {
      print(head);
    }
  }

  void templateParts(span<Node *> parts)
  {
    put('`');
    for (Node *part : parts) {
      if (part->kind == NodeKind::TemplateElement) {
        put(part->text);
      } else {
        put("${");
        print(part);
        put('}');
      }
    }
    put('`');
  }

  void literal(Node *n)
  {
    string_view text = n->text;
    if (LiteralKind(n->dataByte(0)) == LiteralKind::String && text.size() >= 2 &&
        text.front() != style.quote && (text.front() == '"' || text.front() == '\''))
    {
      string_view inner = text.substr(1, text.size() - 2);
      bool safe = true;
      for (char c : inner) {
        if (c == '\\' || c == style.quote) {
          safe = false;
          break;
        }
      }
      if (safe) {
        put(style.quote);
        put(inner);
        put(style.quote);
        return;
      }
    }
    put(text);
  }

  void mappedType(Node *n)
  {
    TSMappedType view(n);
    put("{ ");
    switch (view.readonlyModifier()) {
    case Modifier::Plus:
      put("+readonly ");
      break;
    case Modifier::Minus:
      put("-readonly ");
      break;
    default:
      break;
    }
    put('[');
    Node *param = view.typeParameter();
    if (param) {
      put(param->text);
      optional(" in ", TSTypeParameter(param).constraint());
    }
    optional(" as ", view.nameType());
    put(']');
    switch (view.optionalModifier()) {
    case Modifier::Plus:
      put("+?");
      break;
    case Modifier::Minus:
      put("-?");
      break;
    default:
      break;
    }
    optional(": ", view.typeAnnotation());
    put(" }");
  }

  void signature(Node *n, bool withNew)
  {
    SignatureLike view(n);
    if (withNew) {
      put("new ");
    }
    print(view.typeParameters());
    put('(');
    list(view.params(), ", ");
    put(')');
  }

  void printTemplate(Node *n)
  {
    switch (n->kind) {
    case NodeKind::Program:
      list(Program(n).body(), style.newline);
      break;
    case NodeKind::Identifier:
      put(n->text);
      if (n->hasFlag(Flag::Optional)) {
        put('?');
      }
      optional(": ", Identifier(n).typeAnnotation());
      break;
    case NodeKind::PrivateIdentifier:
      if (n->text.empty() || n->text.front() != '#') {
        put('#');
      }
      put(n->text);
      break;
    case NodeKind::Literal:
      literal(n);
      break;
    case NodeKind::TemplateLiteral:
      templateParts(TemplateLiteral(n).parts());
      break;
    case NodeKind::TSTemplateLiteralType:
      templateParts(TSTemplateLiteralType(n).parts());
      break;
    case NodeKind::TemplateElement:
      put(n->text);
      break;
    case NodeKind::TaggedTemplateExpression: {
      TaggedTemplateExpression view(n);
      print(view.tag());
      print(view.typeArguments());
      print(view.quasi());
      break;
    }
    case NodeKind::ThisExpression:
    case NodeKind::TSThisType:
      put("this");
      break;
    case NodeKind::Super:
      put("super");
      break;

    // --------------------------------------------------------- expressions
    case NodeKind::ArrayExpression:
      put('[');
      list(ArrayExpression(n).elements(), ", ");
      put(']');
      break;
    case NodeKind::ObjectExpression: {
      span<Node *> props = ObjectExpression(n).properties();
      if (props.size() == 0) {
        put("{}");
      } else {
        put("{ ");
        list(props, ", ");
        put(" }");
      }
      break;
    }
    case NodeKind::Property: {
      Property view(n);
      Node *value = view.value();
      if (view.kind() == PropertyKind::Get || view.kind() == PropertyKind::Set ||
          view.isMethod())
      {
        FunctionLike fn = value ? value->as<FunctionLike>() : FunctionLike();
        if (fn && fn.isAsync()) {
          put("async ");
        }
        if (view.kind() == PropertyKind::Get) {
          put("get ");
        } else if (view.kind() == PropertyKind::Set) {
          put("set ");
        }
        if (fn && fn.isGenerator()) {
          put('*');
        }
        key(view.key(), view.isComputed());
        if (value) {
          functionTail(value);
        }
      } else if (view.isShorthand()) {
        print(value);
      } else {
        key(view.key(), view.isComputed());
        put(": ");
        print(value);
      }
      break;
    }
    case NodeKind::SpreadElement:
    case NodeKind::RestElement:
    case NodeKind::TSRestType:
      put("...");
      print(n->children[0]);
      if (n->kind == NodeKind::RestElement) {
        optional(": ", RestElement(n).typeAnnotation());
      }
      break;
    case NodeKind::MemberExpression: {
      MemberExpression view(n);
      print(view.object());
      if (view.isOptional()) {
        put("?.");
      } else if (!view.isComputed()) {
        put('.');
      }
      key(view.property(), view.isComputed());
      break;
    }
    case NodeKind::CallExpression: {
      CallExpression view(n);
      print(view.callee());
      if (view.isOptional()) {
        put("?.");
      }
      print(view.typeArguments());
      put('(');
      list(view.arguments(), ", ");
      put(')');
      break;
    }
    case NodeKind::NewExpression: {
      NewExpression view(n);
      put("new ");
      print(view.callee());
      print(view.typeArguments());
      put('(');
      list(view.arguments(), ", ");
      put(')');
      break;
    }
    case NodeKind::ImportExpression: {
      ImportExpression view(n);
      put("import(");
      print(view.source());
      optional(", ", view.options());
      put(')');
      break;
    }
    case NodeKind::MetaProperty:
      print(n->children[0]);
      put('.');
      print(n->children[1]);
      break;
    case NodeKind::UnaryExpression: {
      UnaryExpression view(n);
      const char *op = unaryText(view.op());
      put(op);
      if (op[0] >= 'a' && op[0] <= 'z') {
        put(' ');
      }
      print(view.argument());
      break;
    }
    case NodeKind::UpdateExpression: {
      UpdateExpression view(n);
      const char *op = view.op() == UpdateOperator::Increment ? "++" : "--";
      if (view.isPrefix()) {
        put(op);
        print(view.argument());
      } else {
        print(view.argument());
        put(op);
      }
      break;
    }
    case NodeKind::BinaryExpression: {
      BinaryExpression view(n);
      print(view.left());
      put(' ');
      put(binaryText(view.op()));
      put(' ');
      print(view.right());
      break;
    }
    case NodeKind::LogicalExpression: {
      LogicalExpression view(n);
      print(view.left());
      put(' ');
      put(logicalText(view.op()));
      put(' ');
      print(view.right());
      break;
    }
    case NodeKind::AssignmentExpression: {
      AssignmentExpression view(n);
      print(view.left());
      put(' ');
      put(assignmentText(view.op()));
      put(' ');
      print(view.right());
      break;
    }
    case NodeKind::AssignmentPattern:
      print(n->children[0]);
      put(" = ");
      print(n->children[1]);
      break;
    case NodeKind::ConditionalExpression: {
      ConditionalExpression view(n);
      print(view.test());
      put(" ? ");
      print(view.consequent());
      put(" : ");
      print(view.alternate());
      break;
    }
    case NodeKind::SequenceExpression:
      list(SequenceExpression(n).expressions(), ", ");
      break;
    case NodeKind::AwaitExpression:
      put("await ");
      print(n->children[0]);
      break;
    case NodeKind::YieldExpression: {
      YieldExpression view(n);
      put("yield");
      if (view.isDelegate()) {
        put('*');
      }
      optional(" ", view.argument());
      break;
    }
    case NodeKind::ArrowFunctionExpression: {
      FunctionLike view(n);
      if (view.isAsync()) {
        put("async ");
      }
      print(view.typeParameters());
      put('(');
      list(view.params(), ", ");
      put(')');
      optional(": ", view.returnType());
      put(" => ");
      print(view.body());
      break;
    }
    case NodeKind::FunctionExpression:
    case NodeKind::FunctionDeclaration:
    case NodeKind::TSDeclareFunction:
    case NodeKind::TSEmptyBodyFunctionExpression:
      if (n->hasFlag(Flag::Declare)) {
        put("declare ");
      }
      functionHead(n);
      functionTail(n);
      break;
    case NodeKind::ClassDeclaration:
    case NodeKind::ClassExpression:
      classLike(n);
      break;
    case NodeKind::ClassBody:
      bracedStatements(ClassBody(n).body());
      break;
    case NodeKind::Decorators:
      for (Node *d : Decorators(n).decorators()) {
        print(d);
        put(' ');
      }
      break;
    case NodeKind::Decorator:
      put('@');
      print(n->children[0]);
      break;
    case NodeKind::MethodDefinition:
    case NodeKind::TSAbstractMethodDefinition:
      methodDefinition(n);
      break;
    case NodeKind::PropertyDefinition:
    case NodeKind::TSAbstractPropertyDefinition:
      propertyDefinition(n, false);
      break;
    case NodeKind::AccessorProperty:
    case NodeKind::TSAbstractAccessorProperty:
      propertyDefinition(n, true);
      break;
    case NodeKind::StaticBlock:
      put("static ");
      bracedStatements(StaticBlock(n).body());
      break;
    case NodeKind::TSParameterProperty: {
      TSParameterProperty view(n);
      print(view.decorators());
      put(accessibilityText(view.accessibility()));
      if (view.isOverride()) {
        put("override ");
      }
      if (view.isReadonly()) {
        put("readonly ");
      }
      print(view.parameter());
      break;
    }

    // ------------------------------------------------------------ bindings
    case NodeKind::VariableDeclaration:
      variableDeclaration(n, true);
      break;
    case NodeKind::VariableDeclarator: {
      VariableDeclarator view(n);
      print(view.id());
      if (view.isDefinite()) {
        put('!');
      }
      optional(" = ", view.init());
      break;
    }
    case NodeKind::ObjectPattern: {
      ObjectPattern view(n);
      if (view.properties().size() == 0) {
        put("{}");
      } else {
        put("{ ");
        list(view.properties(), ", ");
        put(" }");
      }
      optional(": ", view.typeAnnotation());
      break;
    }
    case NodeKind::ArrayPattern: {
      ArrayPattern view(n);
      put('[');
      list(view.elements(), ", ");
      put(']');
      optional(": ", view.typeAnnotation());
      break;
    }

    // ---------------------------------------------------------- statements
    case NodeKind::ExpressionStatement:
      print(n->children[0]);
      semi();
      break;
    case NodeKind::BlockStatement:
      bracedStatements(BlockStatement(n).body());
      break;
    case NodeKind::EmptyStatement:
      put(';');
      break;
    case NodeKind::DebuggerStatement:
      put("debugger");
      semi();
      break;
    case NodeKind::IfStatement: {
      IfStatement view(n);
      put("if (");
      print(view.test());
      put(") ");
      print(view.consequent());
      optional(" else ", view.alternate());
      break;
    }
    case NodeKind::ForStatement: {
      ForStatement view(n);
      put("for (");
      loopHead(view.init());
      put(';');
      optional(" ", view.test());
      put(';');
      optional(" ", view.update());
      put(") ");
      print(view.body());
      break;
    }
    case NodeKind::ForInStatement:
    case NodeKind::ForOfStatement: {
      Loop view(n);
      put("for ");
      if (n->hasFlag(Flag::Await)) {
        put("await ");
      }
      put('(');
      loopHead(n->children[0]);
      put(n->kind == NodeKind::ForInStatement ? " in " : " of ");
      print(n->children[1]);
      put(") ");
      print(view.body());
      break;
    }
    case NodeKind::WhileStatement: {
      WhileStatement view(n);
      put("while (");
      print(view.test());
      put(") ");
      print(view.body());
      break;
    }
    case NodeKind::DoWhileStatement: {
      DoWhileStatement view(n);
      put("do ");
      print(view.body());
      put(" while (");
      print(view.test());
      put(')');
      semi();
      break;
    }
    case NodeKind::ReturnStatement:
      put("return");
      optional(" ", n->children[0]);
      semi();
      break;
    case NodeKind::ThrowStatement:
      put("throw ");
      print(n->children[0]);
      semi();
      break;
    case NodeKind::BreakStatement:
      put("break");
      optional(" ", n->children[0]);
      semi();
      break;
    case NodeKind::ContinueStatement:
      put("continue");
      optional(" ", n->children[0]);
      semi();
      break;
    case NodeKind::LabeledStatement:
      print(n->children[0]);
      put(": ");
      print(n->children[1]);
      break;
    case NodeKind::SwitchStatement: {
      SwitchStatement view(n);
      put("switch (");
      print(view.discriminant());
      put(") ");
      bracedStatements(view.cases());
      break;
    }
    case NodeKind::SwitchCase: {
      SwitchCase view(n);
      if (view.test()) {
        put("case ");
        print(view.test());
      } else {
        put("default");
      }
      put(':');
      string indent = lineIndent();
      for (Node *s : view.consequent()) {
        put(style.newline);
        put(string_view(indent.c_str(), indent.size()));
        put(style.indent);
        print(s);
      }
      break;
    }
    case NodeKind::TryStatement: {
      TryStatement view(n);
      put("try ");
      print(view.block());
      optional(" ", view.handler());
      optional(" finally ", view.finalizer());
      break;
    }
    case NodeKind::CatchClause: {
      CatchClause view(n);
      put("catch");
      optional(" (", view.param(), ")");
      put(' ');
      print(view.body());
      break;
    }
    case NodeKind::WithStatement:
      put("with (");
      print(n->children[0]);
      put(") ");
      print(n->children[1]);
      break;

    // ------------------------------------------------------------- modules
    case NodeKind::ImportDeclaration: {
      ImportDeclaration view(n);
      put("import ");
      if (view.importKind() == ImportKind::Type) {
        put("type ");
      }
      span<Node *> specs = view.specifiers();
      if (specs.size() > 0) {
        bool braces = false;
        bool first = true;
        for (Node *s : specs) {
          bool named = s->kind == NodeKind::ImportSpecifier;
          if (named && !braces) {
            put(first ? "{ " : ", { ");
            braces = true;
          } else if (!first) {
            put(", ");
          }
          print(s);
          first = false;
        }
        if (braces) {
          put(" }");
        }
        put(" from ");
      }
      print(view.source());
      print(view.attributes());
      semi();
      break;
    }
    case NodeKind::ImportSpecifier: {
      ImportSpecifier view(n);
      if (view.importKind() == ImportKind::Type) {
        put("type ");
      }
      print(view.imported());
      if (view.local() && view.imported() && view.local()->text != view.imported()->text)
      {
        put(" as ");
        print(view.local());
      }
      break;
    }
    case NodeKind::ImportDefaultSpecifier:
      print(n->children[0]);
      break;
    case NodeKind::ImportNamespaceSpecifier:
      put("* as ");
      print(n->children[0]);
      break;
    case NodeKind::ImportAttributes:
      put(" with { ");
      list(ImportAttributes(n).attributes(), ", ");
      put(" }");
      break;
    case NodeKind::ImportAttribute:
      print(n->children[0]);
      put(": ");
      print(n->children[1]);
      break;
    case NodeKind::ExportNamedDeclaration: {
      ExportNamedDeclaration view(n);
      put("export ");
      if (view.exportKind() == ImportKind::Type) {
        put("type ");
      }
      if (view.declaration()) {
        print(view.declaration());
        break;
      }
      put("{ ");
      list(view.specifiers(), ", ");
      put(" }");
      optional(" from ", view.source());
      print(view.attributes());
      semi();
      break;
    }
    case NodeKind::ExportSpecifier: {
      ExportSpecifier view(n);
      if (view.exportKind() == ImportKind::Type) {
        put("type ");
      }
      print(view.local());
      if (view.exported() && view.local() && view.exported()->text != view.local()->text)
      {
        put(" as ");
        print(view.exported());
      }
      break;
    }
    case NodeKind::ExportDefaultDeclaration: {
      Node *decl = n->children[0];
      put("export default ");
      print(decl);
      if (decl && decl->isExpression() && decl->kind != NodeKind::FunctionExpression &&
          decl->kind != NodeKind::ClassExpression)
      {
        semi();
      }
      break;
    }
    case NodeKind::ExportAllDeclaration: {
      ExportAllDeclaration view(n);
      put("export ");
      if (view.exportKind() == ImportKind::Type) {
        put("type ");
      }
      put('*');
      optional(" as ", view.exported());
      put(" from ");
      print(view.source());
      print(view.attributes());
      semi();
      break;
    }

    // ------------------------------------------------- TypeScript expressions
    case NodeKind::TSAsExpression:
      print(n->children[0]);
      put(" as ");
      print(n->children[1]);
      break;
    case NodeKind::TSSatisfiesExpression:
      print(n->children[0]);
      put(" satisfies ");
      print(n->children[1]);
      break;
    case NodeKind::TSNonNullExpression:
      print(n->children[0]);
      put('!');
      break;
    case NodeKind::TSTypeAssertion:
      put('<');
      print(n->children[0]);
      put('>');
      print(n->children[1]);
      break;
    case NodeKind::TSInstantiationExpression:
      print(n->children[0]);
      print(n->children[1]);
      break;
    case NodeKind::TSTypeParameterDeclaration:
      put('<');
      list(TSTypeParameterDeclaration(n).params(), ", ");
      put('>');
      break;
    case NodeKind::TSTypeParameter: {
      TSTypeParameter view(n);
      if (view.isConst()) {
        put("const ");
      }
      if (view.isIn()) {
        put("in ");
      }
      if (view.isOut()) {
        put("out ");
      }
      put(n->text);
      optional(" extends ", view.constraint());
      optional(" = ", view.defaultType());
      break;
    }
    case NodeKind::TSTypeParameterInstantiation:
      put('<');
      list(TSTypeParameterInstantiation(n).params(), ", ");
      put('>');
      break;
    case NodeKind::TSInterfaceDeclaration: {
      TSInterfaceDeclaration view(n);
      if (view.isDeclare()) {
        put("declare ");
      }
      put("interface ");
      print(view.id());
      print(view.typeParameters());
      if (view.extends().size() > 0) {
        put(" extends ");
        list(view.extends(), ", ");
      }
      put(' ');
      print(view.body());
      break;
    }
    case NodeKind::TSInterfaceBody: {
      span<Node *> members = TSInterfaceBody(n).body();
      if (members.size() == 0) {
        put("{}");
        break;
      }
      string indent = lineIndent();
      put('{');
      for (Node *m : members) {
        put(style.newline);
        put(string_view(indent.c_str(), indent.size()));
        put(style.indent);
        print(m);
        semi();
      }
      put(style.newline);
      put(string_view(indent.c_str(), indent.size()));
      put('}');
      break;
    }
    case NodeKind::TSInterfaceHeritage:
    case NodeKind::TSClassImplements:
      print(n->children[0]);
      print(n->children[1]);
      break;
    case NodeKind::TSTypeAliasDeclaration: {
      TSTypeAliasDeclaration view(n);
      if (view.isDeclare()) {
        put("declare ");
      }
      put("type ");
      print(view.id());
      print(view.typeParameters());
      put(" = ");
      print(view.typeAnnotation());
      semi();
      break;
    }
    case NodeKind::TSEnumDeclaration: {
      TSEnumDeclaration view(n);
      if (view.isDeclare()) {
        put("declare ");
      }
      if (view.isConst()) {
        put("const ");
      }
      put("enum ");
      print(view.id());
      put(' ');
      span<Node *> members = view.members();
      if (members.size() == 0) {
        put("{}");
        break;
      }
      string indent = lineIndent();
      put('{');
      for (size_t i = 0; i < members.size(); i++) {
        put(style.newline);
        put(string_view(indent.c_str(), indent.size()));
        put(style.indent);
        print(members[i]);
        put(',');
      }
      put(style.newline);
      put(string_view(indent.c_str(), indent.size()));
      put('}');
      break;
    }
    case NodeKind::TSEnumMember: {
      TSEnumMember view(n);
      key(view.id(), view.isComputed());
      optional(" = ", view.initializer());
      break;
    }
    case NodeKind::TSModuleDeclaration: {
      TSModuleDeclaration view(n);
      if (view.isDeclare()) {
        put("declare ");
      }
      switch (view.kind()) {
      case ModuleKind::Module:
        put("module ");
        break;
      case ModuleKind::Namespace:
        put("namespace ");
        break;
      case ModuleKind::Global:
        break;
      }
      print(view.id());
      if (view.body()) {
        put(' ');
        print(view.body());
      } else {
        semi();
      }
      break;
    }
    case NodeKind::TSModuleBlock:
      bracedStatements(TSModuleBlock(n).body());
      break;
    case NodeKind::TSImportEqualsDeclaration: {
      TSImportEqualsDeclaration view(n);
      put("import ");
      if (view.importKind() == ImportKind::Type) {
        put("type ");
      }
      print(view.id());
      put(" = ");
      print(view.moduleReference());
      semi();
      break;
    }
    case NodeKind::TSExternalModuleReference:
      put("require(");
      print(n->children[0]);
      put(')');
      break;
    case NodeKind::TSExportAssignment:
      put("export = ");
      print(n->children[0]);
      semi();
      break;
    case NodeKind::TSNamespaceExportDeclaration:
      put("export as namespace ");
      print(n->children[0]);
      semi();
      break;

    // ------------------------------------------------------------- types
    case NodeKind::TSKeywordType: {
      const KindInfo &info = kindInfo(n->kind);
      uint8_t value = n->dataByte(0);
      put(value < info.enums[0].count ? info.enums[0].values[value] : "any");
      break;
    }
    case NodeKind::TSTypeReference:
      print(n->children[0]);
      print(n->children[1]);
      break;
    case NodeKind::TSQualifiedName:
      print(n->children[0]);
      put('.');
      print(n->children[1]);
      break;
    case NodeKind::TSUnionType:
      list(TSUnionType(n).types(), " | ");
      break;
    case NodeKind::TSIntersectionType:
      list(TSIntersectionType(n).types(), " & ");
      break;
    case NodeKind::TSFunctionType:
      signature(n, false);
      put(" => ");
      print(SignatureLike(n).returnType());
      break;
    case NodeKind::TSConstructorType:
      if (n->hasFlag(Flag::Abstract)) {
        put("abstract ");
      }
      signature(n, true);
      put(" => ");
      print(SignatureLike(n).returnType());
      break;
    case NodeKind::TSConditionalType: {
      TSConditionalType view(n);
      print(view.checkType());
      put(" extends ");
      print(view.extendsType());
      put(" ? ");
      print(view.trueType());
      put(" : ");
      print(view.falseType());
      break;
    }
    case NodeKind::TSInferType:
      put("infer ");
      print(n->children[0]);
      break;
    case NodeKind::TSMappedType:
      mappedType(n);
      break;
    case NodeKind::TSIndexedAccessType:
      print(n->children[0]);
      put('[');
      print(n->children[1]);
      put(']');
      break;
    case NodeKind::TSTypeLiteral: {
      span<Node *> members = TSTypeLiteral(n).members();
      if (members.size() == 0) {
        put("{}");
      } else {
        put("{ ");
        list(members, "; ");
        put(" }");
      }
      break;
    }
    case NodeKind::TSArrayType: {
      Node *element = n->children[0];
      bool wrap = element && (element->kind == NodeKind::TSUnionType ||
                              element->kind == NodeKind::TSIntersectionType ||
                              element->kind == NodeKind::TSFunctionType ||
                              element->kind == NodeKind::TSConstructorType ||
                              element->kind == NodeKind::TSConditionalType ||
                              element->kind == NodeKind::TSTypeOperator ||
                              element->kind == NodeKind::TSInferType);
      if (wrap) {
        put('(');
      }
      print(element);
      if (wrap) {
        put(')');
      }
      put("[]");
      break;
    }
    case NodeKind::TSTupleType:
      put('[');
      list(TSTupleType(n).elementTypes(), ", ");
      put(']');
      break;
    case NodeKind::TSNamedTupleMember: {
      TSNamedTupleMember view(n);
      print(view.label());
      if (view.isOptional()) {
        put('?');
      }
      put(": ");
      print(view.elementType());
      break;
    }
    case NodeKind::TSOptionalType:
      print(n->children[0]);
      put('?');
      break;
    case NodeKind::TSTypeOperator: {
      TSTypeOperator view(n);
      switch (view.op()) {
      case TypeOperator::Keyof:
        put("keyof ");
        break;
      case TypeOperator::Unique:
        put("unique ");
        break;
      case TypeOperator::Readonly:
        put("readonly ");
        break;
      }
      print(view.typeAnnotation());
      break;
    }
    case NodeKind::TSTypeQuery:
      put("typeof ");
      print(n->children[0]);
      print(n->children[1]);
      break;
    case NodeKind::TSTypePredicate: {
      TSTypePredicate view(n);
      if (view.isAsserts()) {
        put("asserts ");
      }
      print(view.parameterName());
      optional(" is ", view.typeAnnotation());
      break;
    }
    case NodeKind::TSLiteralType:
      print(n->children[0]);
      break;
    case NodeKind::TSImportType: {
      TSImportType view(n);
      put("import(");
      print(view.argument());
      put(')');
      optional(".", view.qualifier());
      print(view.typeArguments());
      break;
    }
    case NodeKind::TSPropertySignature: {
      TSPropertySignature view(n);
      if (view.isReadonly()) {
        put("readonly ");
      }
      key(view.key(), view.isComputed());
      if (view.isOptional()) {
        put('?');
      }
      optional(": ", view.typeAnnotation());
      break;
    }
    case NodeKind::TSMethodSignature: {
      TSMethodSignature view(n);
      if (view.kind() == MethodKind::Get) {
        put("get ");
      } else if (view.kind() == MethodKind::Set) {
        put("set ");
      }
      key(view.key(), view.isComputed());
      if (view.isOptional()) {
        put('?');
      }
      signature(n, false);
      optional(": ", view.returnType());
      break;
    }
    case NodeKind::TSCallSignatureDeclaration:
      signature(n, false);
      optional(": ", SignatureLike(n).returnType());
      break;
    case NodeKind::TSConstructSignatureDeclaration:
      signature(n, true);
      optional(": ", SignatureLike(n).returnType());
      break;
    case NodeKind::TSIndexSignature: {
      TSIndexSignature view(n);
      if (view.isStatic()) {
        put("static ");
      }
      if (view.isReadonly()) {
        put("readonly ");
      }
      put('[');
      list(view.parameters(), ", ");
      put(']');
      optional(": ", view.typeAnnotation());
      break;
    }
    case NodeKind::Error:
      break;
    default:
      put("/* fastlint: cannot print ");
      put(kindName(n->kind));
      put(" */");
      break;
    }
  }
};

} // namespace

void printAst(AstFile &file, string &out, PrintOptions options)
{
  Printer printer(file, out, options);
  printer.run();
}

} // namespace fastlint::ast
