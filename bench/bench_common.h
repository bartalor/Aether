#pragma once

// Shared infrastructure for all Aether benchmarks.
// Provides daemon lifecycle, timing, and command-line argument parsing.

#include "aether/control.h"

#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

static inline uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
         + static_cast<uint64_t>(ts.tv_nsec);
}

// ---------------------------------------------------------------------------
// Daemon lifecycle
// ---------------------------------------------------------------------------

static inline pid_t start_daemon() {
    unlink(aether::DAEMON_SOCKET_PATH);
    pid_t pid = fork();
    if (pid < 0) { perror("fork daemon"); std::abort(); }
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
        execl(AETHERD_PATH, "aetherd", nullptr);
        _exit(1);
    }
    for (int i = 0; i < 50; ++i) {
        struct stat st{};
        if (stat(aether::DAEMON_SOCKET_PATH, &st) == 0) return pid;
        usleep(100'000);
    }
    fprintf(stderr, "timeout waiting for daemon socket\n");
    std::abort();
}

// ---------------------------------------------------------------------------
// Command-line arguments
// ---------------------------------------------------------------------------

struct BenchArgs {
    bool record = false;  // write to official CSV in addition to scratch
};

static inline BenchArgs parse_bench_args(int argc, char* argv[]) {
    BenchArgs args;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--record") == 0) args.record = true;
    }
    return args;
}

// ---------------------------------------------------------------------------
// Benchmark result types
// ---------------------------------------------------------------------------

struct LatencyResults {
    uint64_t samples;
    uint64_t min_ns;
    uint64_t p50_ns;
    uint64_t p99_ns;
    uint64_t p99_9_ns;
    uint64_t p99_99_ns;
    uint64_t max_ns;
};

// Sent from subscriber child to parent via pipe (sub-side only)
struct ThroughputSubResults {
    uint64_t received;
    uint64_t lapped;
    double   elapsed_s;
    double   rate_mmps;
};

// Full combined results for reporting (pub + sub)
struct ThroughputResults {
    uint64_t pub_sent;
    double   pub_elapsed_s;
    double   pub_rate_mmps;
    uint64_t sub_received;
    uint64_t sub_lapped;
    double   sub_elapsed_s;
    double   sub_rate_mmps;
};
