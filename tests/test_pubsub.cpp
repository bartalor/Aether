
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aether/subscribe.h"
#include "aether/publish.h"
#include "aether/consume.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "daemon_fixture.h"

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

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
