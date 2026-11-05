# Architecture

MEXNya is a C99 engine, an editor built on the same engine, and a project format
shared by people and agents. This page describes the layers, the frame, ownership
and the rules that keep the pieces separable.

## Layers

| Layer | Where | Depends on |
| --- | --- | --- |
| Core | `engine/core/` — JSON, i18n, PNG writer, memory, containers, paths, math, errors, logging | libc |
| Platform | `engine/platform/` — lifecycle, clock, files, threads, window, input, audio device | SDL3 (desktop backend only) |
| Render | `engine/render/` — intent-level API, batching front end, font, models, UI | platform, OpenGL (GL backend only) |
| World | `engine/world/` — entities, components, transform hierarchy, scene instantiation | core |
| Game | `engine/game/` — binary savegames, settings | core, world, platform |
| Scene | `engine/scene/` — read-only JSON scenes, drawing, ports | core, render |
| Audio | `engine/audio/` — software mixer and MP3 decode | platform, minimp3 |
| Console | `engine/console/` — stdin commands and Lua | platform, Lua |
| Editor | `editor/` — operation layer, pages, agent bridge | engine |
| Games | `samples/`, `templates/game/` | engine |

Only `engine/platform/desktop/` and `engine/render/gl/` may include SDL or OpenGL.
Only `engine/console/lua_console.c` and `editor/script.c` may include Lua.
`scripts/check_boundaries.py` enforces both rules, including that no public engine
header mentions a backend type.

## The frame

`engine_loop_ex` drives one frame at a time:

1. `console_poll()` reads console input.
2. `input_poll()` updates the semantic input snapshot. Games read it with
   `input_get_last()`; it does not change until the next frame.
3. With `fixed_hz > 0`, real time is scaled by `engine_time_scale()` and added to an
   accumulator capped at 0.25 s. Ticks of exactly `1 / fixed_hz` run until the
   accumulator is empty or `max_catchup` is reached; a remaining backlog is dropped
   with one warning instead of spiralling. Input edges are cleared between sub-steps.
4. The game renders with `alpha = accumulator / fixed_dt`, which can be used to
   interpolate between the previous and current transform.
5. `gfx_begin_3d()` → draw → `gfx_begin_2d()` → `gfx_finish()` → `gfx_present()`.

`fixed_hz = 0` keeps the original variable-step behaviour (one update per frame, dt
clamped to 100 ms) and is what the editor and tools use. `engine_step()` performs the
same work for an explicit dt, which is how determinism is tested.

Frame phase timings are available from `engine_frame_stats()`; the tick counter and
FNV-1a tick hash come from `engine_tick_count()` and `engine_tick_hash()`.

## World

`MxWorld` owns entities as `{index, generation}` handles. A generation bump on destroy
makes stale handles fail `mx_entity_alive` and return `NULL` from `mx_comp_get`. Names
are interned strings, so a name lookup is one hash and a pointer comparison.

Components live in sparse sets (dense data array plus an entity-indexed sparse array).
`MxTransform` is stored per entity slot and is always present. Parent links form the
hierarchy; `mx_world_update_transforms` resolves parents before children in one pass and
writes `local` and `world` matrices. `mx_world_tick` copies the current transform to the
`prev_*` fields before the update so rendering can interpolate with the frame alpha.

`mx_world_from_scene` builds a world from an editor scene document: object types become
mesh, light or sprite components, `pos/rot/scale` become the transform, and colours are
converted from `#RRGGBB`. `mx_world_to_json` is the inverse. The editor's rotation
convention is `Ry * Rx * Rz`, so instantiation multiplies quaternions in that order.

`mx_world_draw_3d` draws transformed primitives through the shared
`engine/render/primitives` meshes (models go through the .mcm cache), and
`mx_world_draw_2d` draws sprites. `mx_project_draw_2d` draws only the editor's 2D layers,
which lets a game render its world in the 3D pass and still get the UI layers on top.

## Savegames

A save is `MXSV`, a 32-bit version, flags, a little-endian payload and a CRC-32.
Entities are written in slot order, so the same world produces the same bytes. Each
record stores the entity's original index (for parent links), name, type, transform,
parent and component fields. Loading parses into a temporary world, verifies the whole
payload first, and only swaps it into the caller on success; a corrupt or unknown-version
file leaves the current world untouched. Files are written through
`plat_write_file_atomic`, so a crash keeps the previous save intact.

## Memory

- `MxArena` hands out aligned slices and grows by adding blocks; `mx_arena_reset`
  rewinds every block for reuse. `mx_frame_alloc` is a per-frame arena reset by the
  owner (the engine does not reset it implicitly).
- `MX_VEC(T)` is a typed dynamic array with `mx_vec_push` (amortised O(1), the old
  array survives a failed grow).
- `MxMap` is an open-addressing hash table with linear probing, tombstones and a 0.7
  load factor.
- `mx_str_intern` returns process-lifetime pointers. The table is deliberately not
  thread-safe.
- `mx_mem_alloc` and friends route through tracking when the engine is configured with
  `-DMEXNYA_MEM_TRACK=ON`; `mx_mem_report()` prints live blocks grouped by file:line.

## Files and ownership

- A project lives in `mexnya/`: `project.json`, `scenes/*.json`, `NOTES.md`, agent
  inbox/outbox/log and snapshots. `mexnya.json` at the project root describes build
  targets and packages.
- Every durable write uses `plat_write_file_atomic`: a sibling temporary file, flush,
  fsync (or `_commit`), then rename. Readers therefore see the old file or the new
  one, never a partial write.
- One editor owns a project through `mexnya/.lock`. The file records the process id;
  a lock whose process is gone is reclaimed. Headless operations (`--exec`) take the
  same lock, so two writers cannot interleave.
- `json_save` writes through the atomic path; project saves keep their previous
  contents if the write fails.

## Threads

The engine is single-threaded on the main thread except for the platform audio
callback, which must not allocate or do file I/O. `mx_log` serialises writes with a
mutex. Nothing else in the core layer takes a lock, by design; keep new subsystems on
the main thread unless they have their own explicit synchronisation.

## Tests

`scripts/test.sh` runs, in order: dependency boundaries, interface strings, the
runtime test, the core test, the atomic/lock test, the timestep test, a headless
engine smoke run, the editor operation layer and the sample game. Every executable
runs headless, so CI needs no display or sound card.

## Extending

- Add a core utility under `engine/core/`, a test in `tests/`, and a line in
  `scripts/test.sh`.
- Add a platform primitive to `engine/platform/platform.h` and implement it in
  `engine/platform/desktop/`; never expose SDL types upward.
- Add a renderer feature to `engine/render/render_api.h` and implement it in both
  `gl/` and `soft/`.
- Add an editor operation to `editor/ops.c`; the GUI, Lua, plugins and agents reach
  it through the same dispatcher.
