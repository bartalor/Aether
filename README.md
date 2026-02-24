# Aether

A high-performance local pub/sub message broker using shared memory as the data plane.

Producers publish to named topics. Subscribers receive them. The key differentiator: message data travels through shared memory — zero kernel copies, near-memory speed. No network stack, no socket overhead for the data path.

Real-world equivalents: ZeroMQ `ipc://` transport, LMAX Disruptor, Aeron.

> **Status:** Early development — core data structures defined, broker not yet functional.

---

## Components

| Component | Description |
|---|---|
| `libaether.so` | Client library — publisher and subscriber API |
| `aetherd` | Broker daemon — manages topics, shared memory segments, subscribers |
| `aether-cli` | Admin tool — connects via named pipe for live inspection and control |

---

## Architecture

Aether separates the **data plane** from the **control plane**:

- **Data plane** — POSIX shared memory ring buffers. Once a subscriber is set up, messages flow directly from producer to subscriber without any daemon involvement.
- **Control plane** — Unix domain sockets. Clients use these to subscribe/unsubscribe and manage topics.

### IPC mechanisms

| Mechanism | Role |
|---|---|
| POSIX shared memory | Data plane — zero-copy ring buffer per topic |
| Semaphores / futexes | Synchronize concurrent readers and writers |
| Unix domain sockets | Control plane — subscribe, unsubscribe, topic management |
| Signals | `SIGUSR1` stats dump, `SIGTERM` graceful drain and shutdown |
| POSIX message queues | Small control messages and fallback for tiny payloads |
| Named pipes (FIFOs) | Admin CLI interface |
| Threads | Acceptor thread, per-subscriber dispatch threads, housekeeping thread |

---

## Build

Requirements: GCC 13+ (or Clang 15+), CMake 3.20+, Linux.

```bash
cmake -B build
cmake --build build
```

Output binaries will be in `build/`.

---

## Future directions

- Lock-free ring buffer (replace semaphores with `std::atomic::wait()` / CAS)
- CPU affinity, false sharing analysis, huge pages (`MAP_HUGETLB`)
- Write-ahead log → replay missed messages (Kafka-style)
- Network bridge between two broker instances (distributed message bus)
- eBPF probes for observability
- Python bindings via pybind11
