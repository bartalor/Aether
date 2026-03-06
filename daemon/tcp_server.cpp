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

// Read one wire message from fd. Returns false on disconnect or invalid message.
static bool read_wire_msg(int fd, aether::WireHeader& whdr, uint8_t* body, size_t body_cap) {
    if (!read_exact(fd, &whdr, sizeof(whdr)))
        return false;
    if (whdr.body_len > body_cap)
        return false;
    if (whdr.body_len > 0 && !read_exact(fd, body, whdr.body_len))
        return false;
    return true;
}

static void handle_publish(const uint8_t* body, uint32_t body_len) {
    if (body_len < 4) return;
    uint32_t topic_len;
    std::memcpy(&topic_len, body, 4);
    if (4 + topic_len > body_len) return;

    const char* topic_name = reinterpret_cast<const char*>(body + 4);
    const uint8_t* payload = body + 4 + topic_len;
    const uint32_t payload_len = body_len - 4 - topic_len;

    const TopicInfo* topic = get_or_create_topic(topic_name, topic_len);
    if (topic) {
        aether::publish(topic->hdr, payload, payload_len);
    }
}

static void handle_subscribe(int fd, const uint8_t* body, uint32_t body_len) {
    const TopicInfo* topic = get_or_create_topic(
        reinterpret_cast<const char*>(body), body_len);
    if (!topic) return;
    forward_messages(fd, topic->hdr);
}

static void handle_tcp_client(int fd) {
    constexpr size_t MAX_BODY = aether::SLOT_DATA_SIZE + aether::MAX_TOPIC_LEN + 4;
    uint8_t body[MAX_BODY];
    aether::WireHeader whdr{};

    // Read first message to determine client type
    if (!read_wire_msg(fd, whdr, body, MAX_BODY)) {
        close(fd);
        return;
    }

    if (whdr.msg_type == aether::MsgType::Subscribe) {
        // Subscriber: forward ring messages until disconnect
        handle_subscribe(fd, body, whdr.body_len);
    } else if (whdr.msg_type == aether::MsgType::Publish) {
        // Publisher: handle messages until disconnect
        do {
            handle_publish(body, whdr.body_len);
            if (!g_running.load(std::memory_order_relaxed)) break;
            if (!read_wire_msg(fd, whdr, body, MAX_BODY)) break;
        } while (whdr.msg_type == aether::MsgType::Publish);
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
