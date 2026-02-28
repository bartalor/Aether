#include "aether/subscribe.h"
#include "aether/publish.h"
#include "aether/consume.h"
#include "bench_common.h"
#include "report.h"

#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Message layout
// ---------------------------------------------------------------------------

struct Msg {
    uint64_t seq;
};

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

static uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
         + static_cast<uint64_t>(ts.tv_nsec);
}

// ---------------------------------------------------------------------------
// Benchmark parameters
//
// Both sides run for BENCH_DURATION_NS nanoseconds.  The publisher blasts
// messages as fast as possible; the subscriber drains as fast as it can.
// If the publisher laps the subscriber, ConsumeResult::Lapped advances the
// subscriber's read_seq to the oldest available slot — messages are lost but
// the subscriber keeps running.  We report both rates separately.
// ---------------------------------------------------------------------------

static constexpr uint64_t BENCH_DURATION_NS = 5ULL * 1'000'000'000ULL;  // 5 s

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    const BenchArgs args = parse_bench_args(argc, argv);
    pid_t daemon_pid = start_daemon();

    int ready_pipe[2];
    if (pipe(ready_pipe) != 0) { perror("pipe ready"); std::abort(); }

    int done_pipe[2];
    if (pipe(done_pipe) != 0) { perror("pipe done"); std::abort(); }
    {
        int flags = fcntl(done_pipe[0], F_GETFL, 0);
        fcntl(done_pipe[0], F_SETFL, flags | O_NONBLOCK);
    }

    // Pipe for child to send ThroughputResults back to parent
    int results_pipe[2];
    if (pipe(results_pipe) != 0) { perror("pipe results"); std::abort(); }

    pid_t sub_pid = fork();
    if (sub_pid < 0) { perror("fork sub"); std::abort(); }

    if (sub_pid == 0) {
        // ---------------------------------------------------------------
        // Subscriber child
        // ---------------------------------------------------------------
        close(ready_pipe[0]);
        close(done_pipe[1]);
        close(results_pipe[0]);

        aether::Subscription sub = aether::subscribe("bench", 5);

        char rdy = 1;
        write(ready_pipe[1], &rdy, 1);
        close(ready_pipe[1]);

        uint64_t received  = 0;
        uint64_t lapped    = 0;
        uint64_t t_start   = 0;
        uint64_t read_seq  = 1;
        bool running       = true;

        while (running) {
            Msg buf{};
            uint32_t buf_len = sizeof(buf);
            auto result = aether::consume(sub.hdr, &buf, buf_len, read_seq);

            if (result == aether::ConsumeResult::Ok) {
                if (received == 0) t_start = now_ns();
                ++received;
            } else if (result == aether::ConsumeResult::Lapped) {
                ++lapped;
            } else {
                char done_byte = 0;
                if (read(done_pipe[0], &done_byte, 1) == 1) running = false;
            }
        }

        // Drain remaining messages written before the done signal
        while (true) {
            Msg buf{};
            uint32_t buf_len = sizeof(buf);
            auto result = aether::consume(sub.hdr, &buf, buf_len, read_seq);
            if (result == aether::ConsumeResult::Ok)          { ++received; }
            else if (result == aether::ConsumeResult::Lapped) { ++lapped;   }
            else break;
        }

        const uint64_t t_end = now_ns();
        aether::unsubscribe(sub);
        close(done_pipe[0]);

        ThroughputSubResults res{};
        res.received  = received;
        res.lapped    = lapped;
        res.elapsed_s = static_cast<double>(t_end - t_start) / 1e9;
        res.rate_mmps = static_cast<double>(received) / res.elapsed_s / 1e6;

        write(results_pipe[1], &res, sizeof(res));
        close(results_pipe[1]);
        _exit(0);
    }

    // ---------------------------------------------------------------
    // Publisher parent
    // ---------------------------------------------------------------
    close(ready_pipe[1]);
    close(done_pipe[0]);
    close(results_pipe[1]);

    char rdy = 0;
    read(ready_pipe[0], &rdy, 1);
    close(ready_pipe[0]);

    aether::Subscription pub = aether::subscribe("bench", 5);

    const uint64_t t_pub_start = now_ns();
    uint64_t seq = 0;

    while (now_ns() - t_pub_start < BENCH_DURATION_NS) {
        Msg msg{ seq++ };
        aether::publish(pub.hdr, &msg, sizeof(msg));
    }

    const uint64_t t_pub_end = now_ns();
    aether::unsubscribe(pub);

    char done_byte = 1;
    write(done_pipe[1], &done_byte, 1);
    close(done_pipe[1]);

    // Collect results from subscriber
    ThroughputSubResults sub_res{};
    read(results_pipe[0], &sub_res, sizeof(sub_res));
    close(results_pipe[0]);

    waitpid(sub_pid, nullptr, 0);
    kill(daemon_pid, SIGTERM);
    waitpid(daemon_pid, nullptr, 0);

    const double pub_elapsed_s = static_cast<double>(t_pub_end - t_pub_start) / 1e9;
    const double pub_rate_mmps = static_cast<double>(seq) / pub_elapsed_s / 1e6;

    // ---------------------------------------------------------------
    // Print to console
    // ---------------------------------------------------------------
    printf("--- bench_throughput  (5 s window, 1 pub, 1 sub, same machine) ---\n");
    printf("pub sent     : %llu msgs\n",     (unsigned long long)seq);
    printf("pub elapsed  : %.3f s\n",        pub_elapsed_s);
    printf("pub rate     : %.2f M msgs/s\n", pub_rate_mmps);
    printf("sub received : %llu msgs\n",     (unsigned long long)sub_res.received);
    printf("sub lapped   : %llu msgs\n",     (unsigned long long)sub_res.lapped);
    printf("sub elapsed  : %.3f s\n",        sub_res.elapsed_s);
    printf("sub rate     : %.2f M msgs/s\n", sub_res.rate_mmps);

    const ThroughputResults report{
        seq, pub_elapsed_s, pub_rate_mmps,
        sub_res.received, sub_res.lapped, sub_res.elapsed_s, sub_res.rate_mmps
    };
    write_throughput_report(args, report);

    return 0;
}
