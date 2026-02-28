#pragma once

#include "transport.h"
#include "bench_common.h"

#include "aether/subscribe.h"
#include "aether/publish.h"
#include "aether/consume.h"

#include <csignal>
#include <sys/wait.h>

// ---------------------------------------------------------------------------
// AetherTransport — BenchTransport adapter for libaether
//
// Wraps aether::subscribe / publish / consume behind the harness interface.
// Owns the daemon lifecycle: setup() starts aetherd, teardown() kills it.
// ---------------------------------------------------------------------------

class AetherTransport {
public:
    explicit AetherTransport(const char* topic, uint32_t topic_len)
        : topic_(topic), topic_len_(topic_len) {}

    void setup() {
        daemon_pid_ = start_daemon();
        sub_        = aether::subscribe(topic_, topic_len_);
        read_seq_   = 1;
    }

    bool publish(const void* data, size_t len) {
        return aether::publish(sub_.hdr, data, static_cast<uint32_t>(len));
    }

    ConsumeStatus consume(void* buf, size_t& len) {
        uint32_t buf_len = static_cast<uint32_t>(len);
        const auto result = aether::consume(sub_.hdr, buf, buf_len, read_seq_);
        len = buf_len;  // write back actual bytes received
        switch (result) {
            case aether::ConsumeResult::Ok:     return ConsumeStatus::Ok;
            case aether::ConsumeResult::Empty:  return ConsumeStatus::Empty;
            case aether::ConsumeResult::Lapped: return ConsumeStatus::Lapped;
        }
        __builtin_unreachable();
    }

    void teardown() {
        aether::unsubscribe(sub_);
        kill(daemon_pid_, SIGTERM);
        waitpid(daemon_pid_, nullptr, 0);
    }

private:
    const char*        topic_;
    uint32_t           topic_len_;
    aether::Subscription sub_{};
    uint64_t           read_seq_ = 1;
    pid_t              daemon_pid_ = -1;
};

static_assert(BenchTransport<AetherTransport>,
    "AetherTransport does not satisfy BenchTransport concept");
