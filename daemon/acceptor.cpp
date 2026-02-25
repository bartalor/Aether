#include "acceptor.h"
#include "aether/control.h"

#include <sys/socket.h>  // socket, bind, listen, accept
#include <sys/un.h>      // sockaddr_un
#include <unistd.h>      // read, write, close, unlink

#include <cstdio>        // fprintf, perror
#include <cstdlib>       // abort
#include <cstring>       // strncpy
#include <thread>

static int         g_listen_fd = -1;
static std::thread g_acceptor_thread;

// ---------------------------------------------------------------------------
// Handle a single client connection
// ---------------------------------------------------------------------------

static void handle_client(int client_fd) {
    aether::SubscribeRequest req{};
    ssize_t n = read(client_fd, &req, sizeof(req));
    if (n != static_cast<ssize_t>(sizeof(req))) {
        close(client_fd);
        return;
    }

    // Stub: topic management not yet implemented.
    // Always respond Ok with a placeholder shm name.
    aether::SubscribeResponse resp{};
    resp.status   = aether::ControlStatus::Ok;
    resp.capacity = 1024;
    std::strncpy(resp.shm_name, "/aetherd-default", aether::MAX_SHM_NAME_LEN - 1);

    write(client_fd, &resp, sizeof(resp));
    close(client_fd);
}

// ---------------------------------------------------------------------------
// Acceptor loop — runs on dedicated thread
// ---------------------------------------------------------------------------

static void acceptor_loop() {
    fprintf(stderr, "[aetherd] acceptor listening on %s\n", aether::DAEMON_SOCKET_PATH);

    while (true) {
        int client_fd = accept(g_listen_fd, nullptr, nullptr);
        if (client_fd < 0) {
            // listen fd was closed by stop_acceptor() — time to exit
            break;
        }
        handle_client(client_fd);
    }

    fprintf(stderr, "[aetherd] acceptor stopped\n");
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void start_acceptor() {
    unlink(aether::DAEMON_SOCKET_PATH); // remove stale socket from previous run

    g_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        perror("socket");
        std::abort();
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, aether::DAEMON_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(g_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        std::abort();
    }

    if (listen(g_listen_fd, 16) < 0) {
        perror("listen");
        std::abort();
    }

    g_acceptor_thread = std::thread(acceptor_loop);
}

void stop_acceptor() {
    if (g_listen_fd >= 0) {
        close(g_listen_fd); // unblocks accept() in the acceptor thread
        g_listen_fd = -1;
    }
    if (g_acceptor_thread.joinable()) {
        g_acceptor_thread.join();
    }
    unlink(aether::DAEMON_SOCKET_PATH);
}
