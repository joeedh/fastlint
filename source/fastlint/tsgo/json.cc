#include "fastlint/tsgo/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fastlint::tsgo {

namespace {

void appendView(std::string &out, std::string_view text)
{
  out.append(text);
}

void appendUtf8(std::string &out, uint32_t cp)
{
  if (cp < 0x80) {
    out += char(cp);
  } else if (cp < 0x800) {
    out += char(0xC0 | (cp >> 6));
    out += char(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += char(0xE0 | (cp >> 12));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  } else {
    out += char(0xF0 | (cp >> 18));
    out += char(0x80 | ((cp >> 12) & 0x3F));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  }
}

constexpr int kMaxDepth = 512;

class Parser {
public:
  Parser(std::string_view text, JsonDocument &doc, string &error)
      : m_text(text), m_doc(doc), m_error(error)
  {
  }

  JsonValue *parseDocument()
  {
    skipSpace();
    JsonValue *v = parseValue(0);
    if (!v) {
      return nullptr;
    }
    skipSpace();
    if (m_at != m_text.size()) {
      return fail("trailing characters after the document");
    }
    return v;
  }

private:
  JsonValue *fail(const char *message)
  {
    if (m_error.size() == 0) {
      char buf[128];
      snprintf(buf, sizeof buf, "%s at offset %zu", message, m_at);
      m_error = string(buf);
    }
    return nullptr;
  }

  bool more() const
  {
    return m_at < m_text.size();
  }

  char peek() const
  {
    return more() ? m_text[m_at] : '\0';
  }

  void skipSpace()
  {
    while (more()) {
      char c = m_text[m_at];
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
        break;
      }
      m_at++;
    }
  }

  bool consumeWord(const char *word)
  {
    size_t len = strlen(word);
    if (m_text.substr(m_at, len) != word) {
      return false;
    }
    m_at += len;
    return true;
  }

  JsonValue *parseValue(int depth)
  {
    if (depth > kMaxDepth) {
      return fail("nesting too deep");
    }
    switch (peek()) {
    case '{':
      return parseObject(depth);
    case '[':
      return parseArray(depth);
    case '"': {
      JsonValue *v = m_doc.make(JsonKind::String);
      std::string buffer;
      if (!parseString(buffer)) {
        return nullptr;
      }
      v->text += buffer;
      return v;
    }
    case 't':
      if (consumeWord("true")) {
        JsonValue *v = m_doc.make(JsonKind::Bool);
        v->boolean = true;
        return v;
      }
      return fail("unexpected token");
    case 'f':
      if (consumeWord("false")) {
        return m_doc.make(JsonKind::Bool);
      }
      return fail("unexpected token");
    case 'n':
      if (consumeWord("null")) {
        return m_doc.make(JsonKind::Null);
      }
      return fail("unexpected token");
    default:
      if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
        return parseNumber();
      }
      return fail(more() ? "unexpected character" : "unexpected end of input");
    }
  }

  JsonValue *parseNumber()
  {
    size_t start = m_at;
    if (peek() == '-') {
      m_at++;
    }
    if (!(peek() >= '0' && peek() <= '9')) {
      return fail("malformed number");
    }
    while (peek() >= '0' && peek() <= '9') {
      m_at++;
    }
    if (peek() == '.') {
      m_at++;
      if (!(peek() >= '0' && peek() <= '9')) {
        return fail("malformed number");
      }
      while (peek() >= '0' && peek() <= '9') {
        m_at++;
      }
    }
    if (peek() == 'e' || peek() == 'E') {
      m_at++;
      if (peek() == '+' || peek() == '-') {
        m_at++;
      }
      if (!(peek() >= '0' && peek() <= '9')) {
        return fail("malformed number");
      }
      while (peek() >= '0' && peek() <= '9') {
        m_at++;
      }
    }
    char buf[64];
    size_t len = m_at - start;
    if (len >= sizeof buf) {
      return fail("number too long");
    }
    memcpy(buf, m_text.data() + start, len);
    buf[len] = '\0';
    JsonValue *v = m_doc.make(JsonKind::Number);
    v->number = strtod(buf, nullptr);
    return v;
  }

  bool parseHex4(uint32_t &out)
  {
    if (m_at + 4 > m_text.size()) {
      return false;
    }
    out = 0;
    for (int i = 0; i < 4; i++) {
      char c = m_text[m_at + i];
      uint32_t digit;
      if (c >= '0' && c <= '9') {
        digit = uint32_t(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        digit = uint32_t(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        digit = uint32_t(c - 'A' + 10);
      } else {
        return false;
      }
      out = (out << 4) | digit;
    }
    m_at += 4;
    return true;
  }

  /** Decodes into a std::string so the per-character appends stay linear. */
  bool parseString(std::string &out)
  {
    m_at++;
    while (true) {
      if (!more()) {
        fail("unterminated string");
        return false;
      }
      char c = m_text[m_at++];
      if (c == '"') {
        return true;
      }
      if (uint8_t(c) < 0x20) {
        fail("control character in string");
        return false;
      }
      if (c != '\\') {
        out += c;
        continue;
      }
      if (!more()) {
        fail("unterminated escape");
        return false;
      }
      char e = m_text[m_at++];
      switch (e) {
      case '"':
      case '\\':
      case '/':
        out += e;
        break;
      case 'b':
        out += '\b';
        break;
      case 'f':
        out += '\f';
        break;
      case 'n':
        out += '\n';
        break;
      case 'r':
        out += '\r';
        break;
      case 't':
        out += '\t';
        break;
      case 'u': {
        uint32_t cp;
        if (!parseHex4(cp)) {
          fail("malformed \\u escape");
          return false;
        }
        if (cp >= 0xD800 && cp < 0xDC00) {
          uint32_t low = 0;
          if (m_text.substr(m_at, 2) == "\\u") {
            size_t save = m_at;
            m_at += 2;
            if (!parseHex4(low) || low < 0xDC00 || low >= 0xE000) {
              m_at = save;
              low = 0;
            }
          }
          if (low) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          } else {
            cp = 0xFFFD;
          }
        } else if (cp >= 0xDC00 && cp < 0xE000) {
          cp = 0xFFFD;
        }
        appendUtf8(out, cp);
        break;
      }
      default:
        fail("unknown escape");
        return false;
      }
    }
  }

  JsonValue *parseArray(int depth)
  {
    m_at++;
    JsonValue *arr = m_doc.make(JsonKind::Array);
    skipSpace();
    if (peek() == ']') {
      m_at++;
      return arr;
    }
    while (true) {
      skipSpace();
      JsonValue *item = parseValue(depth + 1);
      if (!item) {
        return nullptr;
      }
      arr->items.append(item);
      skipSpace();
      char c = peek();
      if (c == ',') {
        m_at++;
        continue;
      }
      if (c == ']') {
        m_at++;
        return arr;
      }
      return fail("expected ',' or ']'");
    }
  }

  JsonValue *parseObject(int depth)
  {
    m_at++;
    JsonValue *obj = m_doc.make(JsonKind::Object);
    skipSpace();
    if (peek() == '}') {
      m_at++;
      return obj;
    }
    while (true) {
      skipSpace();
      if (peek() != '"') {
        return fail("expected a member name");
      }
      std::string buffer;
      if (!parseString(buffer)) {
        return nullptr;
      }
      string key;
      key += buffer;
      skipSpace();
      if (peek() != ':') {
        return fail("expected ':'");
      }
      m_at++;
      skipSpace();
      JsonValue *item = parseValue(depth + 1);
      if (!item) {
        return nullptr;
      }
      obj->keys.append(std::move(key));
      obj->items.append(item);
      skipSpace();
      char c = peek();
      if (c == ',') {
        m_at++;
        continue;
      }
      if (c == '}') {
        m_at++;
        return obj;
      }
      return fail("expected ',' or '}'");
    }
  }

  std::string_view m_text;
  JsonDocument &m_doc;
  string &m_error;
  size_t m_at = 0;
};

} // namespace

// ---------------------------------------------------------------- JsonValue

int JsonValue::asInt(int fallback) const
{
  return kind == JsonKind::Number ? int(number) : fallback;
}

uint32_t JsonValue::asUint(uint32_t fallback) const
{
  return kind == JsonKind::Number && number >= 0 ? uint32_t(number) : fallback;
}

int64_t JsonValue::asInt64(int64_t fallback) const
{
  return kind == JsonKind::Number ? int64_t(number) : fallback;
}

double JsonValue::asDouble(double fallback) const
{
  return kind == JsonKind::Number ? number : fallback;
}

bool JsonValue::asBool(bool fallback) const
{
  return kind == JsonKind::Bool ? boolean : fallback;
}

std::string_view JsonValue::asString() const
{
  if (kind != JsonKind::String) {
    return {};
  }
  return std::string_view(text.c_str(), text.size());
}

const JsonValue *JsonValue::at(int index) const
{
  if (index < 0 || index >= int(items.size())) {
    return nullptr;
  }
  return items[index];
}

const JsonValue *JsonValue::get(std::string_view key) const
{
  if (kind != JsonKind::Object) {
    return nullptr;
  }
  for (int i = 0; i < int(keys.size()); i++) {
    const string &k = keys[i];
    if (std::string_view(k.c_str(), k.size()) == key) {
      return items[i];
    }
  }
  return nullptr;
}

int JsonValue::getInt(std::string_view key, int fallback) const
{
  const JsonValue *v = get(key);
  return v ? v->asInt(fallback) : fallback;
}

uint32_t JsonValue::getUint(std::string_view key, uint32_t fallback) const
{
  const JsonValue *v = get(key);
  return v ? v->asUint(fallback) : fallback;
}

bool JsonValue::getBool(std::string_view key, bool fallback) const
{
  const JsonValue *v = get(key);
  return v ? v->asBool(fallback) : fallback;
}

std::string_view JsonValue::getString(std::string_view key) const
{
  const JsonValue *v = get(key);
  return v ? v->asString() : std::string_view();
}

// ---------------------------------------------------------------- JsonDocument

bool JsonDocument::parse(std::string_view text)
{
  clear();
  Parser parser(text, *this, m_error);
  m_root = parser.parseDocument();
  if (!m_root) {
    m_pool.clear();
  }
  return m_root != nullptr;
}

void JsonDocument::clear()
{
  m_pool.clear();
  m_root = nullptr;
  m_error = string();
}

JsonValue *JsonDocument::make(JsonKind kind)
{
  JsonValue *v = m_pool.alloc();
  v->kind = kind;
  return v;
}

// ---------------------------------------------------------------- JsonWriter

void appendJsonString(std::string &out, std::string_view text)
{
  static const char hex[] = "0123456789abcdef";
  out += '"';
  for (char c : text) {
    switch (c) {
    case '"':
      out += '\\';
      out += '"';
      break;
    case '\\':
      out += '\\';
      out += '\\';
      break;
    case '\n':
      out += '\\';
      out += 'n';
      break;
    case '\r':
      out += '\\';
      out += 'r';
      break;
    case '\t':
      out += '\\';
      out += 't';
      break;
    case '\b':
      out += '\\';
      out += 'b';
      break;
    case '\f':
      out += '\\';
      out += 'f';
      break;
    default:
      if (uint8_t(c) < 0x20) {
        out += '\\';
        out += 'u';
        out += '0';
        out += '0';
        out += hex[(uint8_t(c) >> 4) & 0xF];
        out += hex[uint8_t(c) & 0xF];
      } else {
        out += c;
      }
    }
  }
  out += '"';
}

void JsonWriter::separate()
{
  if (m_afterKey) {
    m_afterKey = false;
    return;
  }
  if (m_hasValue.isEmpty()) {
    return;
  }
  if (m_hasValue.last()) {
    m_out += ',';
  }
  m_hasValue.last() = true;
}

void JsonWriter::beginObject()
{
  separate();
  m_out += '{';
  m_hasValue.append(false);
}

void JsonWriter::endObject()
{
  m_out += '}';
  m_hasValue.pop_back();
}

void JsonWriter::beginArray()
{
  separate();
  m_out += '[';
  m_hasValue.append(false);
}

void JsonWriter::endArray()
{
  m_out += ']';
  m_hasValue.pop_back();
}

void JsonWriter::key(std::string_view name)
{
  separate();
  quoted(name);
  m_out += ':';
  m_afterKey = true;
}

void JsonWriter::quoted(std::string_view text)
{
  appendJsonString(m_out, text);
}

void JsonWriter::value(std::string_view text)
{
  separate();
  quoted(text);
}

void JsonWriter::value(int number)
{
  value(int64_t(number));
}

void JsonWriter::value(uint32_t number)
{
  value(int64_t(number));
}

void JsonWriter::value(int64_t number)
{
  separate();
  char buf[32];
  snprintf(buf, sizeof buf, "%lld", static_cast<long long>(number));
  appendView(m_out, buf);
}

void JsonWriter::value(double number)
{
  separate();
  if (!std::isfinite(number)) {
    appendView(m_out, "null");
    return;
  }
  char buf[32];
  if (number == std::floor(number) && std::fabs(number) < 1e15) {
    snprintf(buf, sizeof buf, "%lld", static_cast<long long>(number));
  } else {
    snprintf(buf, sizeof buf, "%.17g", number);
  }
  appendView(m_out, buf);
}

void JsonWriter::value(bool boolean)
{
  separate();
  appendView(m_out, boolean ? "true" : "false");
}

void JsonWriter::null()
{
  separate();
  appendView(m_out, "null");
}

void JsonWriter::raw(std::string_view json)
{
  separate();
  appendView(m_out, json);
}

void JsonWriter::clear()
{
  m_out.clear();
  m_hasValue.clear();
  m_afterKey = false;
}

} // namespace fastlint::tsgo
