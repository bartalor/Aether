#pragma once

#include <cstdint>
#include <cstddef>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>

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

// ---------------------------------------------------------------------------
// IO helpers — read/write exactly `len` bytes (handles short reads/writes)
// ---------------------------------------------------------------------------

inline bool read_exact(int fd, void* buf, size_t len) {
    auto* p = static_cast<uint8_t*>(buf);
    while (len > 0) {
        ssize_t n = read(fd, p, len);
        if (n <= 0) return false;
        p   += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

inline bool write_exact(int fd, const void* buf, size_t len) {
    auto* p = static_cast<const uint8_t*>(buf);
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n <= 0) return false;
        p   += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

inline bool send_msg(int fd, MsgType type, const void* body, uint32_t body_len) {
    WireHeader hdr{};
    hdr.msg_type = type;
    hdr.body_len = body_len;
    if (!write_exact(fd, &hdr, sizeof(hdr))) return false;
    if (body_len > 0 && !write_exact(fd, body, body_len)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// TCP connect helper — aborts on failure (fail fast)
// ---------------------------------------------------------------------------

inline int tcp_connect(const char* host, uint16_t port = DEFAULT_TCP_PORT) {
    assert(host != nullptr);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("tcp_connect: socket");
        std::abort();
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "tcp_connect: invalid address: %s\n", host);
        std::abort();
    }

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("tcp_connect: connect");
        std::abort();
    }

    return fd;
}

} // namespace aether
