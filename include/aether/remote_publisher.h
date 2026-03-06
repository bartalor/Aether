#pragma once

#include "aether/wire.h"

#include <cstdint>

namespace aether {

struct RemotePublisher {
    int fd;  // TCP socket, -1 if not connected
};

RemotePublisher remote_publisher(const char* host, uint16_t port = DEFAULT_TCP_PORT);
void remote_disconnect(RemotePublisher& pub);
bool remote_publish(RemotePublisher& pub,
                    const char* topic, uint32_t topic_len,
                    const void* data, uint32_t data_len);

} // namespace aether
