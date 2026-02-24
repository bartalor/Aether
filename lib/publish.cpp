#include "aether/publish.h"

#include <cstring>  // memcpy
#include <cassert>

namespace aether {

bool publish(RingHeader* hdr, const void* data, uint32_t len) {
    assert(hdr != nullptr);
    assert(data != nullptr);

    // Fail fast — no silent truncation.
    if (len > SLOT_DATA_SIZE) {
        return false;
    }

    // Atomically claim the next sequence number.
    // fetch_add returns the old value — that becomes our sequence number.
    // memory_order_relaxed is sufficient here: we only need atomicity for
    // the counter itself. The release fence comes later on the slot's sequence store.
    const uint64_t seq = hdr->write_seq.fetch_add(1, std::memory_order_relaxed);

    // Map sequence number to a slot index.
    // The ring wraps: slot 0 is reused after `capacity` messages.
    Slot* slots = reinterpret_cast<Slot*>(hdr + 1);
    Slot& slot  = slots[seq % hdr->capacity];

    // Write the payload. These writes must complete before the sequence
    // store below — the sequence is the signal to subscribers that this
    // slot is ready to read.
    slot.payload_len = len;
    memcpy(slot.data, data, len);

    // Publish: store the sequence number with memory_order_release.
    // This is the fence — all writes above this line (payload_len, data)
    // are guaranteed to be visible to any subscriber that reads this
    // atomic with memory_order_acquire and sees the new value.
    slot.sequence.store(seq, std::memory_order_release);

    return true;
}

} // namespace aether