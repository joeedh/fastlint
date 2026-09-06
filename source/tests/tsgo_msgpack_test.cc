#include "fastlint/tsgo/msgpack.h"
#include "testing/test.h"

#include <string>

using namespace fastlint;
using namespace fastlint::tsgo;

namespace {

std::string bytesToString(const Vector<uint8_t, 4> &bytes)
{
  return std::string(reinterpret_cast<const char *>(bytesOf(bytes).data()), bytes.size());
}

span<const uint8_t> view(const Vector<uint8_t, 4> &bytes, size_t count)
{
  return bytesOf(bytes).subspan(0, count);
}

} // namespace

TEST(tsgo_msgpack, encodes_the_three_tuple)
{
  Vector<uint8_t, 4> out;
  encodeFrame(out, MessageType::Request, "initialize", "null");
  std::string expected("\x93\x01\xC4\x0Ainitialize\xC4\x04null", 2 + 12 + 6);
  CHECK_EQ(bytesToString(out), expected);
}

TEST(tsgo_msgpack, decodes_whole_and_partial_frames)
{
  Vector<uint8_t, 4> bytes;
  encodeFrame(
      bytes, MessageType::Response, "initialize", R"({"currentDirectory":"c:/x"})");
  encodeFrame(bytes, MessageType::Error, "getTypeAtPosition", "boom");

  Frame frame;
  size_t consumed = 0;
  for (size_t n = 0; n < 20; n++) {
    INFO("prefix length %zu", n);
    CHECK(decodeFrame(view(bytes, n), frame, consumed) == DecodeStatus::Incomplete);
  }
  CHECK(decodeFrame(view(bytes, bytes.size()), frame, consumed) ==
        DecodeStatus::Complete);
  CHECK(frame.type == MessageType::Response);
  CHECK_EQ(std::string(frame.method.c_str()), std::string("initialize"));
  CHECK_EQ(std::string(frame.payloadText()),
           std::string(R"({"currentDirectory":"c:/x"})"));
  CHECK_EQ(consumed, size_t(2 + 12 + 2 + 27));

  Vector<uint8_t, 4> rest = bytes.slice(int(consumed));
  CHECK(decodeFrame(view(rest, rest.size()), frame, consumed) == DecodeStatus::Complete);
  CHECK(frame.type == MessageType::Error);
  CHECK_EQ(std::string(frame.method.c_str()), std::string("getTypeAtPosition"));
  CHECK_EQ(std::string(frame.payloadText()), std::string("boom"));
  CHECK_EQ(consumed, rest.size());
}

TEST(tsgo_msgpack, wide_payloads_use_bin16_and_bin32)
{
  string medium;
  for (int i = 0; i < 300; i++) {
    medium += char('a' + i % 26);
  }
  string large;
  for (int i = 0; i < 70000; i++) {
    large += char('0' + i % 10);
  }
  Vector<uint8_t, 4> bytes;
  encodeFrame(bytes,
              MessageType::Request,
              "echo",
              std::string_view(medium.c_str(), medium.size()));
  CHECK_EQ(int(bytes[8]), 0xC5);
  CHECK_EQ(int(bytes[9]), 300 >> 8);
  CHECK_EQ(int(bytes[10]), 300 & 0xFF);
  Frame frame;
  size_t consumed = 0;
  CHECK(decodeFrame(view(bytes, bytes.size()), frame, consumed) ==
        DecodeStatus::Complete);
  CHECK_EQ(frame.payload.size(), size_t(300));
  CHECK_EQ(std::string(frame.payloadText()), std::string(medium.c_str()));

  bytes.clear();
  encodeFrame(bytes,
              MessageType::Call,
              "readFile",
              std::string_view(large.c_str(), large.size()));
  CHECK_EQ(int(bytes[12]), 0xC6);
  CHECK(decodeFrame(view(bytes, bytes.size() - 1), frame, consumed) ==
        DecodeStatus::Incomplete);
  CHECK(decodeFrame(view(bytes, bytes.size()), frame, consumed) ==
        DecodeStatus::Complete);
  CHECK(frame.type == MessageType::Call);
  CHECK_EQ(frame.payload.size(), size_t(70000));
  CHECK_EQ(consumed, bytes.size());
}

TEST(tsgo_msgpack, accepts_uint8_message_types_and_rejects_garbage)
{
  uint8_t framed[] = {0x93, 0xCC, 0x04, 0xC4, 0x01, 'x', 0xC4, 0x00};
  Frame frame;
  size_t consumed = 0;
  CHECK(decodeFrame(span<const uint8_t>(framed, sizeof framed), frame, consumed) ==
        DecodeStatus::Complete);
  CHECK(frame.type == MessageType::Response);
  CHECK_EQ(consumed, sizeof framed);
  CHECK_EQ(frame.payload.size(), size_t(0));

  uint8_t notArray[] = {0x92, 0x01};
  string error;
  CHECK(decodeFrame(
            span<const uint8_t>(notArray, sizeof notArray), frame, consumed, &error) ==
        DecodeStatus::Malformed);
  CHECK(error.starts_with(string("expected msgpack fixarray-3")));

  uint8_t badType[] = {0x93, 0x09, 0xC4, 0x00, 0xC4, 0x00};
  error = string();
  CHECK(decodeFrame(
            span<const uint8_t>(badType, sizeof badType), frame, consumed, &error) ==
        DecodeStatus::Malformed);
  CHECK(error.starts_with(string("unknown message type")));

  uint8_t badBin[] = {0x93, 0x01, 0xA1, 'x'};
  CHECK(
      decodeFrame(span<const uint8_t>(badBin, sizeof badBin), frame, consumed, &error) ==
      DecodeStatus::Malformed);
  CHECK(error.starts_with(string("expected msgpack bin8/16/32")));
}
