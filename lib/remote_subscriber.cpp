#include "aether/remote_subscriber.h"

#include <poll.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace aether {

RemoteSubscriber remote_subscriber(const char* host, const char* topic, uint32_t topic_len,
                                   uint16_t port) {
    int fd = tcp_connect(host, port);

    if (!send_msg(fd, MsgType::Subscribe, topic, topic_len)) {
        fprintf(stderr, "remote_subscriber: failed to send subscribe\n");
        std::abort();
    }

    return RemoteSubscriber{fd};
}

void remote_disconnect(RemoteSubscriber& sub) {
    if (sub.fd >= 0) {
        close(sub.fd);
        sub.fd = -1;
    }
}

int remote_consume(RemoteSubscriber& sub, void* buf, uint32_t buf_capacity,
                   int timeout_ms) {
    assert(sub.fd >= 0);

    pollfd pfd{};
    pfd.fd     = sub.fd;
    pfd.events = POLLIN;
    int ready = poll(&pfd, 1, timeout_ms);
    if (ready <= 0)
        return -1;

    WireHeader whdr{};
    if (!read_exact(sub.fd, &whdr, sizeof(whdr)))
        return -1;

    if (whdr.msg_type != MsgType::Message)
        return -1;

    if (whdr.body_len > buf_capacity)
        return -1;

    if (!read_exact(sub.fd, buf, whdr.body_len))
        return -1;

    return static_cast<int>(whdr.body_len);
}

} // namespace aether
