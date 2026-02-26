
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aether/control.h"
#include "aether/shm.h"
#include "aether/ring.h"
#include "aether/subscribe.h"
#include "aether/publish.h"
#include "aether/consume.h"

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

TEST_CASE_FIXTURE(DaemonFixture, "end-to-end: publish and consume a message") {
    aether::Subscription sub = aether::subscribe("prices", 6);
    REQUIRE(sub.hdr != nullptr);

    const char msg[] = "hello aether";
    bool ok = aether::publish(sub.hdr, msg, sizeof(msg));
    REQUIRE(ok);

    char buf[64]{};
    uint32_t buf_len = sizeof(buf);
    uint64_t read_seq = 1;

    aether::ConsumeResult result = aether::consume(sub.hdr, buf, buf_len, read_seq);
    REQUIRE(result == aether::ConsumeResult::Ok);
    CHECK(buf_len == sizeof(msg));
    CHECK(strcmp(buf, "hello aether") == 0);

    aether::unsubscribe(sub);
}

TEST_CASE_FIXTURE(DaemonFixture, "multi-process: publisher and subscriber in separate processes") {
    // Pipe: child signals parent after publishing
    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);

    pid_t child = fork();
    REQUIRE(child >= 0);

    if (child == 0) {
        // Child process: subscribe and publish one message
        close(pipefd[0]);
        aether::Subscription pub = aether::subscribe("prices", 6);
        const char msg[] = "from child";
        aether::publish(pub.hdr, msg, sizeof(msg));
        aether::unsubscribe(pub);
        char done = 1;
        write(pipefd[1], &done, 1);
        close(pipefd[1]);
        _exit(0); // avoid running DaemonFixture destructor in child
    }

    // Parent: wait for child to publish, then consume
    close(pipefd[1]);
    char done = 0;
    read(pipefd[0], &done, 1);
    close(pipefd[0]);

    aether::Subscription sub = aether::subscribe("prices", 6);
    char buf[64]{};
    uint32_t buf_len = sizeof(buf);
    uint64_t read_seq = 1;

    aether::ConsumeResult result = aether::consume(sub.hdr, buf, buf_len, read_seq);
    REQUIRE(result == aether::ConsumeResult::Ok);
    CHECK(strcmp(buf, "from child") == 0);

    aether::unsubscribe(sub);
    waitpid(child, nullptr, 0);
}

TEST_CASE_FIXTURE(DaemonFixture, "lapped consumer: slow subscriber falls behind") {
    aether::Subscription sub = aether::subscribe("prices", 6);
    const uint32_t capacity = sub.hdr->capacity; // 1024

    // Publish capacity+1 messages to wrap the ring
    for (uint32_t i = 0; i <= capacity; ++i) {
        aether::publish(sub.hdr, &i, sizeof(i));
    }

    char buf[64]{};
    uint32_t buf_len = sizeof(buf);
    uint64_t read_seq = 1;

    // Subscriber starting at seq 1 is now lapped
    aether::ConsumeResult result = aether::consume(sub.hdr, buf, buf_len, read_seq);
    REQUIRE(result == aether::ConsumeResult::Lapped);

    // After Lapped, read_seq is advanced to oldest available — next consume should succeed
    buf_len = sizeof(buf);
    result = aether::consume(sub.hdr, buf, buf_len, read_seq);
    CHECK(result == aether::ConsumeResult::Ok);

    aether::unsubscribe(sub);
}

TEST_CASE_FIXTURE(DaemonFixture, "multiple subscribers on same topic each see all messages") {
    aether::Subscription sub1 = aether::subscribe("prices", 6);
    aether::Subscription sub2 = aether::subscribe("prices", 6);

    const char msg[] = "broadcast";
    aether::publish(sub1.hdr, msg, sizeof(msg));

    char buf1[64]{}, buf2[64]{};
    uint32_t len1 = sizeof(buf1), len2 = sizeof(buf2);
    uint64_t seq1 = 1, seq2 = 1;

    CHECK(aether::consume(sub1.hdr, buf1, len1, seq1) == aether::ConsumeResult::Ok);
    CHECK(aether::consume(sub2.hdr, buf2, len2, seq2) == aether::ConsumeResult::Ok);
    CHECK(strcmp(buf1, "broadcast") == 0);
    CHECK(strcmp(buf2, "broadcast") == 0);

    aether::unsubscribe(sub1);
    aether::unsubscribe(sub2);
}

TEST_CASE_FIXTURE(DaemonFixture, "subscriber on different topic does not receive messages") {
    aether::Subscription pub = aether::subscribe("prices", 6);
    aether::Subscription sub = aether::subscribe("orders", 6);

    const char msg[] = "prices only";
    aether::publish(pub.hdr, msg, sizeof(msg));

    char buf[64]{};
    uint32_t buf_len = sizeof(buf);
    uint64_t read_seq = 1;

    // Consumer on "orders" should see nothing published to "prices"
    aether::ConsumeResult result = aether::consume(sub.hdr, buf, buf_len, read_seq);
    CHECK(result == aether::ConsumeResult::Empty);

    aether::unsubscribe(pub);
    aether::unsubscribe(sub);
}

TEST_CASE_FIXTURE(DaemonFixture, "late subscriber can read messages still in ring") {
    aether::Subscription pub = aether::subscribe("prices", 6);

    // Publish messages before subscriber attaches
    for (int i = 0; i < 10; ++i) {
        aether::publish(pub.hdr, &i, sizeof(i));
    }

    // Late subscriber attaches — messages are still in the ring
    aether::Subscription sub = aether::subscribe("prices", 6);

    int val = -1;
    uint32_t buf_len = sizeof(val);
    uint64_t read_seq = 1;

    aether::ConsumeResult result = aether::consume(sub.hdr, &val, buf_len, read_seq);
    REQUIRE(result == aether::ConsumeResult::Ok);
    CHECK(val == 0); // first message published

    aether::unsubscribe(pub);
    aether::unsubscribe(sub);
}
