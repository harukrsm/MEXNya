# Changelog

All notable changes to MEXNya are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Continuous integration for macOS and Ubuntu: configure, build and `scripts/test.sh`.
- `.clang-format` matching the project style, for new and edited code.
- `CONTRIBUTING.md`, `SECURITY.md` and this changelog.
- README instructions for building on macOS from Terminal, in English and Chinese.
- Core foundation: arenas, dynamic arrays, a hash map, string interning, path handling,
  matrix/quaternion/AABB/ray math, error state and a rotating log with an in-memory tail,
  covered by `core_test`.
- `MX_MEM_TRACK` (`-DMEXNYA_MEM_TRACK=ON`) allocation tracking and `mx_mem_report()`.
- `plat_write_file_atomic`, `plat_try_lock` and `plat_unlock`; `atomic_test` covers crashes
  mid-write, concurrent writers and stale locks.

### Changed

- `.gitignore` now also covers temporary files, crash logs and debug symbols.
- Project, scene, settings, snapshot and agent files are written atomically; `json_save`
  no longer deletes the target before renaming.
- The editor owns a project through `mexnya/.lock`: a second instance exits with a clear
  message, a lock left by a dead process is reclaimed, and agent operations keep working
  through the running editor.
- `mexnya/agent/log.jsonl` rotates at 10 MB and keeps one backup.

## [0.1.0] - 2026-11-05

First public snapshot: C99 engine, OpenGL 3.3 and software renderers, the editor,
Lua 5.4 scripting, the audio mixer, five interface languages, samples and tests.
