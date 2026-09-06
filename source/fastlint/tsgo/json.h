#pragma once

#include "util/pool.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

namespace fastlint::tsgo {

using litestl::util::Pool;
using litestl::util::Vector;
using string = litestl::util::string;

enum class JsonKind : uint8_t { Null, Bool, Number, String, Array, Object };

/** One node of a parsed JSON document; arrays and objects own their children through the
 * document's pool. */
struct JsonValue {
  JsonKind kind = JsonKind::Null;
  bool boolean = false;
  double number = 0;
  string text;
  /** Object member names, parallel to `items`; empty for arrays. */
  Vector<string, 1> keys;
  Vector<JsonValue *, 1> items;

  bool isNull() const
  {
    return kind == JsonKind::Null;
  }
  bool isNumber() const
  {
    return kind == JsonKind::Number;
  }
  bool isString() const
  {
    return kind == JsonKind::String;
  }
  bool isArray() const
  {
    return kind == JsonKind::Array;
  }
  bool isObject() const
  {
    return kind == JsonKind::Object;
  }

  /** Numeric value truncated to an int; `fallback` for anything that is not a number. */
  int asInt(int fallback = 0) const;
  uint32_t asUint(uint32_t fallback = 0) const;
  int64_t asInt64(int64_t fallback = 0) const;
  double asDouble(double fallback = 0) const;
  bool asBool(bool fallback = false) const;
  /** String contents, or empty for anything that is not a string. */
  std::string_view asString() const;

  /** Element or member count; 0 for scalars. */
  int size() const
  {
    return int(items.size());
  }
  /** Array element or object member value at `index`, or null when out of range. */
  const JsonValue *at(int index) const;
  /** Member value by name, or null when absent or not an object. */
  const JsonValue *get(std::string_view key) const;
  /** Member by name, treated as an integer; `fallback` when absent or not a number. */
  int getInt(std::string_view key, int fallback = 0) const;
  uint32_t getUint(std::string_view key, uint32_t fallback = 0) const;
  bool getBool(std::string_view key, bool fallback = false) const;
  std::string_view getString(std::string_view key) const;
};

/** Owns the values of one parsed JSON text. */
class JsonDocument {
public:
  JsonDocument() = default;
  JsonDocument(const JsonDocument &) = delete;
  JsonDocument &operator=(const JsonDocument &) = delete;

  /** Parses `text` into `root()`; on failure `root()` is null and `error()` describes the
   * first problem. Previous contents are discarded. */
  bool parse(std::string_view text);
  void clear();

  const JsonValue *root() const
  {
    return m_root;
  }
  const string &error() const
  {
    return m_error;
  }

  /** Allocates a fresh value of `kind` owned by this document. */
  JsonValue *make(JsonKind kind);

private:
  Pool<JsonValue, 64> m_pool;
  JsonValue *m_root = nullptr;
  string m_error;
};

/** Appends a JSON text piece by piece; commas and quoting are handled here. */
class JsonWriter {
public:
  void beginObject();
  void endObject();
  void beginArray();
  void endArray();
  /** Names the next member; must be followed by exactly one value. */
  void key(std::string_view name);
  void value(std::string_view text);
  void value(const char *text)
  {
    value(std::string_view(text));
  }
  void value(int number);
  void value(uint32_t number);
  void value(int64_t number);
  void value(double number);
  void value(bool boolean);
  void null();
  /** Copies an already encoded JSON fragment in as the next value. */
  void raw(std::string_view json);

  /** Convenience for `key(name); value(v);`. */
  template <typename T> void member(std::string_view name, T v)
  {
    key(name);
    value(v);
  }

  std::string_view text() const
  {
    return std::string_view(m_out.c_str(), m_out.size());
  }
  const string &str() const
  {
    return m_out;
  }
  void clear();

private:
  void separate();
  void quoted(std::string_view text);

  string m_out;
  /** One entry per open container: true once it holds a value. */
  Vector<bool, 8> m_hasValue;
  /** True between `key()` and its value, where no comma may be inserted. */
  bool m_afterKey = false;
};

/** Appends `text` to `out` as a quoted JSON string with escapes. */
void appendJsonString(string &out, std::string_view text);

} // namespace fastlint::tsgo
