#include "aether/shm.h"

#include <sys/mman.h> // shm_unlink
#include <csignal>   // sigaction, sig_atomic_t
#include <cstdio>    // fprintf, printf
#include <cstdlib>   // EXIT_FAILURE
#include <unistd.h>  // sleep

// ---------------------------------------------------------------------------
// Signal flags
// ---------------------------------------------------------------------------

// volatile: prevents the compiler from caching this in a register.
// sig_atomic_t: guaranteed to be read/written atomically on this platform —
// safe to write from a signal handler and read from the main loop.
static volatile sig_atomic_t g_shutdown   = 0;
static volatile sig_atomic_t g_dump_stats = 0;

// Signal handlers must only do async-signal-safe operations.
// Setting an integer flag is safe. Everything else happens in the main loop.
static void handle_sigterm(int) { g_shutdown   = 1; }
static void handle_sigusr1(int) { g_dump_stats = 1; }

// ---------------------------------------------------------------------------
// Signal setup
// ---------------------------------------------------------------------------

static bool install_signal_handlers() {
    struct sigaction sa{};
    sa.sa_flags = SA_RESTART; // restart system calls interrupted by signals
                              // (e.g. sleep() continues instead of returning EINTR)
    sigemptyset(&sa.sa_mask); // don't block other signals during handler

    sa.sa_handler = handle_sigterm;
    if (sigaction(SIGTERM, &sa, nullptr) == -1) return false;

    sa.sa_handler = handle_sigusr1;
    if (sigaction(SIGUSR1, &sa, nullptr) == -1) return false;

    return true;
}

// ---------------------------------------------------------------------------
// Stats dump
// ---------------------------------------------------------------------------

static void dump_stats(aether::RingHeader* hdr) {
    // write_seq - 1 = total messages published since startup.
    // (write_seq starts at 1, so subtract 1 to get the count.)
    const uint64_t total = hdr->write_seq.load(std::memory_order_relaxed) - 1;
    printf("[aetherd] stats: capacity=%u  messages_published=%llu\n",
           hdr->capacity,
           static_cast<unsigned long long>(total));
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

static constexpr const char* SHM_NAME = "/aetherd-default";
static constexpr uint32_t    CAPACITY = 1024; // number of ring buffer slots

int main() {
    fprintf(stderr, "[aetherd] starting\n");

    if (!install_signal_handlers()) {
        fprintf(stderr, "[aetherd] failed to install signal handlers\n");
        return EXIT_FAILURE;
    }

    // Clean up any leftover segment from a previous crash before creating a new one.
    shm_unlink(SHM_NAME);

    aether::RingHeader* hdr = aether::shm_create(SHM_NAME, CAPACITY);
    if (hdr == nullptr) {
        fprintf(stderr, "[aetherd] failed to create shared memory segment\n");
        return EXIT_FAILURE;
    }

    fprintf(stderr, "[aetherd] ready (shm=%s capacity=%u)\n", SHM_NAME, CAPACITY);

    // ---------------------------------------------------------------------------
    // Main loop — runs until SIGTERM is received
    // ---------------------------------------------------------------------------
    while (!g_shutdown) {
        if (g_dump_stats) {
            g_dump_stats = 0; // clear before acting — avoids re-triggering
            dump_stats(hdr);
        }

        sleep(1); // placeholder — threads will replace this when we add them
    }

    // ---------------------------------------------------------------------------
    // Graceful shutdown
    // ---------------------------------------------------------------------------
    fprintf(stderr, "[aetherd] shutting down\n");

    aether::shm_detach(hdr);
    aether::shm_destroy(SHM_NAME);

    fprintf(stderr, "[aetherd] bye\n");
    return 0;
}
