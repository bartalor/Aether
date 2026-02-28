#pragma once

#include "transport.h"
#include "bench_common.h"

#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <cstdlib>

// ---------------------------------------------------------------------------
// Throughput harness — transport-agnostic sustained throughput measurement
//
// Publisher blasts messages for THROUGHPUT_DURATION_NS nanoseconds as fast as
// possible. Subscriber drains as fast as it can; lapped messages are counted
// but do not stop the run. Both rates are reported independently.
// ---------------------------------------------------------------------------

static constexpr uint64_t THROUGHPUT_DURATION_NS = 5ULL * 1'000'000'000ULL;

// Minimal payload — just enough to exercise the transport with a real write.
struct ThroughputMsg {
    uint64_t seq;
};

template<BenchTransport T>
ThroughputResults run_throughput_bench(T& transport) {
    transport.setup();

    int ready_pipe[2], done_pipe[2], results_pipe[2];
    if (pipe(ready_pipe)   != 0) { perror("pipe ready");   std::abort(); }
    if (pipe(done_pipe)    != 0) { perror("pipe done");    std::abort(); }
    if (pipe(results_pipe) != 0) { perror("pipe results"); std::abort(); }
    {
        int flags = fcntl(done_pipe[0], F_GETFL, 0);
        fcntl(done_pipe[0], F_SETFL, flags | O_NONBLOCK);
    }

    pid_t sub_pid = fork();
    if (sub_pid < 0) { perror("fork sub"); std::abort(); }

    if (sub_pid == 0) {
        // -------------------------------------------------------------------
        // Subscriber child
        // -------------------------------------------------------------------
        close(ready_pipe[0]);
        close(done_pipe[1]);
        close(results_pipe[0]);

        char rdy = 1;
        write(ready_pipe[1], &rdy, 1);
        close(ready_pipe[1]);

        uint64_t received = 0;
        uint64_t lapped   = 0;
        uint64_t t_start  = 0;
        bool running      = true;

        while (running) {
            ThroughputMsg buf{};
            size_t buf_len = sizeof(buf);
            const auto result = transport.consume(&buf, buf_len);

            if (result == ConsumeStatus::Ok) {
                if (received == 0) t_start = now_ns();
                ++received;
            } else if (result == ConsumeStatus::Lapped) {
                ++lapped;
            } else {
                char done_byte = 0;
                if (read(done_pipe[0], &done_byte, 1) == 1) running = false;
            }
        }

        // Drain remaining messages written before the done signal
        while (true) {
            ThroughputMsg buf{};
            size_t buf_len = sizeof(buf);
            const auto result = transport.consume(&buf, buf_len);
            if      (result == ConsumeStatus::Ok)     { ++received; }
            else if (result == ConsumeStatus::Lapped) { ++lapped;   }
            else break;
        }

        const uint64_t t_end = now_ns();
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

    // -----------------------------------------------------------------------
    // Publisher parent
    // -----------------------------------------------------------------------
    close(ready_pipe[1]);
    close(done_pipe[0]);
    close(results_pipe[1]);

    char rdy = 0;
    read(ready_pipe[0], &rdy, 1);
    close(ready_pipe[0]);

    const uint64_t t_pub_start = now_ns();
    uint64_t seq = 0;

    while (now_ns() - t_pub_start < THROUGHPUT_DURATION_NS) {
        ThroughputMsg msg{ seq++ };
        transport.publish(&msg, sizeof(msg));
    }

    const uint64_t t_pub_end = now_ns();

    char done_byte = 1;
    write(done_pipe[1], &done_byte, 1);
    close(done_pipe[1]);

    ThroughputSubResults sub_res{};
    read(results_pipe[0], &sub_res, sizeof(sub_res));
    close(results_pipe[0]);

    waitpid(sub_pid, nullptr, 0);
    transport.teardown();

    const double pub_elapsed_s = static_cast<double>(t_pub_end - t_pub_start) / 1e9;
    const double pub_rate_mmps = static_cast<double>(seq) / pub_elapsed_s / 1e6;

    return ThroughputResults{
        seq, pub_elapsed_s, pub_rate_mmps,
        sub_res.received, sub_res.lapped, sub_res.elapsed_s, sub_res.rate_mmps
    };
}
