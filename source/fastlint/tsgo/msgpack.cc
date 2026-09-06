#include "fastlint/tsgo/msgpack.h"

#include <cstdio>

namespace fastlint::tsgo {

namespace {

constexpr uint8_t kFixArray3 = 0x93;
constexpr uint8_t kUint8 = 0xCC;
constexpr uint8_t kBin8 = 0xC4;
constexpr uint8_t kBin16 = 0xC5;
constexpr uint8_t kBin32 = 0xC6;

void appendBin(Vector<uint8_t, 4> &out, span<const uint8_t> data)
{
  size_t n = data.size();
  if (n < 256) {
    out.append(kBin8);
    out.append(uint8_t(n));
  } else if (n < 65536) {
    out.append(kBin16);
    out.append(uint8_t(n >> 8));
    out.append(uint8_t(n));
  } else {
    out.append(kBin32);
    out.append(uint8_t(n >> 24));
    out.append(uint8_t(n >> 16));
    out.append(uint8_t(n >> 8));
    out.append(uint8_t(n));
  }
  for (uint8_t b : data) {
    out.append(b);
  }
}

void setError(string *error, const char *what, uint8_t byte)
{
  if (!error) {
    return;
  }
  char buf[96];
  snprintf(buf, sizeof buf, "%s, got 0x%02x", what, byte);
  *error = string(buf);
}

/** Reads one msgpack bin at `at`; on Complete `start`/`size` locate the bytes and `at`
 * moves past them. */
DecodeStatus
readBin(span<const uint8_t> b, size_t &at, size_t &start, size_t &size, string *error)
{
  if (b.size() < at + 1) {
    return DecodeStatus::Incomplete;
  }
  uint8_t marker = b[at];
  size_t headerLen;
  if (marker == kBin8) {
    headerLen = 2;
  } else if (marker == kBin16) {
    headerLen = 3;
  } else if (marker == kBin32) {
    headerLen = 5;
  } else {
    setError(error, "expected msgpack bin8/16/32", marker);
    return DecodeStatus::Malformed;
  }
  if (b.size() < at + headerLen) {
    return DecodeStatus::Incomplete;
  }
  size = 0;
  for (size_t i = 1; i < headerLen; i++) {
    size = (size << 8) | b[at + i];
  }
  start = at + headerLen;
  if (b.size() < start + size) {
    return DecodeStatus::Incomplete;
  }
  at = start + size;
  return DecodeStatus::Complete;
}

} // namespace

void encodeFrame(Vector<uint8_t, 4> &out,
                 MessageType type,
                 std::string_view method,
                 span<const uint8_t> payload)
{
  out.append(kFixArray3);
  uint8_t t = uint8_t(type);
  if (t > 0x7F) {
    out.append(kUint8);
  }
  out.append(t);
  appendBin(out,
            span<const uint8_t>(reinterpret_cast<const uint8_t *>(method.data()),
                                method.size()));
  appendBin(out, payload);
}

DecodeStatus
decodeFrame(span<const uint8_t> bytes, Frame &frame, size_t &consumed, string *error)
{
  if (bytes.size() < 2) {
    return DecodeStatus::Incomplete;
  }
  if (bytes[0] != kFixArray3) {
    setError(error, "expected msgpack fixarray-3 (0x93)", bytes[0]);
    return DecodeStatus::Malformed;
  }
  size_t at = 1;
  uint8_t typeByte = bytes[at];
  uint8_t type;
  if (typeByte <= 0x7F) {
    type = typeByte;
    at += 1;
  } else if (typeByte == kUint8) {
    if (bytes.size() < at + 2) {
      return DecodeStatus::Incomplete;
    }
    type = bytes[at + 1];
    at += 2;
  } else {
    setError(error, "expected fixint or uint8 message type", typeByte);
    return DecodeStatus::Malformed;
  }
  if (type < uint8_t(MessageType::Request) || type > uint8_t(MessageType::Call)) {
    setError(error, "unknown message type", type);
    return DecodeStatus::Malformed;
  }
  size_t methodStart, methodSize;
  DecodeStatus s = readBin(bytes, at, methodStart, methodSize, error);
  if (s != DecodeStatus::Complete) {
    return s;
  }
  size_t payloadStart, payloadSize;
  s = readBin(bytes, at, payloadStart, payloadSize, error);
  if (s != DecodeStatus::Complete) {
    return s;
  }
  frame.type = MessageType(type);
  frame.method = string();
  for (size_t i = 0; i < methodSize; i++) {
    frame.method += char(bytes[methodStart + i]);
  }
  frame.payload.clear();
  frame.payload.ensure_capacity(payloadSize);
  for (size_t i = 0; i < payloadSize; i++) {
    frame.payload.append(bytes[payloadStart + i]);
  }
  consumed = at;
  return DecodeStatus::Complete;
}

} // namespace fastlint::tsgo
