#include "aether/shm.h"
#include "aether/publish.h"
#include "aether/consume.h"

#include <cstring>    // memcmp, memset
#include <cstdio>     // printf
#include <sys/mman.h> // shm_unlink (for pre-test cleanup)

// Simple test harness — no framework, just pass/fail counts.
static int passed = 0;
static int failed = 0;

static void check(const char* name, bool condition) {
    if (condition) {
        printf("  PASS  %s\n", name);
        ++passed;
    } else {
        printf("  FAIL  %s\n", name);
        ++failed;
    }
}

// The shm segment name used by this test.
// Must start with '/'. Cleaned up at the end of the test.
static constexpr const char* SHM_NAME = "/aether-test-ring";

int main() {
    printf("=== test_ring ===\n");

    // Clean up any leftover segment from a previous crashed run.
    // shm_unlink fails silently if it doesn't exist — that's fine.
    shm_unlink(SHM_NAME);

    // ------------------------------------------------------------------
    // 1. Create the ring
    // ------------------------------------------------------------------
    constexpr uint32_t CAPACITY = 16; // small ring — enough to test wrap-around

    aether::RingHeader* hdr = aether::shm_create(SHM_NAME, CAPACITY);
    check("shm_create returns non-null", hdr != nullptr);
    if (hdr == nullptr) {
        printf("Cannot continue — shm_create failed.\n");
        return 1;
    }

    check("magic is set correctly",   hdr->magic    == aether::RING_MAGIC);
    check("version is set correctly", hdr->version  == aether::RING_VERSION);
    check("capacity is set",          hdr->capacity == CAPACITY);
    // write_seq starts at 1 (not 0) — see shm_create for explanation.
    check("write_seq starts at 1",    hdr->write_seq.load() == 1);

    // ------------------------------------------------------------------
    // 2. Publish a message
    // ------------------------------------------------------------------
    const char* msg = "hello aether";
    const uint32_t msg_len = static_cast<uint32_t>(strlen(msg));

    bool pub_ok = aether::publish(hdr, msg, msg_len);
    check("publish returns true", pub_ok);
    check("write_seq incremented to 2", hdr->write_seq.load() == 2);

    // ------------------------------------------------------------------
    // 3. Consume the message
    // ------------------------------------------------------------------
    char buf[aether::SLOT_DATA_SIZE];
    uint32_t buf_len = sizeof(buf);
    // Consumers start read_seq at 1 — the initial value of write_seq.
    // Sequence 0 is never published, so it acts as the "unwritten" sentinel.
    uint64_t read_seq = 1;

    aether::ConsumeResult result = aether::consume(hdr, buf, buf_len, read_seq);
    check("consume returns Ok",             result  == aether::ConsumeResult::Ok);
    check("buf_len matches message length", buf_len == msg_len);
    check("payload content matches",        memcmp(buf, msg, msg_len) == 0);
    check("read_seq advanced to 2",         read_seq == 2);

    // ------------------------------------------------------------------
    // 4. Consume again — ring should be empty
    // ------------------------------------------------------------------
    buf_len = sizeof(buf);
    result = aether::consume(hdr, buf, buf_len, read_seq);
    check("second consume returns Empty", result == aether::ConsumeResult::Empty);
    check("read_seq unchanged after Empty", read_seq == 2);

    // ------------------------------------------------------------------
    // 5. Publish oversized message — should be rejected
    // ------------------------------------------------------------------
    char big[aether::SLOT_DATA_SIZE + 1];
    memset(big, 'x', sizeof(big));
    bool big_ok = aether::publish(hdr, big, sizeof(big));
    check("oversized publish returns false", !big_ok);
    check("write_seq not incremented after failed publish", hdr->write_seq.load() == 2);

    // ------------------------------------------------------------------
    // 6. Lapped detection
    // ------------------------------------------------------------------
    // Consumer is at read_seq=2. We need slot[2%16=2] to be overwritten.
    // That happens when seq=2+CAPACITY=18 is published (slot[18%16=2] overwrites it).
    // So we publish CAPACITY+1 messages (seq=2..18): write_seq goes from 2 to 19.
    for (uint32_t i = 0; i <= CAPACITY; ++i) {
        aether::publish(hdr, &i, sizeof(i));
    }
    buf_len = sizeof(buf);
    result = aether::consume(hdr, buf, buf_len, read_seq);
    check("lapped consumer gets Lapped result", result == aether::ConsumeResult::Lapped);
    check("read_seq advanced past old position", read_seq > 2);

    // ------------------------------------------------------------------
    // Cleanup
    // ------------------------------------------------------------------
    aether::shm_detach(hdr);
    aether::shm_destroy(SHM_NAME);

    // ------------------------------------------------------------------
    // Summary
    // ------------------------------------------------------------------
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
