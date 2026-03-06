# Vision

Aether is a high-performance pub/sub message broker inspired by Aeron.

## Core goal

Sub-microsecond latency, hundreds of millions of messages per second, zero kernel copies
on the local data path. Publishers and subscribers on the same machine communicate
directly through a POSIX shared memory ring buffer — the daemon is only involved at
connection time. Remote clients connect over TCP with the same pub/sub semantics.

## Transport layers

- **Local (IPC)**: POSIX shared memory. Zero kernel copies, zero network overhead.
  This is the fast path and is always preserved — network support never touches it.
- **Remote (TCP)**: The daemon accepts TCP connections from remote clients and bridges
  them to the local ring buffer. Same pub/sub API, different performance profile.
  UDP transport is a future optimization.

Like Aeron, local and remote are separate code paths — no abstraction tax on the
fast path. The wire protocol (message framing) is shared and transport-agnostic,
so swapping TCP for UDP later requires only a new transport implementation.

## Architecture

- **Data plane (local)**: POSIX shared memory ring buffer per topic. Lock-free.
- **Data plane (remote)**: TCP socket with length-prefixed wire protocol.
- **Control plane (local)**: Unix domain socket handshake at connection time.
- **Control plane (remote)**: Wire protocol Subscribe message over TCP.
- **Client library** (`libaether.so`): local pub/sub API (shm) and remote client API (TCP).
- **Daemon** (`aetherd`): topic registry, shm segment management, TCP server for remote clients.
- **CLI** (`aether-cli`): admin tool — pub, sub, stats, shutdown.

## Performance targets

- Throughput: 100M+ messages/sec (single producer, single consumer, local IPC)
- Latency: sub-microsecond on the local hot path (publish + consume, same machine)
- Zero heap allocation on the local hot path

## Roadmap

1. ~~Local happy path — client API, end-to-end pub/sub~~ **done**
2. ~~Benchmark harness — transport-agnostic, repeatable~~ **done**
3. ~~CLI tool — pub, sub, stats, shutdown~~ **done**
4. ~~Seqlock fix — TOCTOU race in consume()~~ **done** (v0.1.1)
5. **TCP transport** — remote pub/sub over TCP *(in progress)*
   - Wire protocol (length-prefixed framing)
   - Daemon TCP server (bridge remote clients to local ring)
   - Remote client API in libaether.so
6. Lock-free multi-producer — CAS-based ring buffer for concurrent publishers
7. Aeron comparison — `aether-benchmarks` companion repo, head-to-head benchmarks
8. Observability — eBPF probes, per-topic latency histograms, metrics endpoint
9. Persistence / WAL — optional replay of missed messages (opt-in per topic)
10. Aeron-style term buffers — replace fixed-slot ring with rotating append-only logs
    (variable-length messages, eliminates seqlock complexity)
