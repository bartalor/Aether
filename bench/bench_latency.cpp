#include "aether/subscribe.h"
#include "aether/publish.h"
#include "aether/consume.h"
#include "aether/control.h"
#include "report.h"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

// ---------------------------------------------------------------------------
// Message layout
// ---------------------------------------------------------------------------

struct Msg {
    uint64_t timestamp_ns;  // CLOCK_MONOTONIC_RAW at publish time
    uint64_t seq;
};

// ---------------------------------------------------------------------------
// Results passed from subscriber child to parent via pipe
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
// Daemon lifecycle
// ---------------------------------------------------------------------------

static pid_t start_daemon() {
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
// Benchmark parameters
// ---------------------------------------------------------------------------

static constexpr int N_MESSAGES = 100'000;

// ---------------------------------------------------------------------------
// Percentile helper
// ---------------------------------------------------------------------------

static uint64_t percentile(const std::vector<uint64_t>& sorted, double p) {
    const size_t idx = static_cast<size_t>(p / 100.0 * static_cast<double>(sorted.size() - 1));
    return sorted[idx];
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    pid_t daemon_pid = start_daemon();

    int ready_pipe[2];
    if (pipe(ready_pipe) != 0) { perror("pipe ready"); std::abort(); }

    int done_pipe[2];
    if (pipe(done_pipe) != 0) { perror("pipe done"); std::abort(); }
    {
        int flags = fcntl(done_pipe[0], F_GETFL, 0);
        fcntl(done_pipe[0], F_SETFL, flags | O_NONBLOCK);
    }

    // Pipe for child to send LatencyResults back to parent
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

        std::vector<uint64_t> latencies;
        latencies.reserve(N_MESSAGES);

        uint64_t read_seq = 1;
        bool pub_done     = false;

        while (true) {
            Msg buf{};
            uint32_t buf_len = sizeof(buf);
            auto result = aether::consume(sub.hdr, &buf, buf_len, read_seq);

            if (result == aether::ConsumeResult::Ok) {
                latencies.push_back(now_ns() - buf.timestamp_ns);
            } else if (result == aether::ConsumeResult::Empty) {
                if (!pub_done) {
                    char done_byte = 0;
                    if (read(done_pipe[0], &done_byte, 1) == 1) pub_done = true;
                } else {
                    break;
                }
            }
            // On Lapped: read_seq was advanced, just continue
        }

        aether::unsubscribe(sub);
        close(done_pipe[0]);

        std::sort(latencies.begin(), latencies.end());

        LatencyResults res{};
        if (!latencies.empty()) {
            res.samples   = latencies.size();
            res.min_ns    = latencies.front();
            res.p50_ns    = percentile(latencies, 50);
            res.p99_ns    = percentile(latencies, 99);
            res.p99_9_ns  = percentile(latencies, 99.9);
            res.p99_99_ns = percentile(latencies, 99.99);
            res.max_ns    = latencies.back();
        }

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

    for (int seq = 0; seq < N_MESSAGES; ++seq) {
        Msg msg{ now_ns(), static_cast<uint64_t>(seq) };
        aether::publish(pub.hdr, &msg, sizeof(msg));
    }

    aether::unsubscribe(pub);

    char done_byte = 1;
    write(done_pipe[1], &done_byte, 1);
    close(done_pipe[1]);

    // Collect results from subscriber
    LatencyResults res{};
    read(results_pipe[0], &res, sizeof(res));
    close(results_pipe[0]);

    waitpid(sub_pid, nullptr, 0);
    kill(daemon_pid, SIGTERM);
    waitpid(daemon_pid, nullptr, 0);

    // ---------------------------------------------------------------
    // Print to console
    // ---------------------------------------------------------------
    printf("--- bench_latency  (%d published, 1 pub, 1 sub, same machine) ---\n", N_MESSAGES);
    printf("samples  : %llu\n",    (unsigned long long)res.samples);
    printf("min      : %llu ns\n", (unsigned long long)res.min_ns);
    printf("p50      : %llu ns\n", (unsigned long long)res.p50_ns);
    printf("p99      : %llu ns\n", (unsigned long long)res.p99_ns);
    printf("p99.9    : %llu ns\n", (unsigned long long)res.p99_9_ns);
    printf("p99.99   : %llu ns\n", (unsigned long long)res.p99_99_ns);
    printf("max      : %llu ns\n", (unsigned long long)res.max_ns);

    // ---------------------------------------------------------------
    // Write CSV row
    // ---------------------------------------------------------------
    FILE* f = open_report_csv(
        "bench_latency.csv",
        "timestamp,aether_version,ring_version,samples,"
        "min_ns,p50_ns,p99_ns,p99_9_ns,p99_99_ns,max_ns"
    );
    if (f) {
        char ts[32];
        fill_timestamp(ts, sizeof(ts));
        fprintf(f, "%s,%s,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                ts,
                AETHER_VERSION_STRING,
                aether::RING_VERSION,
                (unsigned long long)res.samples,
                (unsigned long long)res.min_ns,
                (unsigned long long)res.p50_ns,
                (unsigned long long)res.p99_ns,
                (unsigned long long)res.p99_9_ns,
                (unsigned long long)res.p99_99_ns,
                (unsigned long long)res.max_ns);
        fclose(f);
    }

    return 0;
}
