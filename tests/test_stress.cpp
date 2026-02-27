
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aether/subscribe.h"
#include "aether/publish.h"
#include "aether/consume.h"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <climits>

#include "daemon_fixture.h"

// ---------------------------------------------------------------------------
// Message layout embedded in the ring buffer payload
// ---------------------------------------------------------------------------

struct Msg {
    uint32_t publisher_id;  // which publisher sent this
    uint32_t msg_seq;       // per-publisher sequence number, starts at 0
};

// ---------------------------------------------------------------------------
// Test parameters
// ---------------------------------------------------------------------------

constexpr int     N_PUBLISHERS       = 4;
constexpr int     MSGS_PER_PUBLISHER = 200;
constexpr int     TOTAL_MSGS         = N_PUBLISHERS * MSGS_PER_PUBLISHER; // 800 < capacity 1024
constexpr int     MAX_SPIN           = TOTAL_MSGS * 1000; // safety: bail if stuck

// ---------------------------------------------------------------------------
// Stress test
// ---------------------------------------------------------------------------

TEST_CASE_FIXTURE(DaemonFixture, "stress: N concurrent publishers, 1 subscriber, ordered delivery per producer") {
    // Subscribe before forking — ensures we don't miss any messages
    aether::Subscription sub = aether::subscribe("stress", 6);

    // Pipe: each child writes 1 byte when it has finished publishing
    int done_pipe[2];
    REQUIRE(pipe(done_pipe) == 0);

    // Fork N publisher processes
    std::array<pid_t, N_PUBLISHERS> children{};
    for (int p = 0; p < N_PUBLISHERS; ++p) {
        pid_t child = fork();
        REQUIRE(child >= 0);

        if (child == 0) {
            // Child: publish MSGS_PER_PUBLISHER messages then signal done
            close(done_pipe[0]);

            aether::Subscription pub = aether::subscribe("stress", 6);
            for (uint32_t seq = 0; seq < MSGS_PER_PUBLISHER; ++seq) {
                Msg msg{static_cast<uint32_t>(p), seq};
                aether::publish(pub.hdr, &msg, sizeof(msg));
            }
            aether::unsubscribe(pub);

            char done = 1;
            write(done_pipe[1], &done, 1);
            close(done_pipe[1]);
            _exit(0); // skip destructors — do not kill daemon
        }

        children[p] = child;
    }

    // Parent: wait for all publishers to finish before consuming
    close(done_pipe[1]);
    for (int i = 0; i < N_PUBLISHERS; ++i) {
        char done = 0;
        read(done_pipe[0], &done, 1);
    }
    close(done_pipe[0]);

    // Consume all messages, verify per-publisher ordering
    std::array<uint32_t, N_PUBLISHERS> last_seq{};
    last_seq.fill(UINT32_MAX); // sentinel: not seen yet

    int received = 0;
    int spins    = 0;
    uint64_t read_seq = 1;

    while (received < TOTAL_MSGS && spins < MAX_SPIN) {
        Msg buf{};
        uint32_t buf_len = sizeof(buf);

        auto result = aether::consume(sub.hdr, &buf, buf_len, read_seq);

        if (result == aether::ConsumeResult::Ok) {
            REQUIRE(buf.publisher_id < static_cast<uint32_t>(N_PUBLISHERS));

            // Messages from a given publisher must arrive in increasing order
            if (last_seq[buf.publisher_id] != UINT32_MAX) {
                CHECK(buf.msg_seq == last_seq[buf.publisher_id] + 1);
            }
            last_seq[buf.publisher_id] = buf.msg_seq;
            ++received;
            spins = 0; // reset spin counter on progress
        } else {
            ++spins;
        }
        // Lapped should not occur: 800 messages < ring capacity 1024
    }

    CHECK(received == TOTAL_MSGS);

    aether::unsubscribe(sub);

    for (pid_t child : children) {
        waitpid(child, nullptr, 0);
    }
}
