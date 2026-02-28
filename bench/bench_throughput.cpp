#include "aether_transport.h"
#include "harness_throughput.h"
#include "report.h"

#include <cstdio>

int main(int argc, char* argv[]) {
    const BenchArgs args = parse_bench_args(argc, argv);

    AetherTransport transport("bench", 5);
    const ThroughputResults res = run_throughput_bench(transport);

    printf("--- bench_throughput  (5 s window, 1 pub, 1 sub, same machine) ---\n");
    printf("pub sent     : %llu msgs\n",     (unsigned long long)res.pub_sent);
    printf("pub elapsed  : %.3f s\n",        res.pub_elapsed_s);
    printf("pub rate     : %.2f M msgs/s\n", res.pub_rate_mmps);
    printf("sub received : %llu msgs\n",     (unsigned long long)res.sub_received);
    printf("sub lapped   : %llu msgs\n",     (unsigned long long)res.sub_lapped);
    printf("sub elapsed  : %.3f s\n",        res.sub_elapsed_s);
    printf("sub rate     : %.2f M msgs/s\n", res.sub_rate_mmps);

    write_throughput_report(args, res);

    return 0;
}
