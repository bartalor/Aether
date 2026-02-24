#pragma once

#include "aether/ring.h"
#include <cstdint>

namespace aether {

// Write a message into the next available slot in the ring buffer.
//
// Thread-safe: multiple producers can call this concurrently — write_seq
// is incremented atomically so each producer gets a unique slot.
//
// Returns true on success.
// Returns false if len > SLOT_DATA_SIZE — payload too large, not written.
bool publish(RingHeader* hdr, const void* data, uint32_t len);

} // namespace aether