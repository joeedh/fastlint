#include "fastlint/tsgo/json.h"
#include "testing/test.h"

#include <string>

using namespace fastlint;
using namespace fastlint::tsgo;

namespace {

std::string str(std::string_view view)
{
  return std::string(view);
}

} // namespace

TEST(tsgo_json, parses_nested_documents)
{
  JsonDocument doc;
  CHECK(doc.parse(
      R"({"id": 14, "flags": 32, "name": "string", "list": [1, 2.5, -3e2, true, null],
                     "nested": {"deep": {"x": false}}, "empty": {}, "none": []})"));
  const JsonValue *root = doc.root();
  CHECK(root != nullptr);
  CHECK(root->isObject());
  CHECK_EQ(root->size(), 7);
  CHECK_EQ(root->getInt("id"), 14);
  CHECK_EQ(root->getUint("flags"), 32u);
  CHECK_EQ(str(root->getString("name")), std::string("string"));
  const JsonValue *list = root->get("list");
  CHECK(list != nullptr);
  CHECK(list->isArray());
  CHECK_EQ(list->size(), 5);
  CHECK_EQ(list->at(0)->asInt(), 1);
  CHECK_EQ(list->at(1)->asDouble(), 2.5);
  CHECK_EQ(list->at(2)->asDouble(), -300.0);
  CHECK(list->at(3)->asBool());
  CHECK(list->at(4)->isNull());
  CHECK(list->at(5) == nullptr);
  CHECK(!root->get("nested")->get("deep")->getBool("x", true));
  CHECK_EQ(root->get("empty")->size(), 0);
  CHECK_EQ(root->get("none")->size(), 0);
  CHECK(root->get("missing") == nullptr);
  CHECK_EQ(root->getInt("missing", -1), -1);
  CHECK_EQ(root->getInt("name", -1), -1);
}

TEST(tsgo_json, decodes_escapes)
{
  JsonDocument doc;
  CHECK(
      doc.parse(R"(["a\"b\\c\/d\n\t\r\b\f", "\u0041\u00e9", "\ud83d\ude00", "\udc00"])"));
  const JsonValue *root = doc.root();
  CHECK_EQ(str(root->at(0)->asString()), std::string("a\"b\\c/d\n\t\r\b\f"));
  CHECK_EQ(str(root->at(1)->asString()), std::string("A\xC3\xA9"));
  CHECK_EQ(str(root->at(2)->asString()), std::string("\xF0\x9F\x98\x80"));
  // A lone surrogate becomes U+FFFD rather than invalid UTF-8.
  CHECK_EQ(str(root->at(3)->asString()), std::string("\xEF\xBF\xBD"));
}

TEST(tsgo_json, rejects_malformed_text)
{
  const char *bad[] = {
      "",
      "{",
      "[1,]",
      "{\"a\" 1}",
      "{\"a\":1} x",
      "\"unterminated",
      "tru",
      "[1 2]",
      "\"\\x\"",
      "01x",
      "{1: 2}",
  };
  for (const char *text : bad) {
    JsonDocument doc;
    INFO("text: %s", text);
    CHECK(!doc.parse(text));
    CHECK(doc.root() == nullptr);
    CHECK(doc.error().size() > 0);
  }
  JsonDocument ok;
  CHECK(ok.parse("  null  "));
  CHECK(ok.root()->isNull());
  CHECK_EQ(ok.error().size(), size_t(0));
}

TEST(tsgo_json, writer_builds_documents)
{
  JsonWriter w;
  w.beginObject();
  w.member("snapshot", 3);
  w.member("project", "c:/x/tsconfig.json");
  w.key("positions");
  w.beginArray();
  w.value(uint32_t(1));
  w.value(int64_t(-2));
  w.value(2.5);
  w.value(true);
  w.null();
  w.beginObject();
  w.endObject();
  w.endArray();
  w.key("text");
  w.value(std::string_view("quote\" backslash\\ newline\n tab\t bell\x01"));
  w.key("raw");
  w.raw("[9]");
  w.endObject();
  CHECK_EQ(
      str(w.text()),
      std::string(
          R"({"snapshot":3,"project":"c:/x/tsconfig.json","positions":[1,-2,2.5,true,null,{}],)"
          R"("text":"quote\" backslash\\ newline\n tab\t bell\u0001","raw":[9]})"));

  JsonDocument doc;
  CHECK(doc.parse(w.text()));
  CHECK_EQ(doc.root()->getInt("snapshot"), 3);
  CHECK_EQ(doc.root()->get("positions")->size(), 6);
  CHECK_EQ(str(doc.root()->getString("text")),
           std::string("quote\" backslash\\ newline\n tab\t bell\x01"));

  w.clear();
  w.beginArray();
  w.endArray();
  CHECK_EQ(str(w.text()), std::string("[]"));
}

TEST(tsgo_json, documents_reparse_cleanly)
{
  JsonDocument doc;
  CHECK(doc.parse("[1, 2, 3]"));
  CHECK_EQ(doc.root()->size(), 3);
  CHECK(!doc.parse("[1, 2"));
  CHECK(doc.root() == nullptr);
  CHECK(doc.parse("{\"a\": \"b\"}"));
  CHECK_EQ(str(doc.root()->getString("a")), std::string("b"));
  CHECK_EQ(doc.error().size(), size_t(0));
}
