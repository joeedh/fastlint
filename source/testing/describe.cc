#include "testing/describe.h"

#include <cinttypes>
#include <cstdio>

namespace fastlint::test {

namespace {

template <typename T> string formatted(const char *spec, T value)
{
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), spec, value);
  return string(buffer);
}

} // namespace

string quote(std::string_view value)
{
  string out("\"");
  for (const char c : value) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        out += formatted("\\x%02x", static_cast<unsigned>(static_cast<unsigned char>(c)));
      } else {
        out += c;
      }
    }
  }
  out += "\"";
  return out;
}

string describe(bool value)
{
  return string(value ? "true" : "false");
}

string describe(char value)
{
  return quote(std::string_view(&value, 1));
}

string describe(signed char value)
{
  return formatted("%d", int(value));
}

string describe(unsigned char value)
{
  return formatted("%u", unsigned(value));
}

string describe(short value)
{
  return formatted("%d", int(value));
}

string describe(unsigned short value)
{
  return formatted("%u", unsigned(value));
}

string describe(int value)
{
  return formatted("%d", value);
}

string describe(unsigned int value)
{
  return formatted("%u", value);
}

string describe(long value)
{
  return formatted("%ld", value);
}

string describe(unsigned long value)
{
  return formatted("%lu", value);
}

string describe(long long value)
{
  return formatted("%lld", value);
}

string describe(unsigned long long value)
{
  return formatted("%llu", value);
}

string describe(float value)
{
  return formatted("%g", double(value));
}

string describe(double value)
{
  return formatted("%g", value);
}

string describe(std::nullptr_t)
{
  return string("nullptr");
}

string describe(const char *value)
{
  if (!value) {
    return string("nullptr");
  }
  return quote(std::string_view(value));
}

string describe(std::string_view value)
{
  return quote(value);
}

string describe(const std::string &value)
{
  return quote(std::string_view(value));
}

string describe(const string &value)
{
  return quote(std::string_view(value.c_str(), value.size()));
}

} // namespace fastlint::test
