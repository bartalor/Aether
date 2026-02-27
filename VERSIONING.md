# Versioning

## Project Version

Aether uses [Semantic Versioning](https://semver.org):

| Bump | When |
|------|------|
| `PATCH` (`0.1.x`) | Bug fix, no API or layout changes |
| `MINOR` (`0.x.0`) | New feature, improvement, or any component layout version bumped |
| `MAJOR` (`x.0.0`) | Fundamental redesign — a paradigm shift at the scale of Python 2→3, not just a broken API |

The authoritative version lives in `CMakeLists.txt` as the `VERSION` field of the
`project()` command. It is the single source of truth — do not hardcode it anywhere else.

**The version signals behavioral and functional changes — not editorial ones.**
Only bump when the shipped artifact changes. If the binaries would be identical,
do not bump.

| Changed | Bump? |
|---------|-------|
| `lib/`, `daemon/`, `include/`, `cli/` | Yes |
| `tests/`, `bench/`, docs | No |
| Comments or whitespace in source files | No — comments don't exist in the binary |

## Git Tags

- Every release is tagged `v{MAJOR}.{MINOR}.{PATCH}` (e.g. `v0.1.0`)
- Tags are **annotated**: `git tag -a v0.1.0 -m "Release v0.1.0"`
- The tag is created on the same commit that bumps `VERSION` in `CMakeLists.txt`
- Do not tag before updating `CMakeLists.txt` and `CHANGELOG.md`

## Component Layout Versions

Some internal components own a separate layout version number that protects against
reading shared memory or on-disk data written by an incompatible build.

| Component | Location | Current Version |
|-----------|----------|-----------------|
| Ring buffer | `include/aether/ring.h` (`RING_VERSION`) | 1 |

**When to bump a component version:** when the binary representation of that
component changes in a backward-incompatible way — fields added, removed,
reordered, or resized.

Component versions are self-contained: the runtime detects a mismatch and
rejects the incompatible segment. A component version bump warrants at minimum
a `MINOR` project version bump and a prominent `CHANGELOG.md` entry.

This rule generalises to every future component that owns a layout version
(e.g. WAL format, network protocol). Each is independent and handles its own
compatibility at runtime.
