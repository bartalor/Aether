#pragma once

// Benchmark CSV reporting utilities.
// Reports are appended to master CSV files in AETHER_REPORTS_DIR.
// Each file gets a header row on first creation.

#include "bench_common.h"
#include "aether/version.h"
#include "aether/ring.h"

#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef AETHER_REPORTS_DIR
#error "AETHER_REPORTS_DIR must be defined by CMake"
#endif

// ---------------------------------------------------------------------------
// Timestamp
// ---------------------------------------------------------------------------

static inline void fill_timestamp(char* buf, size_t len) {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", t);
}

// ---------------------------------------------------------------------------
// CSV file helpers
// ---------------------------------------------------------------------------

// Opens the CSV at <dir>/<filename> in append mode.
// Creates AETHER_REPORTS_DIR and <dir> if needed.
// If the file is new, writes the header row first.
// Returns the open FILE*, or nullptr on failure.
static inline FILE* open_report_csv(const char* dir, const char* filename, const char* header) {
    mkdir(AETHER_REPORTS_DIR, 0755);  // ensure root exists
    mkdir(dir, 0755);                  // create subdir (no-op if same as root or already exists)

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, filename);

    const bool is_new = (access(path, F_OK) != 0);

    FILE* f = fopen(path, "a");
    if (!f) {
        fprintf(stderr, "[report] failed to open %s\n", path);
        return nullptr;
    }

    if (is_new) {
        fprintf(f, "%s\n", header);
    }

    return f;
}

// ---------------------------------------------------------------------------
// Internal: write a pre-formatted data row to the appropriate CSV files.
// Prepends timestamp, aether_version, ring_version automatically.
// All CSV routing (scratch vs official, filenames, dirs) lives here.
// ---------------------------------------------------------------------------

static inline void write_csv_row(const BenchArgs& args, const char* filename,
                                  const char* header, const char* data) {
    auto do_write = [&](FILE* f) {
        if (!f) return;
        char ts[32];
        fill_timestamp(ts, sizeof(ts));
        fprintf(f, "%s,%s,%u,%s\n", ts, AETHER_VERSION_STRING, aether::RING_VERSION, data);
        fclose(f);
    };

    do_write(open_report_csv(AETHER_REPORTS_DIR "/scratch", filename, header));
    if (args.record)
        do_write(open_report_csv(AETHER_REPORTS_DIR, filename, header));
}

// ---------------------------------------------------------------------------
// Per-benchmark report writers — only the data fields live here
// ---------------------------------------------------------------------------

static inline void write_latency_report(const BenchArgs& args, const LatencyResults& res) {
    static constexpr const char* HEADER =
        "timestamp,aether_version,ring_version,samples,"
        "min_ns,p50_ns,p99_ns,p99_9_ns,p99_99_ns,max_ns";

    char data[256];
    snprintf(data, sizeof(data), "%llu,%llu,%llu,%llu,%llu,%llu,%llu",
             (unsigned long long)res.samples,
             (unsigned long long)res.min_ns,
             (unsigned long long)res.p50_ns,
             (unsigned long long)res.p99_ns,
             (unsigned long long)res.p99_9_ns,
             (unsigned long long)res.p99_99_ns,
             (unsigned long long)res.max_ns);

    write_csv_row(args, "bench_latency.csv", HEADER, data);
}

static inline void write_throughput_report(const BenchArgs& args, const ThroughputResults& res) {
    static constexpr const char* HEADER =
        "timestamp,aether_version,ring_version,"
        "pub_sent,pub_elapsed_s,pub_rate_mmps,"
        "sub_received,sub_lapped,sub_elapsed_s,sub_rate_mmps";

    char data[256];
    snprintf(data, sizeof(data), "%llu,%.3f,%.2f,%llu,%llu,%.3f,%.2f",
             (unsigned long long)res.pub_sent,
             res.pub_elapsed_s, res.pub_rate_mmps,
             (unsigned long long)res.sub_received,
             (unsigned long long)res.sub_lapped,
             res.sub_elapsed_s, res.sub_rate_mmps);

    write_csv_row(args, "bench_throughput.csv", HEADER, data);
}
