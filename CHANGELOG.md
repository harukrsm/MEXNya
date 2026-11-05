# Changelog

All notable changes to MEXNya are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `scripts/package_macos.sh`: Release build, `MEXNya.app` and `hello.app` bundles with
  Info.plist and ad-hoc (or `MEXNYA_CODESIGN_ID`) signing, verified by running both apps
  headless from the bundle.
- Explicit engine source lists (no configure-time globbing), `CMakePresets.json` for
  `dev`/`release`/`ci`, and a `MEXNYA_WERROR` option that CI turns on.
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
- Versioned binary savegames (`MXSV`, CRC-32, atomic writes, parent links preserved) and
  JSON settings with schema version and recursive default merging; `save_test` covers
  round-trips, determinism, corrupt/truncated/unknown-version files and concurrent writers.
- World layer: generational entity handles, interned names with O(1) lookup, sparse-set
  components (transform, mesh, collider, rigid body, script, audio source, camera, light,
  sprite), parent/child transforms with world matrices and per-tick interpolation state;
  scene instantiation and JSON export for every editor object type; a world renderer shared
  with the scene runtime; `world_test` covers 100k entities, dangling handles, name lookup,
  hierarchies and scene round-trips. The hello sample and game template now run on the
  world layer.
- Fixed-timestep loop (`fixed_hz`, `max_catchup`, `pause_on_focus_lost`), render interpolation
  alpha, time scaling, a deterministic tick hash and per-phase frame timings. `engine_loop`
  keeps its signature; `engine_loop_ex` and `engine_step` expose the new behaviour, and
  `timestep_test` checks ticks at 30/60/144 fps, determinism and pause.

### Changed

- `.gitignore` now also covers temporary files, crash logs and debug symbols.
- Project, scene, settings, snapshot and agent files are written atomically; `json_save`
  no longer deletes the target before renaming.
- The editor owns a project through `mexnya/.lock`: a second instance exits with a clear
  message, a lock left by a dead process is reclaimed, and agent operations keep working
  through the running editor.
- `mexnya/agent/log.jsonl` rotates at 10 MB and keeps one backup.
- With a fixed timestep, input edges are consumed by the first sub-step of a frame so one
  press cannot fire twice.

## [0.1.0] - 2026-11-05

First public snapshot: C99 engine, OpenGL 3.3 and software renderers, the editor,
Lua 5.4 scripting, the audio mixer, five interface languages, samples and tests.
