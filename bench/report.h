#pragma once

// Benchmark CSV reporting utilities.
// Reports are appended to master CSV files in AETHER_REPORTS_DIR.
// Each file gets a header row on first creation.

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

// Opens the CSV at AETHER_REPORTS_DIR/<filename> in append mode.
// Creates the reports directory if needed.
// If the file is new, writes the header row first.
// Returns the open FILE*, or nullptr on failure.
static inline FILE* open_report_csv(const char* filename, const char* header) {
    mkdir(AETHER_REPORTS_DIR, 0755);  // no-op if already exists

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", AETHER_REPORTS_DIR, filename);

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
