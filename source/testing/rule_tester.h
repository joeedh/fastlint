#pragma once

// RuleTester-shaped harness for lint rules (docs/rules.md "Testing a rule"):
// `valid` snippets must produce nothing; `invalid` snippets list the expected
// reports and, for fixable rules, the text after `--fix`.

#include "fastlint/lint/rule.h"
#include "util/span.h"

#include <initializer_list>

namespace fastlint::test {

struct ExpectedError {
  const char *messageId;
  /** One-based; zero means unchecked. */
  int line = 0;
  int column = 0;
  int endLine = 0;
  int endColumn = 0;
  /** The interpolated text; null means unchecked. */
  const char *message = nullptr;
};

struct ValidCase {
  const char *code;
  /** A JSON array of the rule's options, or null. */
  const char *options = nullptr;
  const char *filename = nullptr;
};

struct InvalidCase {
  const char *code;
  std::initializer_list<ExpectedError> errors;
  /** The fixed text; null means no fix is expected to change the code. */
  const char *output = nullptr;
  const char *options = nullptr;
  const char *filename = nullptr;
};

/**
 * Runs `rule` alone at severity error over every case, one `SUBCASE` per case,
 * checking each expected report in source order and the `--fix` output.
 */
void runRuleTests(const lint::RuleDef &rule,
                  std::initializer_list<ValidCase> valid,
                  std::initializer_list<InvalidCase> invalid);

/**
 * Like `runRuleTests` with a type server over tests/fixtures/projects/<project>
 * (`basic` by default; `loose` turns `noImplicitThis` off); each case is typed as
 * `src/<filename>` of that project, `src/case.ts` by default. Cases run in one plain
 * loop, since replaying subcases would restart the server per case. Skips the test
 * when no native `tsc` is found; call from a test tagged `integration`.
 */
void runTypedRuleTests(const lint::RuleDef &rule,
                       std::initializer_list<ValidCase> valid,
                       std::initializer_list<InvalidCase> invalid,
                       const char *project = "basic");

} // namespace fastlint::test
