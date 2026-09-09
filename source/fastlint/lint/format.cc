#include "fastlint/lint/format.h"

#include "fastlint/tsgo/json.h"
#include "fastlint/version.h"
#include "util/vector.h"

#include <cstdio>
#include <string>

namespace fastlint::lint {

namespace {

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

void appendNumber(string &out, int value)
{
  char buffer[16];
  std::snprintf(buffer, sizeof buffer, "%d", value);
  append(out, buffer);
}

void pad(string &out, size_t width, size_t used)
{
  for (size_t i = used; i < width; i++) {
    out += ' ';
  }
}

struct Palette {
  const char *reset;
  const char *bold;
  const char *dim;
  const char *red;
  const char *yellow;
  const char *underline;
};

constexpr Palette kColors{
    "\x1b[0m", "\x1b[1m", "\x1b[2m", "\x1b[31m", "\x1b[33m", "\x1b[4m"};
constexpr Palette kPlain{"", "", "", "", "", ""};

void plural(string &out, int count, const char *word)
{
  appendNumber(out, count);
  out += ' ';
  append(out, word);
  if (count != 1) {
    out += 's';
  }
}

} // namespace

void formatPretty(span<const FileResult> results,
                  const FormatOptions &options,
                  string &out)
{
  const Palette &p = options.color ? kColors : kPlain;
  int errors = 0, warnings = 0, fixableErrors = 0, fixableWarnings = 0;
  for (const FileResult &result : results) {
    errors += result.errorCount;
    warnings += result.warningCount;
    fixableErrors += result.fixableErrorCount;
    fixableWarnings += result.fixableWarningCount;
    if (result.diagnostics.isEmpty()) {
      continue;
    }
    append(out, p.underline);
    append(out, view(result.filename));
    append(out, p.reset);
    out += '\n';

    // Column widths per file, as ESLint's stylish formatter aligns them.
    size_t positionWidth = 0, messageWidth = 0;
    Vector<string> positions;
    for (const Diagnostic &d : result.diagnostics) {
      string position;
      appendNumber(position, int(d.line));
      position += ':';
      appendNumber(position, int(d.column));
      positionWidth = position.size() > positionWidth ? position.size() : positionWidth;
      messageWidth = d.message.size() > messageWidth ? d.message.size() : messageWidth;
      positions.append(std::move(position));
    }
    for (int i = 0; i < int(result.diagnostics.size()); i++) {
      const Diagnostic &d = result.diagnostics[i];
      append(out, "  ");
      append(out, p.dim);
      append(out, view(positions[i]));
      append(out, p.reset);
      pad(out, positionWidth, positions[i].size());
      append(out, "  ");
      if (d.severity == Severity::Error) {
        append(out, p.red);
        append(out, "error  ");
      } else {
        append(out, p.yellow);
        append(out, "warning");
      }
      append(out, p.reset);
      append(out, "  ");
      append(out, view(d.message));
      string_view ruleId = d.ruleId();
      if (!ruleId.empty()) {
        pad(out, messageWidth, d.message.size());
        append(out, "  ");
        append(out, p.dim);
        append(out, ruleId);
        append(out, p.reset);
      }
      out += '\n';
    }
    out += '\n';
  }

  int problems = errors + warnings;
  if (problems == 0) {
    return;
  }
  append(out, errors > 0 ? p.red : p.yellow);
  append(out, p.bold);
  plural(out, problems, "problem");
  append(out, " (");
  plural(out, errors, "error");
  append(out, ", ");
  plural(out, warnings, "warning");
  append(out, ")");
  append(out, p.reset);
  out += '\n';
  if (fixableErrors + fixableWarnings > 0) {
    append(out, errors > 0 ? p.red : p.yellow);
    append(out, p.bold);
    append(out, "  ");
    plural(out, fixableErrors, "error");
    append(out, " and ");
    plural(out, fixableWarnings, "warning");
    append(out, " potentially fixable with the --fix option.");
    append(out, p.reset);
    out += '\n';
  }
}

void formatJson(span<const FileResult> results, string &out)
{
  tsgo::JsonWriter w;
  w.beginArray();
  for (const FileResult &result : results) {
    w.beginObject();
    w.member("filePath", view(result.filename));
    w.key("messages");
    w.beginArray();
    for (const Diagnostic &d : result.diagnostics) {
      w.beginObject();
      w.key("ruleId");
      if (d.rule) {
        w.value(d.rule->meta.name);
      } else {
        w.null();
      }
      w.member("severity", d.severity == Severity::Error ? 2 : 1);
      w.member("message", view(d.message));
      w.member("line", d.line);
      w.member("column", d.column);
      w.member("endLine", d.endLine);
      w.member("endColumn", d.endColumn);
      if (d.messageId) {
        w.member("messageId", d.messageId);
      }
      if (d.fatal) {
        w.member("fatal", true);
      }
      if (d.fixable) {
        w.member("fixable", true);
      }
      if (d.hasFix) {
        w.key("fix");
        w.beginObject();
        w.key("range");
        w.beginArray();
        w.value(d.fixStart);
        w.value(d.fixEnd);
        w.endArray();
        w.member("text", view(d.fixText));
        w.endObject();
      }
      if (!d.suggestions.isEmpty()) {
        w.key("suggestions");
        w.beginArray();
        for (const SuggestionResult &s : d.suggestions) {
          w.beginObject();
          w.member("messageId", s.messageId);
          w.member("desc", view(s.message));
          if (s.hasFix) {
            w.key("fix");
            w.beginObject();
            w.key("range");
            w.beginArray();
            w.value(s.fixStart);
            w.value(s.fixEnd);
            w.endArray();
            w.member("text", view(s.fixText));
            w.endObject();
          }
          w.endObject();
        }
        w.endArray();
      }
      w.endObject();
    }
    w.endArray();
    w.member("errorCount", result.errorCount);
    w.member("warningCount", result.warningCount);
    w.member("fixableErrorCount", result.fixableErrorCount);
    w.member("fixableWarningCount", result.fixableWarningCount);
    if (result.changed) {
      w.member("output", view(result.output));
    }
    w.endObject();
  }
  w.endArray();
  append(out, w.text());
  out += '\n';
}

const char *sarifLevel(Severity severity)
{
  switch (severity) {
  case Severity::Error:
    return "error";
  case Severity::Warn:
    return "warning";
  default:
    return "note";
  }
}

void formatSarif(span<const FileResult> results, string &out)
{
  // The driver's `rules` list every reported rule once, first-seen order; a
  // result points into it by index. A syntax error carries no rule.
  litestl::util::Vector<const RuleDef *> rules;
  auto ruleIndex = [&](const RuleDef *rule) -> int {
    for (int i = 0; i < int(rules.size()); i++) {
      if (rules[i] == rule) {
        return i;
      }
    }
    rules.append(rule);
    return int(rules.size()) - 1;
  };
  for (const FileResult &result : results) {
    for (const Diagnostic &d : result.diagnostics) {
      if (d.rule) {
        ruleIndex(d.rule);
      }
    }
  }

  tsgo::JsonWriter w;
  w.beginObject();
  w.member("version", "2.1.0");
  w.member("$schema", "https://json.schemastore.org/sarif-2.1.0.json");
  w.key("runs");
  w.beginArray();
  w.beginObject();
  w.key("tool");
  w.beginObject();
  w.key("driver");
  w.beginObject();
  w.member("name", "lintrix");
  w.member("informationUri", "https://github.com/joeedh/fastlint");
  w.member("version", version());
  w.key("rules");
  w.beginArray();
  for (const RuleDef *rule : rules) {
    w.beginObject();
    w.member("id", rule->meta.name);
    if (rule->meta.docsUrl && rule->meta.docsUrl[0]) {
      w.member("helpUri", rule->meta.docsUrl);
    }
    w.key("shortDescription");
    w.beginObject();
    w.member("text", rule->meta.description);
    w.endObject();
    w.endObject();
  }
  w.endArray();
  w.endObject(); // driver
  w.endObject(); // tool
  w.key("results");
  w.beginArray();
  for (const FileResult &result : results) {
    for (const Diagnostic &d : result.diagnostics) {
      w.beginObject();
      if (d.rule) {
        w.member("ruleId", d.rule->meta.name);
        w.member("ruleIndex", ruleIndex(d.rule));
      }
      w.member("level", sarifLevel(d.severity));
      w.key("message");
      w.beginObject();
      w.member("text", view(d.message));
      w.endObject();
      w.key("locations");
      w.beginArray();
      w.beginObject();
      w.key("physicalLocation");
      w.beginObject();
      w.key("artifactLocation");
      w.beginObject();
      w.member("uri", view(result.filename));
      w.endObject();
      w.key("region");
      w.beginObject();
      w.member("startLine", d.line);
      w.member("startColumn", d.column);
      w.member("endLine", d.endLine);
      w.member("endColumn", d.endColumn);
      w.endObject(); // region
      w.endObject(); // physicalLocation
      w.endObject(); // location
      w.endArray();  // locations
      w.endObject(); // result
    }
  }
  w.endArray();  // results
  w.endObject(); // run
  w.endArray();  // runs
  w.endObject(); // log
  append(out, w.text());
  out += '\n';
}

} // namespace fastlint::lint
