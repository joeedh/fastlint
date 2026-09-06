#pragma once

#include "util/span.h"
#include "util/string.h"
#include "util/vector.h"

#include <cstdint>
#include <string_view>

namespace fastlint::tsgo {

using litestl::util::span;
using litestl::util::Vector;
using string = litestl::util::string;

/** Message types of the `tsc --api` msgpack envelope
 * (tsc/internal/api/protocol_msgpack.go). */
enum class MessageType : uint8_t {
  Request = 1,
  CallResponse = 2,
  CallError = 3,
  Response = 4,
  Error = 5,
  Call = 6,
};

/** Read-only view of a byte vector. */
inline span<const uint8_t> bytesOf(const Vector<uint8_t, 4> &bytes)
{
  return span<const uint8_t>(const_cast<Vector<uint8_t, 4> &>(bytes).data(),
                             bytes.size());
}

/** One decoded envelope: `[type, method, payload]`. The payload is JSON text except for
 * `getSourceFile` and `echo`, which answer with raw bytes. */
struct Frame {
  MessageType type = MessageType::Request;
  string method;
  Vector<uint8_t, 4> payload;

  std::string_view payloadText() const
  {
    return std::string_view(reinterpret_cast<const char *>(bytesOf(payload).data()),
                            payload.size());
  }
};

enum class DecodeStatus { Complete, Incomplete, Malformed };

/** Appends the envelope for `type`/`method`/`payload` to `out`. */
void encodeFrame(Vector<uint8_t, 4> &out,
                 MessageType type,
                 std::string_view method,
                 span<const uint8_t> payload);

inline void encodeFrame(Vector<uint8_t, 4> &out,
                        MessageType type,
                        std::string_view method,
                        std::string_view payload)
{
  encodeFrame(out,
              type,
              method,
              span<const uint8_t>(reinterpret_cast<const uint8_t *>(payload.data()),
                                  payload.size()));
}

/** Decodes the frame at the front of `bytes`. `consumed` is the frame's length when the
 * result is Complete; `error` names the problem when it is Malformed. */
DecodeStatus decodeFrame(span<const uint8_t> bytes,
                         Frame &frame,
                         size_t &consumed,
                         string *error = nullptr);

} // namespace fastlint::tsgo
