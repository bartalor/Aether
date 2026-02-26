#pragma once

#include "aether/ring.h"
#include <cstddef>
#include <cstdint>

namespace aether {

// Handle returned by subscribe(). Passed to consume() and unsubscribe().
struct Subscription {
    RingHeader* hdr;       // pointer to the mapped ring buffer
    size_t      map_size;  // total size of the mapping — needed for munmap()
};

// Connect to the daemon, look up or create the shm segment for `topic`,
// and map it into this process's address space.
//
// `topic`     — topic name (not null-terminated; length given by `topic_len`)
// `topic_len` — length of topic name in bytes, must be <= MAX_TOPIC_LEN
//
// Returns a Subscription on success.
// Terminates (assert/abort) on any error — fail fast.
Subscription subscribe(const char* topic, uint32_t topic_len);

// Unmap the shm segment. After this call, `sub.hdr` is invalid.
void unsubscribe(Subscription& sub);

} // namespace aether
