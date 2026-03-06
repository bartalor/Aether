#include "aether/remote.h"
#include "aether/control.h"
#include "aether/ring.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <poll.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace aether {

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

RemoteConnection remote_connect(const char* host, uint16_t port) {
    assert(host != nullptr);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("remote_connect: socket");
        std::abort();
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "remote_connect: invalid address: %s\n", host);
        std::abort();
    }

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("remote_connect: connect");
        std::abort();
    }

    return RemoteConnection{fd};
}

void remote_disconnect(RemoteConnection& conn) {
    if (conn.fd >= 0) {
        close(conn.fd);
        conn.fd = -1;
    }
}

void remote_subscribe(RemoteConnection& conn, const char* topic, uint32_t topic_len) {
    assert(conn.fd >= 0);
    if (!send_msg(conn.fd, MsgType::Subscribe, topic, topic_len)) {
        fprintf(stderr, "remote_subscribe: failed to send\n");
        std::abort();
    }
}

bool remote_publish(RemoteConnection& conn,
                    const char* topic, uint32_t topic_len,
                    const void* data, uint32_t data_len) {
    assert(conn.fd >= 0);

    // body = topic_len(4) + topic + payload
    const uint32_t body_len = 4 + topic_len + data_len;
    uint8_t body[4 + MAX_TOPIC_LEN + SLOT_DATA_SIZE];
    if (body_len > sizeof(body)) return false;

    std::memcpy(body, &topic_len, 4);
    std::memcpy(body + 4, topic, topic_len);
    std::memcpy(body + 4 + topic_len, data, data_len);

    return send_msg(conn.fd, MsgType::Publish, body, body_len);
}

int remote_consume(RemoteConnection& conn, void* buf, uint32_t buf_capacity,
                   int timeout_ms) {
    assert(conn.fd >= 0);

    // Wait for data with timeout so we don't block forever
    pollfd pfd{};
    pfd.fd     = conn.fd;
    pfd.events = POLLIN;
    int ready = poll(&pfd, 1, timeout_ms);
    if (ready <= 0)
        return -1; // timeout or error

    WireHeader whdr{};
    if (!read_exact(conn.fd, &whdr, sizeof(whdr)))
        return -1; // disconnected

    if (whdr.msg_type != MsgType::Message)
        return -1; // unexpected message type

    if (whdr.body_len > buf_capacity)
        return -1; // message too large for buffer

    if (!read_exact(conn.fd, buf, whdr.body_len))
        return -1;

    return static_cast<int>(whdr.body_len);
}

} // namespace aether
