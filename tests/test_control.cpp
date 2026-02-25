#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aether/control.h"
#include "aether/shm.h"
#include "aether/ring.h"

#include <fcntl.h>       // open, O_WRONLY
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef AETHERD_PATH
#error "AETHERD_PATH must be defined by CMake"
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void wait_for_socket() {
    for (int i = 0; i < 50; ++i) {
        struct stat st{};
        if (stat(aether::DAEMON_SOCKET_PATH, &st) == 0) return;
        usleep(100'000); // 100ms per attempt, 5s total
    }
    fprintf(stderr, "timeout waiting for daemon socket\n");
    std::abort();
}

static aether::SubscribeResponse do_subscribe(const char* topic) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); std::abort(); }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, aether::DAEMON_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("connect"); std::abort();
    }

    aether::SubscribeRequest req{};
    req.topic_len = static_cast<uint32_t>(strlen(topic));
    strncpy(req.topic, topic, aether::MAX_TOPIC_LEN - 1);
    write(fd, &req, sizeof(req));

    aether::SubscribeResponse resp{};
    read(fd, &resp, sizeof(resp));
    close(fd);
    return resp;
}

// ---------------------------------------------------------------------------
// Fixture: starts a fresh daemon before each test, stops it after.
// Each test gets a clean slate — no leftover topics.
// ---------------------------------------------------------------------------

struct DaemonFixture {
    pid_t pid;

    DaemonFixture() {
        unlink(aether::DAEMON_SOCKET_PATH); // remove stale socket if any

        pid = fork();
        if (pid == 0) {
            // Suppress daemon log output during tests
            int devnull = open("/dev/null", O_WRONLY);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
            execl(AETHERD_PATH, "aetherd", nullptr);
            _exit(1);
        }
        if (pid < 0) { perror("fork"); std::abort(); }
        wait_for_socket();
    }

    ~DaemonFixture() {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        // daemon calls destroy_all_topics() on shutdown — shm cleaned up
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE_FIXTURE(DaemonFixture, "subscribe returns Ok for new topic") {
    auto resp = do_subscribe("prices");
    CHECK(resp.status == aether::ControlStatus::Ok);
    CHECK(resp.capacity == 1024);
    CHECK(strcmp(resp.shm_name, "/aether_prices") == 0);
}

TEST_CASE_FIXTURE(DaemonFixture, "subscribe twice returns same shm_name") {
    auto resp1 = do_subscribe("prices");
    auto resp2 = do_subscribe("prices");
    CHECK(resp1.status == aether::ControlStatus::Ok);
    CHECK(resp2.status == aether::ControlStatus::Ok);
    CHECK(strcmp(resp1.shm_name, resp2.shm_name) == 0);
}

TEST_CASE_FIXTURE(DaemonFixture, "different topics get different shm segments") {
    auto resp1 = do_subscribe("prices");
    auto resp2 = do_subscribe("orders");
    CHECK(strcmp(resp1.shm_name, "/aether_prices") == 0);
    CHECK(strcmp(resp2.shm_name, "/aether_orders") == 0);
    CHECK(strcmp(resp1.shm_name, resp2.shm_name) != 0);
}

TEST_CASE_FIXTURE(DaemonFixture, "shm_attach succeeds with returned shm_name") {
    auto resp = do_subscribe("prices");
    REQUIRE(resp.status == aether::ControlStatus::Ok);

    aether::RingHeader* hdr = aether::shm_attach(resp.shm_name);
    REQUIRE(hdr != nullptr);
    CHECK(hdr->capacity == 1024);
    aether::shm_detach(hdr);
}
