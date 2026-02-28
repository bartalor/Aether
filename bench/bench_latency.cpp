#include "aether_transport.h"
#include "harness_latency.h"
#include "report.h"

#include <cstdio>

int main(int argc, char* argv[]) {
    const BenchArgs args = parse_bench_args(argc, argv);

    AetherTransport transport("bench", 5);
    const LatencyResults res = run_latency_bench(transport);

    printf("--- bench_latency  (%d published, 1 pub, 1 sub, same machine) ---\n", LATENCY_N_MESSAGES);
    printf("samples  : %llu\n",    (unsigned long long)res.samples);
    printf("min      : %llu ns\n", (unsigned long long)res.min_ns);
    printf("p50      : %llu ns\n", (unsigned long long)res.p50_ns);
    printf("p99      : %llu ns\n", (unsigned long long)res.p99_ns);
    printf("p99.9    : %llu ns\n", (unsigned long long)res.p99_9_ns);
    printf("p99.99   : %llu ns\n", (unsigned long long)res.p99_99_ns);
    printf("max      : %llu ns\n", (unsigned long long)res.max_ns);

    write_latency_report(args, res);

    return 0;
}
