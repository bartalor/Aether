# Aether — Instructions for Claude

## Versioning Rules

Read VERSIONING.md before touching any version number. Key points:

- The authoritative project version is the `VERSION` field in `CMakeLists.txt`
- The version signals behavioral and functional changes — not editorial ones.
  Only bump when the shipped artifact changes. Docs, tests, benchmarks, and
  comment-only edits to source files do not bump the version.
- Bump `PATCH` for bug fixes, `MINOR` for new features or any component layout
  version bump, `MAJOR` only for a fundamental redesign (rare — think Python 2→3)
- `RING_VERSION` in `include/aether/ring.h` is the ring buffer layout version.
  Bump it only when `RingHeader` or `Slot` binary layout changes incompatibly.
  A `RING_VERSION` bump requires at minimum a `MINOR` project version bump.
- Future components with their own layout versions follow the same rule.

## Git Tag Rules

After bumping the version in `CMakeLists.txt`:
1. Update `CHANGELOG.md` (rename `[Unreleased]` → `[X.Y.Z] - YYYY-MM-DD`, add new `[Unreleased]`)
2. Commit both files together
3. Tag: `git tag -a vX.Y.Z -m "Release vX.Y.Z"`

Never tag without first updating both `CMakeLists.txt` and `CHANGELOG.md`.

## Changelog Rules

- Every user-visible change goes in `CHANGELOG.md` under `[Unreleased]` as you work
- Sections: `Added`, `Changed`, `Fixed`, `Removed`
- Be specific: name the component and describe the behaviour
- Do not batch entries at release time — add them as changes are made

## Benchmark Reports

- Benchmarks write CSV rows to `reports/bench_latency.csv` and
  `reports/bench_throughput.csv` (append mode, header written on first run)
- `reports/` is gitignored — never commit it
- Each CSV row includes: `timestamp`, `aether_version`, `ring_version`,
  and all metric columns for that benchmark
