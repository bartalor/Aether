#pragma once

#include "aether/wire.h"

#include <cstdint>

namespace aether {

// Handle for a TCP connection to a remote daemon.
struct RemoteConnection {
    int fd;  // TCP socket, -1 if not connected
};

// Connect to a remote aetherd over TCP.
// Returns a RemoteConnection on success. Aborts on failure (fail fast).
RemoteConnection remote_connect(const char* host, uint16_t port = DEFAULT_TCP_PORT);

// Disconnect from the remote daemon.
void remote_disconnect(RemoteConnection& conn);

// Subscribe to a topic on the remote daemon.
// After this call, use remote_consume() to receive messages.
void remote_subscribe(RemoteConnection& conn, const char* topic, uint32_t topic_len);

// Publish a message to a topic on the remote daemon.
bool remote_publish(RemoteConnection& conn,
                    const char* topic, uint32_t topic_len,
                    const void* data, uint32_t data_len);

// Receive the next message from a subscribed remote connection.
// Blocks up to timeout_ms milliseconds (default: 5000).
// Returns the number of bytes written to buf, or -1 on disconnect/timeout.
int remote_consume(RemoteConnection& conn, void* buf, uint32_t buf_capacity,
                   int timeout_ms = 5000);

} // namespace aether
