#include "fastlint/ast/file.h"
#include "fastlint/ast/generated/views.h"
#include "fastlint/ast/lower.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "fastlint/tsgo/generated/enums.h"
#include "fastlint/tsgo/source_file.h"
#include "testing/test.h"

#include <string>

using namespace fastlint;
using namespace fastlint::tsgo;

namespace {

void putU32(Vector<uint8_t, 4> &out, uint32_t v)
{
  out.append(uint8_t(v));
  out.append(uint8_t(v >> 8));
  out.append(uint8_t(v >> 16));
  out.append(uint8_t(v >> 24));
}

void putNode(
    Vector<uint8_t, 4> &out, uint32_t kind, uint32_t pos, uint32_t end, uint32_t parent)
{
  putU32(out, kind);
  putU32(out, pos);
  putU32(out, end);
  putU32(out, 0);
  putU32(out, parent);
  putU32(out, 0);
  putU32(out, 0);
}

/** The table tsgo would encode for a file holding one call statement, in its 7.0.2 header
 * layout. */
/** The server's table for the fixture statement, every offset moved by `shift` units. */
Vector<uint8_t, 4> encodedFooCall(uint32_t shift = 0)
{
  Vector<uint8_t, 4> out;
  putU32(out, 5u << 24);
  for (int i = 0; i < 9; i++) {
    putU32(out, 0x11111111u * uint32_t(i + 1));
  }
  putU32(out, 44);
  putNode(out, SyntaxKind::SourceFile, 0, 7 + shift, 0);
  putNode(out, kNodeListKind, 0, 7 + shift, 0);
  putNode(out, SyntaxKind::ExpressionStatement, 0, 7 + shift, 0);
  putNode(out, SyntaxKind::CallExpression, 0, 6 + shift, 2);
  putNode(out, SyntaxKind::Identifier, 0, 3 + shift, 3);
  putNode(out, kNodeListKind, 4 + shift, 5 + shift, 3);
  putNode(out, SyntaxKind::NumericLiteral, 4 + shift, 5 + shift, 3);
  putNode(out, SyntaxKind::EndOfFile, 7 + shift, 7 + shift, 0);
  return out;
}

struct Lowered {
  syntax::Diagnostics diagnostics;
  syntax::GrammarTree tree;
  ast::AstFile file;
  ast::Node *root = nullptr;

  explicit Lowered(std::string_view source) : file(&tree)
  {
    syntax::Parser parser(source, {}, diagnostics);
    parser.parseFile(tree);
    root = ast::lower(tree, file);
  }
};

} // namespace

TEST(tsgo_source_file, decodes_header_and_nodes)
{
  Vector<uint8_t, 4> bytes = encodedFooCall();
  EncodedSourceFile encoded;
  string error;
  CHECK(encoded.decode(span<const uint8_t>(bytes.data(), bytes.size()), error));
  CHECK_EQ(std::string(error.c_str()), std::string());
  CHECK_EQ(encoded.protocolVersion, 5u);
  CHECK_EQ(encoded.contentHashLow, 0x2222222211111111ull);
  CHECK_EQ(encoded.contentHashHigh, 0x4444444433333333ull);
  CHECK_EQ(encoded.bytes, bytes.size());
  CHECK_EQ(encoded.nodes.size(), size_t(8));
  CHECK_EQ(encoded.nodes[3].kind, SyntaxKind::CallExpression);
  CHECK_EQ(encoded.nodes[3].end, 6u);
  CHECK_EQ(encoded.nodes[3].parent, 2u);

  CHECK_EQ(encoded.findNode(0, 6, SyntaxKind::CallExpression), 3);
  CHECK_EQ(encoded.findNode(0, 6), 3);
  CHECK_EQ(encoded.findNode(4, 5), 6);
  CHECK_EQ(encoded.findNode(0, 7), 2);
  CHECK_EQ(encoded.findNode(0, 6, SyntaxKind::Identifier), -1);
  CHECK_EQ(encoded.findNode(3, 9), -1);

  Vector<uint8_t, 4> truncated = bytes.slice(0, 40);
  CHECK(!encoded.decode(span<const uint8_t>(truncated.data(), truncated.size()), error));
  CHECK(error.size() > 0);
  Vector<uint8_t, 4> ragged = bytes.slice(0, int(bytes.size()) - 3);
  CHECK(!encoded.decode(span<const uint8_t>(ragged.data(), ragged.size()), error));
}

TEST(tsgo_source_file, formats_handles_and_canonical_paths)
{
  CHECK_EQ(std::string(nodeHandle(43, 214, "c:/dev/x/main.ts").c_str()),
           std::string("43.214.c:/dev/x/main.ts"));
  CHECK_EQ(std::string(canonicalPath("C:\\Dev\\X\\Main.ts", false).c_str()),
           std::string("c:/dev/x/main.ts"));
  CHECK_EQ(std::string(canonicalPath("/Home/Dev/Main.ts", true).c_str()),
           std::string("/Home/Dev/Main.ts"));
}

TEST(tsgo_source_file, utf16_offsets_follow_multibyte_characters)
{
  Utf16Offsets ascii;
  ascii.build("foo(1);");
  CHECK(ascii.ascii());
  CHECK_EQ(ascii.at(0), 0u);
  CHECK_EQ(ascii.at(7), 7u);

  // `§` is two bytes and one unit; the emoji is four bytes and two units.
  Utf16Offsets mixed;
  mixed.build("a\xc2\xa7"
              "b\xf0\x9f\x98\x80"
              "c");
  CHECK(!mixed.ascii());
  CHECK_EQ(mixed.at(0), 0u);
  CHECK_EQ(mixed.at(1), 1u);
  CHECK_EQ(mixed.at(2), 1u);
  CHECK_EQ(mixed.at(3), 2u);
  CHECK_EQ(mixed.at(4), 3u);
  CHECK_EQ(mixed.at(6), 3u);
  CHECK_EQ(mixed.at(8), 5u);
  CHECK_EQ(mixed.at(9), 6u);

  // A byte order mark is dropped by the server, not counted as a unit.
  Utf16Offsets bom;
  bom.build("\xef\xbb\xbf"
            "ab");
  CHECK(!bom.ascii());
  CHECK_EQ(bom.at(0), 0u);
  CHECK_EQ(bom.at(3), 0u);
  CHECK_EQ(bom.at(4), 1u);
  CHECK_EQ(bom.at(5), 2u);
}

TEST(tsgo_source_file, node_index_table_converts_byte_offsets)
{
  // The leading comment `/*§*/` is six bytes to us and five units to the server, so the
  // same table shifted by five must still pair every node.
  Vector<uint8_t, 4> bytes = encodedFooCall(5);
  EncodedSourceFile encoded;
  string error;
  CHECK(encoded.decode(span<const uint8_t>(bytes.data(), bytes.size()), error));

  Lowered l("/*\xc2\xa7*/foo(1);");
  NodeIndexTable table;
  table.build(l.file, encoded);
  CHECK_EQ(table.mappedCount(), 5);
  ast::Node *statement = l.root->children[0];
  ast::CallExpression call = statement->children[0]->as<ast::CallExpression>();
  CHECK(bool(call));
  CHECK_EQ(call.node()->start, 6u);
  CHECK_EQ(table.lookup(statement), 2);
  CHECK_EQ(table.lookup(call.node()), 3);
  CHECK_EQ(table.lookup(call.arguments()[0]), 6);
}

TEST(tsgo_source_file, node_index_table_pairs_nodes_by_span)
{
  Vector<uint8_t, 4> bytes = encodedFooCall();
  EncodedSourceFile encoded;
  string error;
  CHECK(encoded.decode(span<const uint8_t>(bytes.data(), bytes.size()), error));

  Lowered l("foo(1);");
  NodeIndexTable table;
  table.build(l.file, encoded);
  CHECK_EQ(table.mappedCount(), 5);

  ast::Node *statement = l.root->children[0];
  ast::CallExpression call = statement->children[0]->as<ast::CallExpression>();
  CHECK(bool(call));
  CHECK_EQ(table.lookup(l.root), 0);
  CHECK_EQ(table.lookup(statement), 2);
  CHECK_EQ(table.lookup(call.node()), 3);
  CHECK_EQ(table.lookup(call.callee()), 4);
  CHECK_EQ(table.lookup(call.arguments()[0]), 6);
  CHECK_EQ(std::string(table.handle(call.node(), "c:/x/main.ts").c_str()),
           std::string("3.214.c:/x/main.ts"));

  Lowered other("bar;");
  CHECK_EQ(table.lookup(other.root->children[0]), -1);
  CHECK_EQ(table.handle(other.root, "c:/x/main.ts").size(), size_t(0));

  table.clear();
  CHECK_EQ(table.mappedCount(), 0);
  CHECK_EQ(table.lookup(l.root), -1);
}
