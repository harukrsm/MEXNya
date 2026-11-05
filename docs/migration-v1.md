# Migrating a game to the new layers

This release adds a core utility layer, a world/entity runtime, fixed-timestep
scheduling, atomic writes and savegames. The old APIs still compile and behave as
before; migration is incremental. This page shows the mapping and the pitfalls.

## What did not change

- `mx_project_load`, `mx_object`, `mx_scene_draw_*` and `mx_emit` still read and draw
  the editor's JSON, so the editor and existing games keep working.
- `engine_init` and `engine_loop(update, draw, user, frame_limit)` keep their
  signatures and their variable-step behaviour.
- `json_get*` / `json_set*` are unchanged; `json_save` is now atomic internally.
- Scene objects still use `pos`/`rot`/`scale`, with `rot` in degrees and the
  `Ry * Rx * Rz` convention.

## API map

| Old | New |
| --- | --- |
| `json_getv(player, "pos", ...)` | `MxTransform.position` on the entity |
| `json_setv(player, "rot", ...)` | `MxTransform.rotation` (`MxQuat`) |
| `mx_object(project, scene, id)` | `mx_entity_find(world, id)` |
| per-frame JSON lookups | `mx_comp_get(world, entity, MX_COMP_*)` |
| `engine_loop(update, draw, ...)` | `engine_loop_ex(tick, render, ...)` |
| variable `dt` | `EngineConfig.fixed_hz`, accumulator and `render(alpha)` |
| `json_save` / `plat_write_file` | unchanged or `plat_write_file_atomic` |
| ad-hoc persistence | `mx_save_write` / `mx_save_read` |
| `printf` diagnostics | `mx_log(MX_LOG_INFO, "channel", ...)` |

## Build a world

```c
MxWorld *world = mx_world_from_scene(mx_scene_doc(project, scene));
MxEntity player = mx_entity_find(world, "player");
MxTransform *t = (MxTransform *)mx_comp_get(world, player, MX_COMP_TRANSFORM);
```

Object types become components: `box`/`plane`/`sphere`/`cylinder`/`cone`/`model`
give an `MxMeshRenderer`, `light` gives an `MxLight`, and `rect`/`panel`/`button`/
`text`/`image` give an `MxSprite`. Attach your own with `mx_comp_add`; a component
holds its own state, so there are no per-frame JSON reads or writes.

## Update the game

Before:

```c
Json *player = mx_object(g->project, g->main_scene, "player");
float pos[3] = {0, 0.5f, 0};
json_getv(player, "pos", pos, 3);
pos[0] += speed * dt;
json_setv(player, "pos", pos, 3);
```

After:

```c
MxTransform *t = (MxTransform *)mx_comp_get(g->world, g->player, MX_COMP_TRANSFORM);
t->position.x += speed * dt;
t->rotation = mx_quat_from_euler(mx_v3(0.0f, heading_degrees, 0.0f));
```

Rotation is a quaternion. Use `mx_quat_from_euler` and `mx_quat_to_euler` at the
boundary, `mx_quat_axis_angle` for an axis, and `mx_transform_compose`/
`mx_transform_decompose` for matrices.

Parents use entity handles: `mx_entity_set_parent(world, child, parent)` and
`MxTransform.world` after `mx_world_update_transforms` or `mx_world_tick`.

## Fixed timestep

```c
EngineConfig cfg = {1280, 720, 0, 0, "game", "Org", "Game", "auto", 1,
                    60, 4, 0};
engine_loop_ex(tick, render, &game, 0);
```

`tick` receives exactly `1 / fixed_hz` (or the scaled dt) and `render` receives
`alpha` in [0, 1]. Interpolate with `mx_v3_lerp(t->prev_position, t->position, alpha)`
and the quaternion equivalent. Input is polled once per frame and edges are consumed
by the first sub-step. `engine_loop` is the old entry point and passes `alpha = 1`.

## Draw

```c
mx_camera_apply(&cam, w, h);
mx_world_draw_3d(world);
mx_project_draw_2d(project, w, h);
```

The engine starts the 3D pass; `mx_project_draw_2d` switches to the 2D pass and draws
the editor's visible 2D layers. `mx_world_draw_2d(world, x, y, scale)` draws sprites
from the world instead.

## Save and settings

```c
mx_save_write(world, "slot1");
mx_save_read(world, "slot1");
Json *settings = mx_settings_load(path, defaults);
mx_settings_save(path, settings);
```

Saves are `MXSV` v1, little-endian, CRC-32, written atomically. Loading parses into a
temporary world and swaps only on success; a bad file leaves the running world alone.
Entity handles from before a load are invalid afterwards.

## Pitfalls

- Check `mx_entity_alive` before using a handle kept across frames; a destroyed entity
  bumps its generation and `mx_comp_get` returns `NULL`.
- Move parents before reading children; call `mx_world_tick`/`mx_world_update_transforms`.
- Do not mix units: positions are metres, Euler angles are degrees at the API boundary.
- Use `plat_write_file_atomic` for anything a reader can observe while it is written.
- `mx_save_read` replaces every entity in the target world.

## Where to look

- `docs/architecture.md`: layers, frame, memory and file formats.
- `docs/agent/HANDOFF.md`: what each task changed and how it was verified.
- `samples/hello/src/main.c`: the same game before/after this migration.
