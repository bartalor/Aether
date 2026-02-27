# Changelog

All notable changes to this project will be documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow the rules in [VERSIONING.md](VERSIONING.md).

## How to update this file

- Add entries to `[Unreleased]` as you work — don't batch them up at release time
- Sections within a version block: `Added`, `Changed`, `Fixed`, `Removed`
- Be specific: name the component, the behaviour, and the reason if non-obvious
- When tagging a release:
  1. Rename `[Unreleased]` to `[X.Y.Z] - YYYY-MM-DD`
  2. Add a new empty `[Unreleased]` section above it
  3. Update `VERSION` in `CMakeLists.txt`
  4. Commit, then `git tag -a vX.Y.Z -m "Release vX.Y.Z"`

---

## [Unreleased]

## [0.1.0] - 2026-02-27

### Added
- Ring buffer: lock-free shared-memory ring (`RingHeader`, `Slot`) with
  overwrite-oldest drop policy; `RING_VERSION = 1`
- Control plane: Unix domain socket daemon (`aetherd`) with
  subscribe/unsubscribe protocol and per-topic shm segment creation
- Signal handling in `aetherd`: `SIGTERM` for graceful shutdown,
  `SIGUSR1` for stats dump
- Client API (`libaether.so`): `subscribe()`, `unsubscribe()`,
  `publish()`, `consume()` with `ConsumeResult` (Ok / Empty / Lapped)
- Integration tests: control plane, multi-process pub/sub, lapped consumer,
  multiple subscribers on same topic, topic isolation, late subscriber
- Stress test: 4 concurrent publisher processes, 1 subscriber,
  per-publisher message ordering verified
- Benchmarks: `bench_latency` (p50 / p99 / p99.9 / p99.99 / max) and
  `bench_throughput` (M msgs/s) with automatic CSV report output
