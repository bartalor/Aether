#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aether/remote.h"
#include "aether/ring.h"

#include <csignal>
#include <cstring>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Helper: start and stop the daemon for each test
// ---------------------------------------------------------------------------

static pid_t g_daemon_pid = -1;

static void start_daemon() {
    g_daemon_pid = fork();
    if (g_daemon_pid == 0) {
        execl(AETHERD_PATH, "aetherd", nullptr);
        _exit(1);
    }
    usleep(200'000); // 200ms — give daemon time to bind
}

static void stop_daemon() {
    if (g_daemon_pid > 0) {
        kill(g_daemon_pid, SIGTERM);
        waitpid(g_daemon_pid, nullptr, 0);
        g_daemon_pid = -1;
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("tcp pub then sub receives message") {
    start_daemon();

    // Subscribe first — server enters forwarding mode
    auto sub = aether::remote_connect("127.0.0.1");
    aether::remote_subscribe(sub, "test", 4);
    usleep(50'000); // let server's forward_messages loop start

    // Publish from a separate connection
    auto pub = aether::remote_connect("127.0.0.1");
    const char* msg = "hello over tcp";
    REQUIRE(aether::remote_publish(pub, "test", 4, msg, strlen(msg)));
    aether::remote_disconnect(pub);

    // Consume — should arrive within the timeout
    char buf[aether::SLOT_DATA_SIZE];
    int n = aether::remote_consume(sub, buf, sizeof(buf), 2000);

    CHECK(n == static_cast<int>(strlen(msg)));
    CHECK(memcmp(buf, msg, strlen(msg)) == 0);

    aether::remote_disconnect(sub);
    stop_daemon();
}

TEST_CASE("tcp sub receives multiple messages") {
    start_daemon();

    // Subscribe first
    auto sub = aether::remote_connect("127.0.0.1");
    aether::remote_subscribe(sub, "multi", 5);
    usleep(50'000);

    // Publish 5 messages from a separate connection
    auto pub = aether::remote_connect("127.0.0.1");
    for (int i = 0; i < 5; ++i) {
        char msg[32];
        int len = snprintf(msg, sizeof(msg), "msg-%d", i);
        REQUIRE(aether::remote_publish(pub, "multi", 5, msg, len));
    }
    aether::remote_disconnect(pub);

    // Consume all 5
    for (int i = 0; i < 5; ++i) {
        char expected[32];
        int exp_len = snprintf(expected, sizeof(expected), "msg-%d", i);

        char buf[aether::SLOT_DATA_SIZE];
        int n = aether::remote_consume(sub, buf, sizeof(buf), 2000);

        CHECK(n == exp_len);
        CHECK(memcmp(buf, expected, exp_len) == 0);
    }

    aether::remote_disconnect(sub);
    stop_daemon();
}

TEST_CASE("tcp remote_consume times out when no messages") {
    start_daemon();

    auto sub = aether::remote_connect("127.0.0.1");
    aether::remote_subscribe(sub, "empty", 5);

    // No one publishes — should timeout and return -1, not hang
    char buf[aether::SLOT_DATA_SIZE];
    int n = aether::remote_consume(sub, buf, sizeof(buf), 500);
    CHECK(n == -1);

    aether::remote_disconnect(sub);
    stop_daemon();
}
