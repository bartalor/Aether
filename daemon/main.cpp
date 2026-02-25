#include "acceptor.h"
#include "topic_registry.h"

#include <csignal>   // sigaction, sig_atomic_t
#include <cstdio>    // fprintf
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
// Entry point
// ---------------------------------------------------------------------------

int main() {
    fprintf(stderr, "[aetherd] starting\n");

    if (!install_signal_handlers()) {
        fprintf(stderr, "[aetherd] failed to install signal handlers\n");
        return EXIT_FAILURE;
    }

    fprintf(stderr, "[aetherd] ready\n");

    start_acceptor();

    // ---------------------------------------------------------------------------
    // Main loop — runs until SIGTERM is received
    // ---------------------------------------------------------------------------
    while (!g_shutdown) {
        if (g_dump_stats) {
            g_dump_stats = 0; // clear before acting — avoids re-triggering
            dump_all_topic_stats();
        }

        sleep(1); // placeholder — threads will replace this when we add them
    }

    // ---------------------------------------------------------------------------
    // Graceful shutdown
    // ---------------------------------------------------------------------------
    fprintf(stderr, "[aetherd] shutting down\n");

    stop_acceptor();
    destroy_all_topics();

    fprintf(stderr, "[aetherd] bye\n");
    return 0;
}
