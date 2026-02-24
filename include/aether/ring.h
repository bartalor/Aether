#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>

namespace aether {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// Maximum bytes a single message payload can occupy in a slot.
// 4096 = one memory page — a natural allocation unit.
// If a payload exceeds this, publish() returns an error. No silent truncation.
constexpr std::size_t SLOT_DATA_SIZE = 4096;

// Written into RingHeader::magic on initialisation.
// If we open a shm segment and the magic doesn't match, the segment is
// stale, corrupt, or belongs to a different program — reject it.
constexpr uint64_t RING_MAGIC = 0xAE7E4000DEADC0DE;

// Bump this if the layout of RingHeader or Slot ever changes incompatibly.
constexpr uint32_t RING_VERSION = 1;

// ---------------------------------------------------------------------------
// Slot — one entry in the ring buffer
// ---------------------------------------------------------------------------

// alignas(64): each slot is aligned to a 64-byte cache line boundary.
// This prevents two adjacent slots from sharing a cache line, which would
// cause false sharing between the producer and any subscriber touching them.
struct alignas(64) Slot {
    // The producer sets this AFTER writing payload_len and data[].
    // A subscriber waiting on slot index i polls this value:
    //   - sequence == expected_seq  → message is ready, safe to read
    //   - sequence <  expected_seq  → slot not yet written (subscriber is ahead)
    //   - sequence >  expected_seq  → subscriber was lapped (message overwritten)
    std::atomic<uint64_t> sequence;

    // How many bytes of data[] are actually used by this message.
    // Always <= SLOT_DATA_SIZE.
    uint32_t payload_len;

    // Raw message bytes. Only the first payload_len bytes are valid.
    uint8_t data[SLOT_DATA_SIZE];
};

// ---------------------------------------------------------------------------
// RingHeader — lives at offset 0 of the shared memory segment
// ---------------------------------------------------------------------------

// Memory layout of the full shm segment:
//
//   [ RingHeader (aligned to 64 bytes) ]
//   [ Slot 0 ][ Slot 1 ] ... [ Slot N-1 ]
//
// The broker creates this segment; publishers and subscribers map it read/write.

struct alignas(64) RingHeader {
    // Sanity check: detect stale or foreign shm segments on attach.
    uint64_t magic;

    // Layout version: reject segments written by an incompatible binary.
    uint32_t version;

    // Number of slots in the ring. Set once at creation, never changed.
    uint32_t capacity;

    // Monotonically increasing counter. The producer increments this to
    // claim the next slot to write into.
    // slot index = write_seq % capacity
    std::atomic<uint64_t> write_seq;
};

// ---------------------------------------------------------------------------
// Static assertions — catch layout problems at compile time
// ---------------------------------------------------------------------------

// std::atomic<uint64_t> must be lock-free for shared memory to be safe.
// If this fails, the platform doesn't support what we need.
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "std::atomic<uint64_t> must be lock-free on this platform");

} // namespace aether
