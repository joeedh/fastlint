#include "fastlint/lint/result_cache.h"

#include "fastlint/tsgo/json.h"

namespace fastlint::lint {

namespace {

using tsgo::JsonValue;
using tsgo::JsonWriter;

void append(string &out, string_view text)
{
  for (char c : text) {
    out += c;
  }
}

string_view view(const string &s)
{
  return string_view(s.c_str(), s.size());
}

/** The rule's own static id string equal to `id`, so a replayed diagnostic's
 * `messageId` outlives the payload. Null when no message matches. */
const char *staticMessageId(const RuleDef *rule, string_view id)
{
  if (!rule) {
    return nullptr;
  }
  for (const Message &message : rule->meta.messages) {
    if (id == message.id) {
      return message.id;
    }
  }
  return nullptr;
}

} // namespace

void serializeResult(const FileResult &result, string &out)
{
  JsonWriter w;
  w.beginObject();
  w.member("e", result.errorCount);
  w.member("w", result.warningCount);
  w.member("fe", result.fixableErrorCount);
  w.member("fw", result.fixableWarningCount);
  w.key("d");
  w.beginArray();
  for (const Diagnostic &d : result.diagnostics) {
    w.beginObject();
    w.key("r");
    if (d.rule) {
      w.value(d.rule->meta.name);
    } else {
      w.null();
    }
    w.member("s", int(d.severity));
    if (d.fatal) {
      w.member("fatal", true);
    }
    if (d.fixable) {
      w.member("fixable", true);
    }
    w.member("a", d.start);
    w.member("b", d.end);
    w.member("ln", d.line);
    w.member("co", d.column);
    w.member("eln", d.endLine);
    w.member("eco", d.endColumn);
    if (d.messageId) {
      w.member("mid", d.messageId);
    }
    w.member("m", view(d.message));
    if (!d.suggestions.isEmpty()) {
      w.key("sug");
      w.beginArray();
      for (const SuggestionResult &s : d.suggestions) {
        w.beginObject();
        w.member("mid", s.messageId ? s.messageId : "");
        w.member("desc", view(s.message));
        if (s.hasFix) {
          w.key("fix");
          w.beginObject();
          w.member("a", s.fixStart);
          w.member("b", s.fixEnd);
          w.member("t", view(s.fixText));
          w.endObject();
        }
        w.endObject();
      }
      w.endArray();
    }
    if (d.hasFix) {
      w.key("fix");
      w.beginObject();
      w.member("a", d.fixStart);
      w.member("b", d.fixEnd);
      w.member("t", view(d.fixText));
      w.endObject();
    }
    w.endObject();
  }
  w.endArray();
  w.endObject();
  append(out, w.text());
}

bool deserializeResult(string_view payload, const Registry &registry, FileResult &out)
{
  tsgo::JsonDocument doc;
  if (!doc.parse(payload)) {
    return false;
  }
  const JsonValue *root = doc.root();
  if (!root || !root->isObject()) {
    return false;
  }
  out.errorCount = root->getInt("e");
  out.warningCount = root->getInt("w");
  out.fixableErrorCount = root->getInt("fe");
  out.fixableWarningCount = root->getInt("fw");
  const JsonValue *list = root->get("d");
  if (!list || !list->isArray()) {
    return false;
  }
  for (int i = 0; i < list->size(); i++) {
    const JsonValue *item = list->at(i);
    if (!item || !item->isObject()) {
      return false;
    }
    Diagnostic d;
    const JsonValue *rule = item->get("r");
    if (rule && rule->isString()) {
      d.rule = registry.find(rule->asString());
    }
    d.severity = Severity(item->getInt("s"));
    d.fatal = item->getBool("fatal");
    d.fixable = item->getBool("fixable");
    d.start = item->getUint("a");
    d.end = item->getUint("b");
    d.line = item->getUint("ln");
    d.column = item->getUint("co");
    d.endLine = item->getUint("eln");
    d.endColumn = item->getUint("eco");
    d.messageId = staticMessageId(d.rule, item->getString("mid"));
    append(d.message, item->getString("m"));
    if (const JsonValue *suggestions = item->get("sug")) {
      for (int j = 0; j < suggestions->size(); j++) {
        const JsonValue *s = suggestions->at(j);
        SuggestionResult result{staticMessageId(d.rule, s->getString("mid")), {}};
        append(result.message, s->getString("desc"));
        if (const JsonValue *fix = s->get("fix")) {
          result.hasFix = true;
          result.fixStart = fix->getUint("a");
          result.fixEnd = fix->getUint("b");
          append(result.fixText, fix->getString("t"));
        }
        d.suggestions.append(std::move(result));
      }
    }
    if (const JsonValue *fix = item->get("fix")) {
      d.hasFix = true;
      d.fixStart = fix->getUint("a");
      d.fixEnd = fix->getUint("b");
      append(d.fixText, fix->getString("t"));
    }
    out.diagnostics.append(std::move(d));
  }
  return true;
}

} // namespace fastlint::lint
