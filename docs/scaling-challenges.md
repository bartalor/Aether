# Engineering Challenges

Known gaps and hard problems on the path to a production-grade Aeron-like system.
These are not hypothetical — they are the real work ahead.

---

## Correctness Under Concurrency

The ring buffer currently assumes a single producer per topic. Two concurrent publishers
will corrupt each other's slots — both call `fetch_add` on `write_seq`, claim different
slot indices, but there's no guarantee their writes don't interleave at the slot level.

Fix: CAS-based lock-free multi-producer ring buffer (as in LMAX Disruptor / Aeron).
This is roadmap item 3 and is the most technically demanding piece.

---

## Slow / Dead Subscribers

A subscriber that stops reading will get lapped — the producer silently overwrites its
unread slots. The system currently has no way to detect or handle this.

Aeron's policy: **drop**. The publisher is never blocked by a slow subscriber.
Flow control is the publisher's responsibility. This is our chosen direction —
it preserves the non-blocking guarantee on the hot path.

---

## Subscriber Tracking

The daemon hands out shm names but keeps no record of who attached. If a subscriber
crashes, the daemon doesn't know. No cleanup, no notification, no reconnect logic.

Fix: daemon maintains a subscriber list per topic; detects disconnects via socket
keepalive or heartbeat.

---

## Backpressure

Current policy: none. If a publisher produces faster than consumers can consume,
messages are silently dropped (lapped). This is intentional — consistent with Aeron.

If blocking backpressure is ever needed, it requires a per-subscriber read cursor
visible to the producer, and a producer that checks it before overwriting. This
fundamentally changes the ring buffer design and kills the lock-free guarantee.

---

## Persistence / Replay

Messages exist only in the ring buffer. A subscriber that starts after messages were
published cannot replay them. If the daemon restarts, all messages are lost.

This is an optional future extension (roadmap item 5) — a write-ahead log (WAL)
appended before each ring write. Opt-in per topic, not the default.

---

## Network Transport

The data plane is POSIX shared memory — local to one machine. Crossing a machine
boundary requires a different transport (TCP, RDMA).

Roadmap item 6: a bridge process subscribes locally and re-publishes over the network
to a remote broker instance. The local data plane is unchanged.

---

## Observability

Currently: `SIGUSR1` dumps stats to stderr. That is the entire observability story.

Roadmap item 4:
- eBPF probes on the ring buffer hot path (zero-overhead tracing)
- Per-topic latency histograms
- Prometheus-compatible metrics endpoint
- CLI (`aether-cli`) for live inspection

---

## Summary

The hard part is not the happy path — it's correctness under concurrency, observable
failure modes, and predictable behaviour under load. The current implementation covers
the happy path. The items above are where the real engineering depth lives.
