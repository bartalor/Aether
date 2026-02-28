#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// BenchTransport — transport interface for the benchmark harness
//
// Modelled after Aeron's MessageTransceiver pattern: a single object owns
// both the publisher and subscriber endpoints. The harness (LoadTestRig
// equivalent) only talks to this interface — it has no knowledge of Aether,
// Aeron, or any specific transport.
//
// Design note — why setup() is a single call covering both directions:
//   The harness uses fork() to run publisher (parent) and subscriber (child)
//   as separate processes. After fork(), each process has its own memory.
//   Both endpoints must therefore exist in shared memory *before* the fork so
//   both processes inherit access. A single setup() enforces this. Splitting
//   into open_pub() / open_sub() would require the harness to know to call
//   both before forking — leaking transport topology into harness logic.
// ---------------------------------------------------------------------------

// Status returned by consume(). Transports with no lapping concept
// (e.g. MutexTransport) simply never return Lapped.
enum class ConsumeStatus {
    Ok,     // message received; buf/len filled
    Empty,  // no message available yet
    Lapped, // consumer fell behind, message(s) lost; internal state advanced
};

// A type T satisfies BenchTransport if it provides these four operations.
template<typename T>
concept BenchTransport = requires(T t, const void* data, void* buf, size_t len) {
    // Prepare both endpoints in shared memory. Must be called before fork().
    { t.setup()            } -> std::same_as<void>;

    // Publish len bytes from data. Returns false if payload is too large.
    { t.publish(data, len) } -> std::same_as<bool>;

    // Try to consume one message into buf. len is buffer capacity on entry,
    // bytes written on Ok. Transport owns its read position internally.
    { t.consume(buf, len)  } -> std::same_as<ConsumeStatus>;

    // Release all resources acquired in setup().
    { t.teardown()         } -> std::same_as<void>;
};
