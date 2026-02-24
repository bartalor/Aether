#include "aether/consume.h"

#include <cstring>  // memcpy
#include <cassert>

namespace aether {

ConsumeResult consume(RingHeader* hdr, void* buf, uint32_t& buf_len, uint64_t& read_seq) {
    assert(hdr != nullptr);
    assert(buf != nullptr);

    Slot* slots = reinterpret_cast<Slot*>(hdr + 1);
    Slot& slot  = slots[read_seq % hdr->capacity];

    // Load the slot's sequence number with memory_order_acquire.
    // This is the other half of the release/acquire pair with publish().
    // If we see seq == read_seq, we are guaranteed to also see the payload
    // that was written before the producer's memory_order_release store.
    const uint64_t seq = slot.sequence.load(std::memory_order_acquire);

    if (seq == read_seq) {
        // Message is ready. Copy the payload out.
        const uint32_t msg_len = slot.payload_len;
        memcpy(buf, slot.data, msg_len);
        buf_len = msg_len;
        ++read_seq;
        return ConsumeResult::Ok;
    }

    if (seq < read_seq) {
        // Slot hasn't been written yet — producer hasn't reached this
        // sequence number. Nothing to read.
        return ConsumeResult::Empty;
    }

    // seq > read_seq: we were lapped. The producer has overwritten the slot
    // we were about to read (and possibly many slots beyond it).
    // Advance read_seq to the oldest message still in the ring:
    //   write_seq - capacity = the sequence number of the oldest live slot.
    // Load write_seq with relaxed ordering — we just need an approximate
    // value to catch up; the acquire on slot.sequence above is the real fence.
    const uint64_t write_seq = hdr->write_seq.load(std::memory_order_relaxed);
    read_seq = write_seq - hdr->capacity;
    return ConsumeResult::Lapped;
}

} // namespace aether
