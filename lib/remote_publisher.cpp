#include "aether/remote_publisher.h"
#include "aether/control.h"
#include "aether/ring.h"

#include <cassert>
#include <cstring>

namespace aether {

RemotePublisher remote_publisher(const char* host, uint16_t port) {
    return RemotePublisher{tcp_connect(host, port)};
}

void remote_disconnect(RemotePublisher& pub) {
    if (pub.fd >= 0) {
        close(pub.fd);
        pub.fd = -1;
    }
}

bool remote_publish(RemotePublisher& pub,
                    const char* topic, uint32_t topic_len,
                    const void* data, uint32_t data_len) {
    assert(pub.fd >= 0);

    const uint32_t body_len = 4 + topic_len + data_len;
    uint8_t body[4 + MAX_TOPIC_LEN + SLOT_DATA_SIZE];
    if (body_len > sizeof(body)) return false;

    std::memcpy(body, &topic_len, 4);
    std::memcpy(body + 4, topic, topic_len);
    std::memcpy(body + 4 + topic_len, data, data_len);

    return send_msg(pub.fd, MsgType::Publish, body, body_len);
}

} // namespace aether
