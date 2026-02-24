#pragma once

#include "aether/ring.h"
#include <cstdint>

namespace aether {

// Result of a consume() call.
enum class ConsumeResult {
    Ok,     // message successfully read into buf, read_seq incremented
    Empty,  // no new message yet — slot not written, call again later
    Lapped, // subscriber fell too far behind, messages were overwritten.
            // read_seq has been advanced to the oldest available message.
            // caller decides whether to continue or treat this as an error.
};

// Attempt to read the next message from the ring buffer.
//
// `hdr`      — pointer to the mapped RingHeader
// `buf`      — caller-provided buffer to copy the message into
// `buf_len`  — in: capacity of buf (bytes). out: actual bytes written on Ok.
// `read_seq` — subscriber's position in the ring. Caller owns this value
//              and must preserve it between calls. Start at 0.
//
// On Ok:     buf contains the message, buf_len is set to payload size, read_seq incremented.
// On Empty:  buf and buf_len unchanged, read_seq unchanged.
// On Lapped: read_seq advanced to oldest available message, buf unchanged.
//            Call consume() again immediately to read from the new position.
ConsumeResult consume(RingHeader* hdr, void* buf, uint32_t& buf_len, uint64_t& read_seq);

} // namespace aether
