#include "fastlint/lint/registry.h"
#include "fastlint/lint/result_cache.h"
#include "fastlint/rules/rules.h"
#include "testing/test.h"

#include <string>

using namespace fastlint;
using namespace fastlint::lint;

namespace {

std::string sv(const litestl::util::string &s)
{
  return std::string(s.c_str(), s.size());
}

} // namespace

TEST(lint_result_cache, round_trips_a_result)
{
  Registry registry;
  registry.add(rules::kNoDebugger);

  FileResult original;
  original.errorCount = 1;
  original.warningCount = 1;
  original.fixableErrorCount = 1;

  Diagnostic a;
  a.rule = &rules::kNoDebugger;
  a.severity = Severity::Error;
  a.fixable = true;
  a.start = 0;
  a.end = 9;
  a.line = 1;
  a.column = 1;
  a.endLine = 1;
  a.endColumn = 10;
  a.messageId = "unexpected";
  a.message = litestl::util::string();
  for (char c : std::string("Unexpected 'debugger' statement.")) {
    a.message += c;
  }
  a.hasFix = true;
  a.fixStart = 0;
  a.fixEnd = 10;
  for (char c : std::string("")) {
    a.fixText += c;
  }
  original.diagnostics.append(std::move(a));

  Diagnostic b;
  b.severity = Severity::Warn;
  b.fatal = false;
  b.start = 12;
  b.end = 15;
  b.line = 2;
  b.column = 1;
  b.endLine = 2;
  b.endColumn = 4;
  for (char c : std::string("a syntax note")) {
    b.message += c;
  }
  original.diagnostics.append(std::move(b));

  string payload;
  serializeResult(original, payload);

  FileResult restored;
  REQUIRE(deserializeResult(sv(payload), registry, restored));
  CHECK_EQ(restored.errorCount, 1);
  CHECK_EQ(restored.warningCount, 1);
  CHECK_EQ(restored.fixableErrorCount, 1);
  REQUIRE_EQ(int(restored.diagnostics.size()), 2);

  const Diagnostic &ra = restored.diagnostics[0];
  CHECK(ra.rule == &rules::kNoDebugger);
  CHECK(ra.severity == Severity::Error);
  CHECK(ra.fixable);
  CHECK_EQ(int(ra.endColumn), 10);
  // The message id resolves to the rule's own static string.
  CHECK(ra.messageId != nullptr);
  CHECK_EQ(std::string(ra.messageId ? ra.messageId : ""), "unexpected");
  CHECK_EQ(sv(ra.message), "Unexpected 'debugger' statement.");
  CHECK(ra.hasFix);
  CHECK_EQ(int(ra.fixStart), 0);
  CHECK_EQ(int(ra.fixEnd), 10);

  const Diagnostic &rb = restored.diagnostics[1];
  CHECK(rb.rule == nullptr);
  CHECK(rb.severity == Severity::Warn);
  CHECK(!rb.hasFix);
  CHECK_EQ(sv(rb.message), "a syntax note");
}

TEST(lint_result_cache, rejects_a_malformed_payload)
{
  Registry registry;
  FileResult out;
  CHECK(!deserializeResult("not json", registry, out));
  CHECK(!deserializeResult("[1,2,3]", registry, out));
}
