#pragma once

#include "testing/describe.h"
#include "util/string.h"
#include "util/vector.h"

#include <format>
#include <string>
#include <utility>

namespace fastlint::test {

using litestl::util::string;
using litestl::util::Vector;

/** One registered test. Registration happens at static-init time. */
struct TestCase {
  const char *suite = nullptr;
  const char *name = nullptr;
  const char *file = nullptr;
  int line = 0;
  const char *const *tags = nullptr;
  int tagCount = 0;
  void (*fn)() = nullptr;
};

void registerTest(const TestCase &test);

struct Registrar {
  explicit Registrar(const TestCase &test)
  {
    registerTest(test);
  }
};

// ------------------------------------------------------------ decomposition

/** The outcome of one assertion, with both operands already rendered. */
struct ExprResult {
  bool passed = false;
  string lhs;
  string op;
  string rhs;
};

// The decomposer deliberately compares whatever the test wrote, including a
// signed value against an unsigned one, and `Decomposer{} << a == b` relies on
// `<<` outranking the comparison. Both are diagnosed by clang.
#ifdef __clang__
#define FASTLINT_DIAGNOSTIC_PUSH                                                         \
  _Pragma("clang diagnostic push")                                                       \
      _Pragma("clang diagnostic ignored \"-Wsign-compare\"")                             \
          _Pragma("clang diagnostic ignored \"-Woverloaded-shift-op-parentheses\"")
#define FASTLINT_DIAGNOSTIC_POP _Pragma("clang diagnostic pop")
#else
#define FASTLINT_DIAGNOSTIC_PUSH
#define FASTLINT_DIAGNOSTIC_POP
#endif

FASTLINT_DIAGNOSTIC_PUSH

template <typename L> struct LhsProxy {
  const L &lhs;

#define FASTLINT_BINARY_OP(OP)                                                           \
  template <typename R> ExprResult operator OP(const R &rhs) const                       \
  {                                                                                      \
    return {bool(lhs OP rhs), render(lhs), #OP, render(rhs)};                            \
  }

  FASTLINT_BINARY_OP(==)
  FASTLINT_BINARY_OP(!=)
  FASTLINT_BINARY_OP(<)
  FASTLINT_BINARY_OP(<=)
  FASTLINT_BINARY_OP(>)
  FASTLINT_BINARY_OP(>=)

#undef FASTLINT_BINARY_OP

  ExprResult result() const
  {
    return {bool(lhs), render(lhs), "", ""};
  }
};

/**
 * Captures the left operand. `<<` binds tighter than every comparison, so
 * `Decomposer{} << a == b` parses as `(Decomposer{} << a) == b`. Wrap `&&` and
 * `||` expressions in an extra pair of parentheses; the decomposer cannot see
 * through them.
 */
struct Decomposer {
  template <typename L> LhsProxy<L> operator<<(const L &lhs) const
  {
    return LhsProxy<L>{lhs};
  }
};

FASTLINT_DIAGNOSTIC_POP

inline ExprResult toResult(const ExprResult &result)
{
  return result;
}

template <typename L> ExprResult toResult(const LhsProxy<L> &proxy)
{
  return proxy.result();
}

// ------------------------------------------------------------ reporting

/** Records an assertion. Returns false when the test body should return. */
bool reportAssert(const ExprResult &result,
                  const char *expression,
                  const char *file,
                  int line,
                  bool fatal);
void reportFailure(const string &message, const char *file, int line);
void markSkipped(const string &reason);

/** Pushes a line of context onto every failure recorded inside its scope. */
struct InfoScope {
  explicit InfoScope(const string &text);
  ~InfoScope();

  InfoScope(const InfoScope &) = delete;
  InfoScope &operator=(const InfoScope &) = delete;
};

template <typename... Args>
string formatText(std::format_string<Args...> fmt, Args &&...args)
{
  const std::string text = std::format(fmt, std::forward<Args>(args)...);
  return string(text.c_str());
}

inline string formatText(const char *text)
{
  return string(text);
}

// ------------------------------------------------------------ subcases

/**
 * One named sub-scope. The runner replays the test body until every subcase
 * has been entered exactly once, so sibling subcases never share state.
 */
class Subcase {
public:
  Subcase(const char *label, const char *file, int line);
  ~Subcase();

  Subcase(const Subcase &) = delete;
  Subcase &operator=(const Subcase &) = delete;

  explicit operator bool() const
  {
    return entered_;
  }

private:
  bool entered_ = false;
  int deferralsAtEntry_ = 0;
};

} // namespace fastlint::test

// ------------------------------------------------------------ macros

#define FASTLINT_CAT_(a, b) a##b
#define FASTLINT_CAT(a, b) FASTLINT_CAT_(a, b)
#define FASTLINT_UNIQUE(prefix) FASTLINT_CAT(prefix, __LINE__)

#define FASTLINT_TEST_IMPL(SUITE, NAME, ...)                                             \
  static void FASTLINT_UNIQUE(fastlintTestBody)();                                       \
  static const char *const FASTLINT_UNIQUE(fastlintTestTags)[] = {__VA_ARGS__};          \
  static const ::fastlint::test::Registrar FASTLINT_UNIQUE(fastlintTestRegistrar){       \
      ::fastlint::test::TestCase{                                                        \
          #SUITE,                                                                        \
          #NAME,                                                                         \
          __FILE__,                                                                      \
          __LINE__,                                                                      \
          FASTLINT_UNIQUE(fastlintTestTags),                                             \
          int(sizeof(FASTLINT_UNIQUE(fastlintTestTags)) / sizeof(const char *)),         \
          &FASTLINT_UNIQUE(fastlintTestBody)}};                                          \
  static void FASTLINT_UNIQUE(fastlintTestBody)()

/** Registers a test named `suite.name`, tagged `fast`. */
#define TEST(SUITE, NAME) FASTLINT_TEST_IMPL(SUITE, NAME, "fast")

/** Registers a test with explicit tags; `fast` is not implied. */
#define TEST_TAGGED(SUITE, NAME, ...) FASTLINT_TEST_IMPL(SUITE, NAME, __VA_ARGS__)

#define FASTLINT_ASSERT(EXPR, FATAL)                                                     \
  do {                                                                                   \
    FASTLINT_DIAGNOSTIC_PUSH                                                             \
    const ::fastlint::test::ExprResult fastlintResult =                                  \
        ::fastlint::test::toResult(::fastlint::test::Decomposer{} << EXPR);              \
    if (!::fastlint::test::reportAssert(                                                 \
            fastlintResult, #EXPR, __FILE__, __LINE__, FATAL))                           \
    {                                                                                    \
      return;                                                                            \
    }                                                                                    \
    FASTLINT_DIAGNOSTIC_POP                                                              \
  } while (false)

#define CHECK(EXPR) FASTLINT_ASSERT(EXPR, false)
#define REQUIRE(EXPR) FASTLINT_ASSERT(EXPR, true)

#define FASTLINT_ASSERT_BINARY(LHS, OP, RHS, FATAL)                                      \
  do {                                                                                   \
    FASTLINT_DIAGNOSTIC_PUSH                                                             \
    const auto &fastlintLhs = (LHS);                                                     \
    const auto &fastlintRhs = (RHS);                                                     \
    const ::fastlint::test::ExprResult fastlintResult{                                   \
        bool(fastlintLhs OP fastlintRhs),                                                \
        ::fastlint::test::render(fastlintLhs),                                           \
        #OP,                                                                             \
        ::fastlint::test::render(fastlintRhs)};                                          \
    if (!::fastlint::test::reportAssert(                                                 \
            fastlintResult, #LHS " " #OP " " #RHS, __FILE__, __LINE__, FATAL))           \
    {                                                                                    \
      return;                                                                            \
    }                                                                                    \
    FASTLINT_DIAGNOSTIC_POP                                                              \
  } while (false)

#define CHECK_EQ(LHS, RHS) FASTLINT_ASSERT_BINARY(LHS, ==, RHS, false)
#define CHECK_NE(LHS, RHS) FASTLINT_ASSERT_BINARY(LHS, !=, RHS, false)
#define CHECK_LT(LHS, RHS) FASTLINT_ASSERT_BINARY(LHS, <, RHS, false)
#define CHECK_LE(LHS, RHS) FASTLINT_ASSERT_BINARY(LHS, <=, RHS, false)
#define CHECK_GT(LHS, RHS) FASTLINT_ASSERT_BINARY(LHS, >, RHS, false)
#define CHECK_GE(LHS, RHS) FASTLINT_ASSERT_BINARY(LHS, >=, RHS, false)
#define REQUIRE_EQ(LHS, RHS) FASTLINT_ASSERT_BINARY(LHS, ==, RHS, true)
#define REQUIRE_NE(LHS, RHS) FASTLINT_ASSERT_BINARY(LHS, !=, RHS, true)

#define FAIL(...)                                                                        \
  do {                                                                                   \
    ::fastlint::test::reportFailure(                                                     \
        ::fastlint::test::formatText(__VA_ARGS__), __FILE__, __LINE__);                  \
    return;                                                                              \
  } while (false)

#define SKIP(...)                                                                        \
  do {                                                                                   \
    ::fastlint::test::markSkipped(::fastlint::test::formatText(__VA_ARGS__));            \
    return;                                                                              \
  } while (false)

#define INFO(...)                                                                        \
  const ::fastlint::test::InfoScope FASTLINT_UNIQUE(fastlintInfo)                        \
  {                                                                                      \
    ::fastlint::test::formatText(__VA_ARGS__)                                            \
  }

// The guard is named per line so a nested SUBCASE does not shadow its parent,
// which /W4 rejects.
#define SUBCASE(LABEL)                                                                   \
  if (::fastlint::test::Subcase FASTLINT_UNIQUE(fastlintSubcase){                        \
          LABEL, __FILE__, __LINE__};                                                    \
      FASTLINT_UNIQUE(fastlintSubcase))
