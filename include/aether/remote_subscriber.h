#pragma once

#include "aether/wire.h"

#include <cstdint>

namespace aether {

struct RemoteSubscriber {
    int fd;  // TCP socket, -1 if not connected
};

// Connect and subscribe to a topic in one step.
RemoteSubscriber remote_subscriber(const char* host, const char* topic, uint32_t topic_len,
                                   uint16_t port = DEFAULT_TCP_PORT);
void remote_disconnect(RemoteSubscriber& sub);

// Blocks up to timeout_ms milliseconds (default: 5000).
// Returns the number of bytes written to buf, or -1 on disconnect/timeout.
int remote_consume(RemoteSubscriber& sub, void* buf, uint32_t buf_capacity,
                   int timeout_ms = 5000);

} // namespace aether
