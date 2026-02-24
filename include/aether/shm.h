#pragma once

#include "aether/ring.h"
#include <cstdint>
#include <cstddef>

namespace aether {

// ---------------------------------------------------------------------------
// Segment size calculation
// ---------------------------------------------------------------------------

// Returns the total number of bytes needed for a shm segment that holds
// one RingHeader followed by `capacity` Slots.
// This is what we pass to ftruncate() when creating the segment.
constexpr std::size_t shm_segment_size(uint32_t capacity) {
    return sizeof(RingHeader) + capacity * sizeof(Slot);
}

// ---------------------------------------------------------------------------
// Lifecycle functions
// ---------------------------------------------------------------------------

// Create a new named shm segment, map it into this process, and initialise
// the RingHeader (magic, version, capacity, write_seq = 0).
// Also initialises each Slot's sequence number to its index (i), so that
// a subscriber waiting for sequence 0 will correctly see slot 0 as not-yet-written.
//
// `name`     — POSIX shm name, must start with '/' (e.g. "/aether-prices")
// `capacity` — number of Slots in the ring; must be a power of two (not enforced here)
//
// Returns a pointer to the mapped RingHeader on success, nullptr on failure.
// On failure, errno is set by the failing syscall.
RingHeader* shm_create(const char* name, uint32_t capacity);

// Open an existing named shm segment and map it into this process.
// Validates that magic == RING_MAGIC and version == RING_VERSION before
// returning — rejects stale or incompatible segments.
//
// Returns a pointer to the mapped RingHeader on success, nullptr on failure.
RingHeader* shm_attach(const char* name);

// Unmap a previously created or attached segment from this process's
// address space. Does NOT delete the segment — other processes keep access.
// After this call, `hdr` is invalid and must not be used.
void shm_detach(RingHeader* hdr);

// Delete the named shm segment. The segment continues to exist (and remain
// mapped) in any process that already has it attached, but no new process
// can open it by name after this call.
// Typically called by the daemon on shutdown.
void shm_destroy(const char* name);

} // namespace aether
