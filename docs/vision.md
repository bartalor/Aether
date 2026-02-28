# Vision

Aether is a high-performance local pub/sub message broker inspired by Aeron.

## Core goal

Sub-microsecond latency, hundreds of millions of messages per second, zero kernel copies
on the data path. The data plane is POSIX shared memory — publishers and subscribers
communicate directly through a lock-free ring buffer without involving the kernel or the
daemon after the initial handshake.

## Transport layers

The fast path is local: processes on the same machine communicate through POSIX shared
memory — zero kernel copies, zero network overhead. This is the primary transport and
where the performance story lives.

Cross-machine communication is a future extension: a network bridge forwards topics
between broker instances over TCP or RDMA. The local data plane remains unchanged —
the bridge is a separate process that subscribes locally and republishes remotely.

## Architecture

- **Data plane**: POSIX shared memory ring buffer per topic. Zero-copy, lock-free.
- **Control plane**: Unix domain socket. Used once at startup to negotiate topic names.
  After that the daemon is out of the picture.
- **Client library** (`libaether.so`): publisher and subscriber API.
- **Daemon** (`aetherd`): manages topic registry, creates shm segments on demand.
- **CLI** (`aether-cli`): admin tool for inspecting topics, stats, shutdown.

## Performance targets

- Throughput: 100M+ messages/sec (single producer, single consumer, small payloads)
- Latency: sub-microsecond on the hot path (publish + consume, same machine)
- Zero heap allocation on the hot path

## Roadmap

1. ~~Complete the happy path — client API, end-to-end pub/sub working~~ **done**
2. ~~Benchmarks — real numbers, repeatable, tracked over time~~ **done**
   - Transport-agnostic harness (modelled after Aeron's `LoadTestRig` / `MessageTransceiver`)
   - `AetherTransport` adapter; harness ready for additional transports
3. Lock-free multi-producer — CAS-based ring buffer for concurrent publishers
   - Precondition for fair comparison against Aeron (which is multi-producer by design)
4. Aeron comparison — `aether-benchmarks` companion repo, `AeronTransport` adapter
   - Separate repo so Aeron is never a dependency of the main project
   - Apples-to-apples: same harness, same methodology, both lock-free IPC
5. Observability — eBPF probes, per-topic latency histograms, metrics endpoint
6. Persistence / WAL — optional replay of missed messages (Kafka concept, opt-in)
7. Network bridge — forward topics to a remote broker instance over TCP/RDMA