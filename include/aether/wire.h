#pragma once

#include <cstdint>

namespace aether {

// ---------------------------------------------------------------------------
// Wire protocol — message framing for TCP transport
//
// Every message on the wire:
//   [ msg_type: 1 byte ][ body_len: 4 bytes (LE) ][ body: body_len bytes ]
//
// All multi-byte integers are little-endian.
// ---------------------------------------------------------------------------

constexpr uint16_t DEFAULT_TCP_PORT = 9090;

enum class MsgType : uint8_t {
    Subscribe = 1,  // client → daemon: body = topic name
    Publish   = 2,  // client → daemon: body = topic_len(4) + topic + payload
    Message   = 3,  // daemon → client: body = payload
};

struct WireHeader {
    MsgType  msg_type;
    uint32_t body_len;
} __attribute__((packed));

static_assert(sizeof(WireHeader) == 5, "WireHeader must be exactly 5 bytes");

} // namespace aether
