#include "fastlint/lint/format.h"

#include "fastlint/tsgo/json.h"

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
      if (!d.suggestions.isEmpty()) {
        w.key("suggestions");
        w.beginArray();
        for (const SuggestionResult &s : d.suggestions) {
          w.beginObject();
          w.member("messageId", s.messageId);
          w.member("desc", view(s.message));
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

} // namespace fastlint::lint
