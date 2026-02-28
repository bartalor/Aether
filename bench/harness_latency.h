#pragma once

#include "transport.h"
#include "bench_common.h"

#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

// ---------------------------------------------------------------------------
// Latency harness — transport-agnostic one-way latency measurement
//
// Publishes N_MESSAGES messages with embedded timestamps, measures the
// one-way latency on the subscriber side. Publisher and subscriber run as
// separate processes (fork). Transport owns setup/teardown.
// ---------------------------------------------------------------------------

static constexpr int LATENCY_N_MESSAGES = 100'000;

// Message layout: timestamp embedded so subscriber can compute one-way latency.
struct LatencyMsg {
    uint64_t timestamp_ns;
    uint64_t seq;
};

static uint64_t latency_percentile(const std::vector<uint64_t>& sorted, double p) {
    const size_t idx = static_cast<size_t>(p / 100.0 * static_cast<double>(sorted.size() - 1));
    return sorted[idx];
}

template<BenchTransport T>
LatencyResults run_latency_bench(T& transport) {
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

        std::vector<uint64_t> latencies;
        latencies.reserve(LATENCY_N_MESSAGES);

        bool pub_done = false;
        while (true) {
            LatencyMsg buf{};
            size_t buf_len = sizeof(buf);
            const auto result = transport.consume(&buf, buf_len);

            if (result == ConsumeStatus::Ok) {
                latencies.push_back(now_ns() - buf.timestamp_ns);
            } else if (result == ConsumeStatus::Empty) {
                if (!pub_done) {
                    char done_byte = 0;
                    if (read(done_pipe[0], &done_byte, 1) == 1) pub_done = true;
                } else {
                    break;
                }
            }
            // On Lapped: internal state already advanced, just continue
        }

        close(done_pipe[0]);
        std::sort(latencies.begin(), latencies.end());

        LatencyResults res{};
        if (!latencies.empty()) {
            res.samples   = latencies.size();
            res.min_ns    = latencies.front();
            res.p50_ns    = latency_percentile(latencies, 50);
            res.p99_ns    = latency_percentile(latencies, 99);
            res.p99_9_ns  = latency_percentile(latencies, 99.9);
            res.p99_99_ns = latency_percentile(latencies, 99.99);
            res.max_ns    = latencies.back();
        }

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

    for (int seq = 0; seq < LATENCY_N_MESSAGES; ++seq) {
        LatencyMsg msg{ now_ns(), static_cast<uint64_t>(seq) };
        transport.publish(&msg, sizeof(msg));
    }

    char done_byte = 1;
    write(done_pipe[1], &done_byte, 1);
    close(done_pipe[1]);

    LatencyResults res{};
    read(results_pipe[0], &res, sizeof(res));
    close(results_pipe[0]);

    waitpid(sub_pid, nullptr, 0);
    transport.teardown();

    return res;
}
