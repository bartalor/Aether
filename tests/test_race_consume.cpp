#include "aether/shm.h"
#include "aether/publish.h"
#include "aether/consume.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <thread>

// ---------------------------------------------------------------------------
// Reproducer for the consume() TOCTOU race.
//
// Setup: tiny ring (capacity=4), one fast publisher, one consumer.
// Each message payload is filled with a single repeating byte derived from
// its sequence number. If the consumer ever reads a payload where the bytes
// are NOT all the same value, the publisher overwrote the slot mid-read.
// ---------------------------------------------------------------------------

static constexpr const char* SHM_NAME = "/aether-test-race";
static constexpr uint32_t CAPACITY = 4;
static constexpr uint32_t PAYLOAD_SIZE = 512; // large enough to widen the race window
static constexpr uint64_t NUM_PUBLISHES = 2'000'000;

static std::atomic<bool> g_start{false};
static std::atomic<bool> g_pub_done{false};

static void publisher(aether::RingHeader* hdr) {
    uint8_t payload[PAYLOAD_SIZE];

    // Wait for both threads to be ready.
    while (!g_start.load(std::memory_order_acquire)) {}

    for (uint64_t i = 0; i < NUM_PUBLISHES; ++i) {
        // Fill payload with a repeating byte unique to this sequence.
        const uint8_t fill = static_cast<uint8_t>(i & 0xFF);
        std::memset(payload, fill, PAYLOAD_SIZE);
        aether::publish(hdr, payload, PAYLOAD_SIZE);
    }

    g_pub_done.store(true, std::memory_order_release);
}

int main() {
    printf("=== test_race_consume ===\n");
    printf("Attempting to trigger consume() TOCTOU race...\n");
    printf("capacity=%u, payload=%u bytes, publishes=%llu\n\n",
           CAPACITY, PAYLOAD_SIZE, static_cast<unsigned long long>(NUM_PUBLISHES));

    shm_unlink(SHM_NAME);

    aether::RingHeader* hdr = aether::shm_create(SHM_NAME, CAPACITY);
    if (!hdr) {
        fprintf(stderr, "shm_create failed\n");
        return 1;
    }

    std::thread pub_thread(publisher, hdr);

    uint8_t buf[PAYLOAD_SIZE];
    uint32_t buf_len = PAYLOAD_SIZE;
    uint64_t read_seq = 1;
    uint64_t consumed = 0;
    uint64_t lapped = 0;
    uint64_t corrupted = 0;

    // Start both threads.
    g_start.store(true, std::memory_order_release);

    while (!g_pub_done.load(std::memory_order_acquire) || true) {
        buf_len = PAYLOAD_SIZE;
        aether::ConsumeResult r = aether::consume(hdr, buf, buf_len, read_seq);

        if (r == aether::ConsumeResult::Ok) {
            ++consumed;

            // Check payload integrity: all bytes should be the same value.
            const uint8_t expected = buf[0];
            for (uint32_t i = 1; i < buf_len; ++i) {
                if (buf[i] != expected) {
                    ++corrupted;
                    printf("  CORRUPTION at consumed=%llu: buf[0]=0x%02x buf[%u]=0x%02x\n",
                           static_cast<unsigned long long>(consumed), expected, i, buf[i]);
                    // Print first few bytes for context.
                    printf("  first 16 bytes:");
                    for (uint32_t j = 0; j < 16 && j < buf_len; ++j)
                        printf(" %02x", buf[j]);
                    printf("\n");
                    break; // one report per message is enough
                }
            }
        } else if (r == aether::ConsumeResult::Lapped) {
            ++lapped;
        } else {
            // Empty — publisher might be done.
            if (g_pub_done.load(std::memory_order_acquire))
                break;
        }
    }

    pub_thread.join();

    aether::shm_detach(hdr);
    aether::shm_destroy(SHM_NAME);

    printf("\nResults:\n");
    printf("  consumed:  %llu\n", static_cast<unsigned long long>(consumed));
    printf("  lapped:    %llu\n", static_cast<unsigned long long>(lapped));
    printf("  corrupted: %llu\n", static_cast<unsigned long long>(corrupted));

    if (corrupted > 0) {
        printf("\n  FAIL  consume() TOCTOU race triggered (%llu corruptions)\n",
               static_cast<unsigned long long>(corrupted));
        return 1;
    }

    printf("\n  PASS  no corruption detected (race may not have triggered)\n");
    return 0;
}
