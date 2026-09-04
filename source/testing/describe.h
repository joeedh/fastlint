#pragma once

#include "util/span.h"
#include "util/string.h"
#include "util/vector.h"

#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

/**
 * `describe()` renders a value for assertion and snapshot output. It is a
 * customization point: overload it for your own type in your own namespace and
 * ADL will find it. There is deliberately no generic catch-all overload, so a
 * type without one is reported as unprintable rather than silently rendering
 * as an address.
 */
namespace fastlint::test {

using litestl::util::span;
using litestl::util::string;
using litestl::util::Vector;

string describe(bool value);
string describe(char value);
string describe(signed char value);
string describe(unsigned char value);
string describe(short value);
string describe(unsigned short value);
string describe(int value);
string describe(unsigned int value);
string describe(long value);
string describe(unsigned long value);
string describe(long long value);
string describe(unsigned long long value);
string describe(float value);
string describe(double value);
string describe(std::nullptr_t);
string describe(const char *value);
string describe(std::string_view value);
string describe(const std::string &value);
string describe(const string &value);

/**
 * Container overloads are declared the HasDescribe concept so unqualified lookup
 * inside it finds them; ADL cannot, because the containers live in
 * litestl::util rather than here.
 */
template <typename T, int Size> string describe(const Vector<T, Size> &values);
template <typename T, size_t Extent> string describe(const span<T, Extent> &values);

/** Quotes and escapes a string the way describe() renders one. */
string quote(std::string_view value);

namespace detail {
template <typename T>
concept HasDescribe = requires(const T &value) {
  { describe(value) } -> std::convertible_to<string>;
};
} // namespace detail

/**
 * Renders any value: through `describe()` when one is visible, otherwise as an
 * enum's underlying value or a placeholder naming nothing we can read.
 */
template <typename T> string render(const T &value)
{
  if constexpr (detail::HasDescribe<T>) {
    return describe(value);
  } else if constexpr (std::is_enum_v<T>) {
    return describe(static_cast<std::underlying_type_t<T>>(value));
  } else if constexpr (std::is_pointer_v<T>) {
    return value ? string("<pointer>") : string("nullptr");
  } else {
    return string("<no describe() overload>");
  }
}

namespace detail {
template <typename Range> string describeRange(const Range &values)
{
  string out = "[";
  bool first = true;
  for (const auto &value : values) {
    if (!first) {
      out += ", ";
    }
    first = false;
    out += render(value);
  }
  out += "]";
  return out;
}
} // namespace detail

template <typename T, int Size> string describe(const Vector<T, Size> &values)
{
  return detail::describeRange(values);
}

template <typename T, size_t Extent> string describe(const span<T, Extent> &values)
{
  return detail::describeRange(values);
}

} // namespace fastlint::test
