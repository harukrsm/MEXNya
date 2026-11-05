# HANDOFF — foundation (T0–T3)

This file records what the engine foundation work changed, how it was verified,
and what the next task needs to know. It follows the task plan supplied with this work.

## Completed

### T0 · repository baseline

- `.github/workflows/ci.yml`: macOS and Ubuntu configure, build and run `scripts/test.sh`.
- `.clang-format` (LLVM base, 4 spaces, 120 columns, attached braces, includes unsorted).
- `CHANGELOG.md`, `SECURITY.md`, `CONTRIBUTING.md`; `.gitignore` covers build output,
  temporary files, crash logs and debug symbols.
- README (English and Chinese): macOS build from Terminal, release build and offline SDL3.
- The `editor` section of the root `mexnya.json` is still read by `editor/tools.c`
  (Build page → Run), so it was kept. The task description assumed it was dead.
- SPDX headers were not added: the project owner asked for no comments, and the GPL
  notice in `LICENSE` already covers the tree.

### T1 · core foundation

- `engine/core/mem.[ch]`: aligned arenas (with growth), per-frame arena, tracked
  allocation (`MX_MEM_TRACK`), `mx_mem_report()`.
- `engine/core/vec.h`: `MX_VEC(T)`, push/reserve/remove-swap/clear/free.
- `engine/core/map.[ch]`: FNV-1a 64, open addressing with tombstones, 0.7 load factor.
- `engine/core/str.[ch]`: stable interned strings.
- `engine/core/path.[ch]`: join/normalize/subpath/extension, `/` and `\`, drive letters.
- `engine/core/math.[ch]`: v2/v3/v4, mat3/mat4 (column-major, shared with `RMat4`),
  quaternions, TRS compose/decompose, inverse, AABB, ray/AABB.
- `engine/core/error.[ch]`: error codes, thread-local message, `MX_CHECK`.
- `engine/core/log.[ch]`: levels, mutex, size rotation (8 MB / 2 files by default),
  400-line ring buffer for the editor.
- `engine/render/gl/gl_render.c` now uses `mx_mat4_identity` / `mx_mat4_mul`; the
  arithmetic is the same and `RMat4` is a `MxMat4` typedef.
- `tests/core_test.c`, run from `scripts/test.sh`.

### T2 · durable writes and project ownership

- `plat_write_file_atomic` (sibling temp file, flush, fsync/`_commit`, rename),
  `plat_try_lock` / `plat_unlock` (exclusive create, pid recorded, stale lock reclaimed).
- `json_save` is atomic and no longer removes the target first.
- Project, scene, settings, snapshot, agent guide/heartbeat and generated script files
  are written atomically.
- The editor owns `mexnya/.lock` for its lifetime. A second instance exits with a clear
  message; `mexnya agent` keeps forwarding to the running editor.
- `mexnya/agent/log.jsonl` rotates at 10 MB, keeping `log.jsonl.1`.
- `tests/atomic_test.c` covers a process killed mid-write, concurrent writers and locks.

### T4 · world (in progress)

- `engine/world/world.[ch]`: generational entity handles, a free list, interned-name
  lookup through a hash chain, sparse-set component storage, parent/child transforms,
  `mx_world_tick` (prev/current state) and `mx_world_update_transforms`.
- `engine/world/components.h`: transform, mesh renderer, collider, rigid body, script,
  audio source, camera, light and sprite layouts.
- `engine/world/instantiate.[ch]`: `mx_world_from_scene` maps every editor object type
  (box/plane/sphere/cylinder/cone/model/light/rect/panel/button/text/image);
  `mx_world_to_json` writes the same fields back, including the editor's Ry*Rx*Rz
  rotation convention.
- `engine/render/primitives.[ch]`: the primitive meshes and flat lighting moved out of
  `scene.c`, shared by the scene and world renderers. The hello screenshot is byte-identical
  before and after the move.
- `engine/world/draw.[ch]`: `mx_world_draw_3d` (primitives and cached .mcm models) and
  `mx_world_draw_2d` (rect/panel/button/text sprites).
- `scene.[ch]` gains `mx_project_draw_2d`, so a game can draw its own 3D and still get the
  2D layers.
- `samples/hello` and `templates/game` instantiate the main scene into a world, drive the
  player's `MxTransform`, and draw the world. The rendered frame matches the previous
  scene-rendered frame exactly (0 differing pixels at 1280×720).
- `tests/world_test.c`: 100k entities, reuse and dangling handles, 100k name lookups
  (under 100 ms), component add/get/remove, parent/child/grandchild/rotation cases, and
  scene → world → JSON → world round-trips (embedded scene and `samples/hello`).
- Scene fog settings are not part of the world; the sample keeps fog disabled. A future
  world camera/settings component should carry them.

### T5 · savegames and settings

- `engine/game/save.[ch]`: `mx_save_write` / `mx_save_read` / `mx_save_delete` /
  `mx_save_exists`, slot paths under `<user>/saves/<slot>.mxs`, `MXSV` v1, little-endian
  payload, CRC-32, atomic writes, entity order deterministic, parent links restored.
- Loading parses into a temporary world and swaps on success (`mx_world_swap`), so a
  corrupt, truncated or unknown-version file does not touch the current world.
- `engine/game/settings.[ch]`: JSON settings with `schema_version` and recursive defaults.
- `tests/save_test.c`: full component round-trip, byte-identical repeated saves, bad
  version, bad CRC, truncation, slot path rejection and two concurrent writers.
- No v1→v2 migration exists yet because v1 is the first format; unknown versions return
  `MX_ERR_FORMAT` with a message. Add the migration chain when the format changes.

### T13 · packaging (macOS only)

- `scripts/package_macos.sh` builds a Release tree if needed and assembles
  `dist/MEXNya.app` (editor) and `dist/hello.app` (sample). Resources go to
  `Contents/Resources`; `SDL_GetBasePath` resolves there for bundles.
- Info.plist carries the version from `project(MEXNya VERSION ...)`, the bundle id and
  `NSHighResolutionCapable`; `codesign --force --deep` uses `-` unless
  `MEXNYA_CODESIGN_ID` is set, then `codesign --verify --deep --strict` must pass.
- Verified: both apps start from the bundle, the editor returns `project.get`, the sample
  renders a PNG headlessly, and `otool -L` shows only system frameworks.
- Not done: notarization, Windows/Linux artifacts, CI upload.

### T3 · fixed timestep

- `EngineConfig` gains `fixed_hz`, `max_catchup` and `pause_on_focus_lost` (appended,
  so existing initialisers keep working and default to the old variable step).
- `engine_loop_ex` adds `EngineTickFn`/`EngineRenderFn` with interpolation alpha;
  `engine_loop` forwards to it and keeps its signature.
- `engine_step` advances one frame from an explicit `dt`; `engine_reset_time` resets
  ticks, hash and accumulator.
- `engine_set_time_scale`, `engine_tick_count`, `engine_tick_hash`, `engine_frame_alpha`,
  `engine_frame_stats` (input/tick/render/present milliseconds).
- Input is polled once per frame; edges are cleared between sub-steps so one press cannot
  fire twice.
- `tests/timestep_test.c` covers 30/60/144 fps, determinism, pause, catch-up and alpha.

## Verification

```text
$ cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build/debug -j8
zero warnings
$ bash scripts/test.sh
boundary check OK
i18n check OK (167 keys, 5 languages)
runtime test OK
core_test: all checks passed
atomic_test: all checks passed
timestep_test: all checks passed
engine smoke OK
editor operations OK
sample game OK
all tests OK
```

A tracked build (`-DMEXNYA_MEM_TRACK=ON`) also passes `core_test` with no live
allocations at exit. A from-scratch build in `build/clean` re-fetches SDL3 and passes
the same suite.

## Known gaps

- T13 is partial: macOS bundles exist, but notarization, Windows/Linux artifacts and CI
  uploads do not.
- Not implemented: physics (T6), RGBA8 textures (T7), batching (T8), audio buses (T9),
  input expansion (T10), assets (T11), Lua components (T12), play-in-editor (T14),
  migration guide and performance gates (T15).
- The Ubuntu CI job is new and has not been run on this machine; it installs the SDL3
  build dependencies and runs the same suite.
- `str` interning and the tracked allocator are single-threaded by design.
- Atomic temp files use `pid` plus a stack address; a crash can leave one behind.

## Conventions for the next task

- Public headers stay free of SDL, OpenGL and Lua types.
- New code writes files through `plat_write_file_atomic`, paths through `mx_path_*`,
  logging through `mx_log` and allocation through `mx_mem_*`.
- `RMat4` is `MxMat4`; do not add a second matrix type.
- `fixed_hz` is opt-in; the editor and existing tools keep the variable step.
