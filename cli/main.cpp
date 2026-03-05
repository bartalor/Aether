#include "aether/control.h"
#include "aether/subscribe.h"
#include "aether/publish.h"
#include "aether/consume.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void usage() {
    fprintf(stderr,
        "Usage:\n"
        "  aether-cli pub <topic> <message>\n"
        "  aether-cli sub <topic>\n"
        "  aether-cli stats\n"
        "  aether-cli shutdown\n");
}

// Read the daemon PID from the PID file. Returns -1 on failure.
static pid_t read_daemon_pid() {
    FILE* f = fopen(aether::DAEMON_PID_PATH, "r");
    if (!f) {
        fprintf(stderr, "error: cannot open %s — is aetherd running?\n",
                aether::DAEMON_PID_PATH);
        return -1;
    }
    int pid = 0;
    if (fscanf(f, "%d", &pid) != 1 || pid <= 0) {
        fprintf(stderr, "error: invalid PID in %s\n", aether::DAEMON_PID_PATH);
        fclose(f);
        return -1;
    }
    fclose(f);
    return static_cast<pid_t>(pid);
}

// ---------------------------------------------------------------------------
// Subcommands
// ---------------------------------------------------------------------------

static int cmd_pub(const char* topic, const char* message) {
    const auto topic_len = static_cast<uint32_t>(strlen(topic));
    aether::Subscription sub = aether::subscribe(topic, topic_len);

    const auto msg_len = static_cast<uint32_t>(strlen(message));
    if (!aether::publish(sub.hdr, message, msg_len)) {
        fprintf(stderr, "error: publish failed (message too large?)\n");
        aether::unsubscribe(sub);
        return 1;
    }

    printf("published %u bytes to '%s'\n", msg_len, topic);
    aether::unsubscribe(sub);
    return 0;
}

static int cmd_sub(const char* topic) {
    const auto topic_len = static_cast<uint32_t>(strlen(topic));
    aether::Subscription sub = aether::subscribe(topic, topic_len);

    uint64_t read_seq = sub.hdr->write_seq.load(std::memory_order_relaxed);
    char buf[aether::SLOT_DATA_SIZE];
    uint32_t buf_len;

    printf("subscribed to '%s', waiting for messages... (Ctrl-C to stop)\n", topic);

    while (true) {
        buf_len = sizeof(buf);
        aether::ConsumeResult r = aether::consume(sub.hdr, buf, buf_len, read_seq);

        if (r == aether::ConsumeResult::Ok) {
            // Print the payload. Not null-terminated, so use fwrite.
            fwrite(buf, 1, buf_len, stdout);
            putchar('\n');
            fflush(stdout);
        } else if (r == aether::ConsumeResult::Lapped) {
            fprintf(stderr, "[lapped — skipped to seq %llu]\n",
                    static_cast<unsigned long long>(read_seq));
        } else {
            usleep(1000); // 1ms — avoid busy spin
        }
    }

    // unreachable, but clean up if we ever add signal handling
    aether::unsubscribe(sub);
    return 0;
}

static int cmd_stats() {
    pid_t pid = read_daemon_pid();
    if (pid < 0) return 1;

    if (kill(pid, SIGUSR1) != 0) {
        perror("kill(SIGUSR1)");
        return 1;
    }
    printf("sent SIGUSR1 to aetherd (pid %d) — check daemon stderr for stats\n", pid);
    return 0;
}

static int cmd_shutdown() {
    pid_t pid = read_daemon_pid();
    if (pid < 0) return 1;

    if (kill(pid, SIGTERM) != 0) {
        perror("kill(SIGTERM)");
        return 1;
    }
    printf("sent SIGTERM to aetherd (pid %d)\n", pid);
    return 0;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage();
        return 1;
    }

    const char* cmd = argv[1];

    if (strcmp(cmd, "pub") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Usage: aether-cli pub <topic> <message>\n");
            return 1;
        }
        return cmd_pub(argv[2], argv[3]);
    }

    if (strcmp(cmd, "sub") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: aether-cli sub <topic>\n");
            return 1;
        }
        return cmd_sub(argv[2]);
    }

    if (strcmp(cmd, "stats") == 0) {
        return cmd_stats();
    }

    if (strcmp(cmd, "shutdown") == 0) {
        return cmd_shutdown();
    }

    fprintf(stderr, "unknown command: %s\n", cmd);
    usage();
    return 1;
}
