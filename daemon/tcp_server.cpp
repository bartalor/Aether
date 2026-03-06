#include "tcp_server.h"
#include "topic_registry.h"
#include "aether/wire.h"
#include "aether/ring.h"
#include "aether/publish.h"
#include "aether/consume.h"

#include <arpa/inet.h>    // htonl, ntohl
#include <netinet/in.h>   // sockaddr_in
#include <sys/socket.h>   // socket, bind, listen, accept, setsockopt

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <mutex>

using aether::read_exact;
using aether::write_exact;
using aether::send_msg;

static int                     g_listen_fd = -1;
static std::thread             g_server_thread;
static std::atomic<bool>       g_running{false};
static std::vector<std::thread> g_client_threads;
static std::mutex              g_clients_mutex;

// ---------------------------------------------------------------------------
// Handle a subscribed client: poll the ring and forward messages over TCP
// ---------------------------------------------------------------------------

static void forward_messages(int fd, aether::RingHeader* hdr) {
    uint64_t read_seq = hdr->write_seq.load(std::memory_order_relaxed);
    uint8_t buf[aether::SLOT_DATA_SIZE];
    uint32_t buf_len;

    while (g_running.load(std::memory_order_relaxed)) {
        buf_len = sizeof(buf);
        aether::ConsumeResult r = aether::consume(hdr, buf, buf_len, read_seq);

        if (r == aether::ConsumeResult::Ok) {
            if (!send_msg(fd, aether::MsgType::Message, buf, buf_len))
                return; // client disconnected
        } else if (r == aether::ConsumeResult::Lapped) {
            // skip ahead, try again immediately
        } else {
            usleep(100); // 100us — avoid busy spin
        }
    }
}

// ---------------------------------------------------------------------------
// Handle one client connection
// ---------------------------------------------------------------------------

static void handle_tcp_client(int fd) {
    aether::RingHeader* subscribed_ring = nullptr;

    while (g_running.load(std::memory_order_relaxed)) {
        // Read wire header
        aether::WireHeader whdr{};
        if (!read_exact(fd, &whdr, sizeof(whdr)))
            break; // client disconnected

        // Read body
        if (whdr.body_len > aether::SLOT_DATA_SIZE + aether::MAX_TOPIC_LEN + 4) {
            break; // sanity check — body too large
        }

        uint8_t body[aether::SLOT_DATA_SIZE + aether::MAX_TOPIC_LEN + 4];
        if (whdr.body_len > 0 && !read_exact(fd, body, whdr.body_len))
            break;

        switch (whdr.msg_type) {
        case aether::MsgType::Subscribe: {
            // body = topic name
            const TopicInfo* topic = get_or_create_topic(
                reinterpret_cast<const char*>(body), whdr.body_len);
            if (!topic) {
                close(fd);
                return;
            }
            subscribed_ring = topic->hdr;
            // Enter forwarding mode — blocks until disconnect or shutdown
            forward_messages(fd, subscribed_ring);
            close(fd);
            return;
        }

        case aether::MsgType::Publish: {
            // body = topic_len(4) + topic + payload
            if (whdr.body_len < 4) break;
            uint32_t topic_len;
            std::memcpy(&topic_len, body, 4);
            if (4 + topic_len > whdr.body_len) break;

            const char* topic_name = reinterpret_cast<const char*>(body + 4);
            const uint8_t* payload = body + 4 + topic_len;
            const uint32_t payload_len = whdr.body_len - 4 - topic_len;

            const TopicInfo* topic = get_or_create_topic(topic_name, topic_len);
            if (topic) {
                aether::publish(topic->hdr, payload, payload_len);
            }
            break;
        }

        case aether::MsgType::Message:
            // Clients shouldn't send this — ignore
            break;
        }
    }

    close(fd);
}

// ---------------------------------------------------------------------------
// Server loop — accept TCP connections
// ---------------------------------------------------------------------------

static void server_loop() {
    fprintf(stderr, "[aetherd] tcp server listening on port %u\n", aether::DEFAULT_TCP_PORT);

    while (g_running.load(std::memory_order_relaxed)) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(g_listen_fd,
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &addr_len);
        if (client_fd < 0) break; // listen fd closed by stop_tcp_server()

        fprintf(stderr, "[aetherd] tcp client connected: %s:%d\n",
                inet_ntoa(client_addr.sin_addr),
                ntohs(client_addr.sin_port));

        std::lock_guard<std::mutex> lock(g_clients_mutex);
        g_client_threads.emplace_back(handle_tcp_client, client_fd);
    }

    fprintf(stderr, "[aetherd] tcp server stopped\n");
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void start_tcp_server() {
    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        perror("tcp socket");
        std::abort();
    }

    int opt = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(aether::DEFAULT_TCP_PORT);

    if (bind(g_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("tcp bind");
        std::abort();
    }

    if (listen(g_listen_fd, 16) < 0) {
        perror("tcp listen");
        std::abort();
    }

    g_running.store(true, std::memory_order_release);
    g_server_thread = std::thread(server_loop);
}

void stop_tcp_server() {
    g_running.store(false, std::memory_order_release);

    if (g_listen_fd >= 0) {
        shutdown(g_listen_fd, SHUT_RDWR);
        close(g_listen_fd);
        g_listen_fd = -1;
    }

    if (g_server_thread.joinable())
        g_server_thread.join();

    std::lock_guard<std::mutex> lock(g_clients_mutex);
    for (auto& t : g_client_threads) {
        if (t.joinable()) t.join();
    }
    g_client_threads.clear();
}
