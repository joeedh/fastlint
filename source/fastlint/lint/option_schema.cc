#include "fastlint/lint/option_schema.h"

#include "util/vector.h"

#include <cstdio>

namespace fastlint::lint {

namespace {

using tsgo::JsonKind;
using tsgo::JsonValue;

void append(string &out, string_view text)
{
  for (char c : text) {
    out += c;
  }
}

void appendNumber(string &out, int value)
{
  char buffer[16];
  std::snprintf(buffer, sizeof buffer, "%d", value);
  append(out, buffer);
}

/** A JSON scalar spelled for an error message: strings quoted, the rest bare. */
void describe(string &out, const JsonValue *value)
{
  switch (value->kind) {
  case JsonKind::Null:
    append(out, "null");
    break;
  case JsonKind::Bool:
    append(out, value->asBool() ? "true" : "false");
    break;
  case JsonKind::Number:
    appendNumber(out, value->asInt());
    break;
  case JsonKind::String:
    out += '"';
    append(out, value->asString());
    out += '"';
    break;
  case JsonKind::Array:
    append(out, "an array");
    break;
  case JsonKind::Object:
    append(out, "an object");
    break;
  }
}

bool matchesType(string_view type, const JsonValue *value)
{
  if (type == "string") {
    return value->isString();
  }
  if (type == "boolean") {
    return value->kind == JsonKind::Bool;
  }
  if (type == "integer" || type == "number") {
    return value->isNumber();
  }
  if (type == "array") {
    return value->isArray();
  }
  if (type == "object") {
    return value->isObject();
  }
  if (type == "null") {
    return value->isNull();
  }
  return false;
}

bool equalScalar(const JsonValue *a, const JsonValue *b)
{
  if (a->kind != b->kind) {
    return false;
  }
  switch (a->kind) {
  case JsonKind::String:
    return a->asString() == b->asString();
  case JsonKind::Bool:
    return a->asBool() == b->asBool();
  case JsonKind::Number:
    return a->asDouble() == b->asDouble();
  case JsonKind::Null:
    return true;
  default:
    return false;
  }
}

// The `path` names the offending option in errors ("option 0", "option 0.null").
bool validate(const JsonValue *schema,
              const JsonValue *value,
              const string &path,
              string &error);

/** True when `value` satisfies at least one of the `oneOf`/`anyOf` branches. */
bool matchesAny(const JsonValue *branches, const JsonValue *value)
{
  string sink;
  for (int i = 0; i < branches->size(); i++) {
    if (validate(branches->at(i), value, sink, sink)) {
      return true;
    }
    sink = string();
  }
  return false;
}

void expected(string &error, const string &path, const char *want, const JsonValue *got)
{
  error = string();
  append(error, path);
  append(error, ": expected ");
  append(error, want);
  append(error, ", got ");
  describe(error, got);
}

bool validate(const JsonValue *schema,
              const JsonValue *value,
              const string &path,
              string &error)
{
  if (const JsonValue *branches =
          schema->get("oneOf") ? schema->get("oneOf") : schema->get("anyOf"))
  {
    if (!matchesAny(branches, value)) {
      error = string();
      append(error, path);
      append(error, ": no allowed form matches ");
      describe(error, value);
      return false;
    }
    return true;
  }

  if (const JsonValue *choices = schema->get("enum")) {
    for (int i = 0; i < choices->size(); i++) {
      if (equalScalar(choices->at(i), value)) {
        return true;
      }
    }
    error = string();
    append(error, path);
    append(error, ": ");
    describe(error, value);
    append(error, " is not one of ");
    for (int i = 0; i < choices->size(); i++) {
      if (i > 0) {
        append(error, ", ");
      }
      describe(error, choices->at(i));
    }
    return false;
  }

  const JsonValue *type = schema->get("type");
  if (type && type->isString() && !matchesType(type->asString(), value)) {
    string want;
    append(want, type->asString());
    expected(error, path, want.c_str(), value);
    return false;
  }

  if (type && type->isString() && type->asString() == "object" && value->isObject()) {
    const JsonValue *props = schema->get("properties");
    const JsonValue *additional = schema->get("additionalProperties");
    bool closed =
        additional && additional->kind == JsonKind::Bool && !additional->asBool();
    for (int i = 0; i < value->size(); i++) {
      string_view key = string_view(value->keys[i].c_str(), value->keys[i].size());
      const JsonValue *sub = props ? props->get(key) : nullptr;
      if (!sub) {
        if (closed) {
          error = string();
          append(error, path);
          append(error, ": unknown option \"");
          append(error, key);
          error += '"';
          return false;
        }
        continue;
      }
      string childPath;
      append(childPath, path);
      childPath += '.';
      append(childPath, key);
      if (!validate(sub, value->at(i), childPath, error)) {
        return false;
      }
    }
    if (const JsonValue *required = schema->get("required")) {
      for (int i = 0; i < required->size(); i++) {
        string_view name = required->at(i)->asString();
        if (!value->get(name)) {
          error = string();
          append(error, path);
          append(error, ": missing required option \"");
          append(error, name);
          error += '"';
          return false;
        }
      }
    }
  }

  if (type && type->isString() && type->asString() == "array" && value->isArray()) {
    if (const JsonValue *items = schema->get("items")) {
      for (int i = 0; i < value->size(); i++) {
        string childPath;
        append(childPath, path);
        childPath += '[';
        appendNumber(childPath, i);
        childPath += ']';
        if (!validate(items, value->at(i), childPath, error)) {
          return false;
        }
      }
    }
    const JsonValue *minItems = schema->get("minItems");
    if (minItems && value->size() < minItems->asInt()) {
      expected(error, path, "more items", value);
      return false;
    }
    const JsonValue *maxItems = schema->get("maxItems");
    if (maxItems && value->size() > maxItems->asInt()) {
      expected(error, path, "fewer items", value);
      return false;
    }
  }
  return true;
}

} // namespace

bool validateOptions(const RuleDef &rule, const JsonValue *setting, string &error)
{
  if (!rule.meta.schema || !setting || !setting->isArray()) {
    return true;
  }
  tsgo::JsonDocument doc;
  if (!doc.parse(rule.meta.schema)) {
    // A malformed built-in schema is a fastlint bug, not a user's; let the
    // options through rather than reject a valid config.
    return true;
  }
  const JsonValue *schema = doc.root();
  if (!schema || !schema->isArray()) {
    return true;
  }
  string prefix;
  append(prefix, "config: rule \"");
  append(prefix, rule.meta.name);
  append(prefix, "\" option ");
  // Options follow the severity at index 0; schema[k] governs option k.
  int options = setting->size() - 1;
  if (options > schema->size()) {
    error = string();
    append(error, prefix);
    appendNumber(error, schema->size());
    append(error, ": the rule takes no such option");
    return false;
  }
  for (int k = 0; k < options; k++) {
    string path;
    append(path, prefix);
    appendNumber(path, k);
    if (!validate(schema->at(k), setting->at(k + 1), path, error)) {
      return false;
    }
  }
  return true;
}

} // namespace fastlint::lint
