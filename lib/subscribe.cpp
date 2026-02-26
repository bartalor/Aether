#include "aether/subscribe.h"
#include "aether/shm.h"
#include "aether/control.h"

#include <sys/mman.h>    // munmap
#include <sys/socket.h>  // socket, connect, send, recv, close
#include <sys/un.h>      // sockaddr_un
#include <unistd.h>      // close
#include <cassert>
#include <cstring>

namespace aether {

Subscription subscribe(const char* topic, uint32_t topic_len) {
    assert(topic != nullptr);
    assert(topic_len > 0 && topic_len <= MAX_TOPIC_LEN);

    // --- 1. Connect to the daemon ---
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(sock != -1);

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, DAEMON_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    int rc = connect(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    assert(rc == 0);

    // --- 2. Send SubscribeRequest ---
    SubscribeRequest req{};
    req.topic_len = topic_len;
    std::memcpy(req.topic, topic, topic_len);

    ssize_t sent = send(sock, &req, sizeof(req), 0);
    assert(sent == static_cast<ssize_t>(sizeof(req)));

    // --- 3. Read SubscribeResponse ---
    SubscribeResponse resp{};
    ssize_t received = recv(sock, &resp, sizeof(resp), MSG_WAITALL);
    assert(received == static_cast<ssize_t>(sizeof(resp)));
    assert(resp.status == ControlStatus::Ok);

    close(sock);

    // --- 4. Map the shm segment ---
    RingHeader* hdr = shm_attach(resp.shm_name);
    assert(hdr != nullptr);

    // shm_segment_size() reconstructs the total mapping size from capacity.
    // We store it in the handle so unsubscribe() can call munmap() correctly.
    const size_t map_size = shm_segment_size(hdr->capacity);

    return Subscription{hdr, map_size};
}

void unsubscribe(Subscription& sub) {
    assert(sub.hdr != nullptr);
    munmap(sub.hdr, sub.map_size);
    sub.hdr     = nullptr;
    sub.map_size = 0;
}

} // namespace aether
