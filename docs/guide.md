# MEXNya handbook

[← Project overview](../README.md) · [简体中文介绍](../README_zhcn.md)

Engine, editor, agent bridge and file-format reference.

## Contents

- [Features](#features)
- [Quick start](#quick-start)
- [Making a game](#making-a-game)
- [The editor](#the-editor)
- [Concepts](#concepts)
- [Working with AI agents](#working-with-ai-agents)
- [Engine API](#engine-api)
- [Build tool and mexnya.json](#build-tool-and-mexnyajson)
- [File formats](#file-formats)
- [Plugin specification](#plugin-specification)
- [Localization](#localization)
- [Repository layout](#repository-layout)
- [Platform status](#platform-status)
- [Contributing](#contributing)
- [License](#license)

## Features

**Engine**

- **Plain C99.** Public headers never expose SDL, OpenGL or Lua types.
- **Two renderers behind one small API.** OpenGL 3.3, or a CPU rasteriser that also
  runs headless for tests and servers.
- **Two display modes.**
  - *Native*: the window's own resolution, full colour, HiDPI aware.
  - *Retro*: a fixed low-resolution frame with ordered dither, scaled up with
    nearest-neighbour sampling.
- **Semantic input.** Actions such as confirm, jump, use, attack and menu, with
  keyboard, mouse and gamepad defaults, rebinding, and 16 actions for your game to
  define.
- **Raw input for tools.** Raw keys, text input, clipboard and cursors.
- **Audio.** A software mixer for 16 voices, a sound-bank loader and a music hook.
- **Text.** UTF-8 text from a bundled pixel font covering English, Chinese
  (Simplified and Traditional), Japanese and Korean, or from the OS font engine
  (macOS, Windows).
- **Core helpers.** A JSON DOM that keeps key order, a PNG writer, and key-based i18n
  in five languages.
- **Scene runtime.** Loads projects made in the editor, draws them, and follows their
  Lines and Tunnels.
- **Lua 5.4** console and scripting, with a sandbox and an instruction budget.

**Editor**

- **Scenes as layers.** A 3D world and a 2D UI screen can stack and be edited together.
- **Unity-style 3D view.** Orbit, pan, fly, preset views, and move/rotate/scale gizmos
  with snapping.
- **2D canvas.** Pixel-accurate, with zoom from 12.5% to 800%.
- **Notes.** Box, circle, arrow, pin and pen marks, each saved with pixels, targets, a
  world point and a PNG. They are also collected in `mexnya/NOTES.md`.
- **Graph page.** Draw Lines between scene ports, add Tunnels, and simulate what a port
  reaches.
- **Build and Git pages.** The build page drives the engine's build tool. The Git page
  shows status and diffs, commits, and fetches, pulls and pushes; pushing asks for
  confirmation.
- **Agent bridge.** Any agent can call any operation from a shell command. All calls are
  logged to `mexnya/agent/log.jsonl`.
- **Plugins** in Lua or native C (a JSON-only ABI).
- **Interface.** Five interface languages, pixel or system font, and UI scale for
  Retina and 4K screens.

## Quick start

### Requirements

| | |
|---|---|
| C compiler | Clang, GCC or MinGW-w64 (C99) |
| CMake | 3.16 or newer |
| Python | 3.8 or newer (build tool; Pillow only for the model and font tools) |
| Git | to fetch SDL3 the first time you configure (or use an installed SDL3) |
| Linux only | the usual SDL3 build dependencies (X11/Wayland, ALSA/PulseAudio, OpenGL headers) |

### Build and run

```bash
git clone https://github.com/harukrsm/MEXNya.git
cd MEXNya
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug   # downloads SDL3 (pinned) the first time
cmake --build build/debug -j
bash scripts/test.sh                                   # static checks, engine, editor, sample
```

Then:

```bash
./build/debug/samples/hello/hello        # the sample game
bin/mexnya run                           # build and open the editor on this repository
```

Put `bin/` on your `PATH` to type `mexnya` anywhere (`bin/mexnya.cmd` on Windows).

> **Offline or with your own SDL3?** Pass `-DMEXNYA_SDL3_DIR=/path/to/SDL` (a source
> tree), put SDL's source in `third_party/SDL3/`, or install SDL3 so `find_package(SDL3)`
> finds it.

## Making a game

```bash
mexnya new ~/games/my-game        # copies templates/game, points it at this engine
cd ~/games/my-game
mexnya build                      # configure + compile into build/debug
./build/debug/my-game             # run it
mexnya run                        # open the editor on it
```

A new game looks like this:

```
my-game/
├── CMakeLists.txt        links mexnya::engine (MEXNYA_DIR points at the engine)
├── mexnya.json           build targets for the build tool and the editor
├── src/main.c            your game: starts the engine, loads the scenes, plays
├── mexnya/               the editor's project: project.json + scenes/*.json
└── assets/               models, sounds, anything you load (copied next to the binary)
```

The template is a working game. You move a cube with WASD, jump with Space, and E fires
a port whose Line shows the HUD scene. Its core:

```c
#include "engine/engine.h"
#include "engine/platform/input.h"
#include "engine/scene/scene.h"

static void update(float dt, void *user) {
    Game *g = user;
    Input in;
    input_get_last(&in);                                  // semantic actions + axes
    Json *player = mx_object(g->project, g->main, "player");
    float pos[3];
    json_getv(player, "pos", pos, 3);
    pos[0] += in.move_x * 4 * dt;                         // move the editor's object
    json_setv(player, "pos", pos, 3);
    if (in.pressed & ACT_BIT(ACT_USE)) mx_emit(g->project, "main.use");  // follow Lines
}

static void draw(void *user) {
    Game *g = user;
    int w, h;
    r_ui_size(&w, &h);
    mx_project_draw(g->project, &g->cam, w, h);           // 3D layers, then 2D layers
}

int main(int argc, char **argv) {
    EngineConfig cfg = {1280, 720, 0, 0, "My Game", "Me", "MyGame", "auto", 1 /* native */};
    engine_init(&cfg);
    /* load the font, mx_project_load("<exe>/mexnya"), pick a camera ... */
    engine_loop(update, draw, &game, 0);
    engine_shutdown();
}
```

### Assets

| Asset | Tool | Use in code |
|---|---|---|
| 3D models | `python3 engine/tools/obj2mcm.py model.obj assets/models/model.mcm [--ground] [--light]` (OBJ + MTL: colours, 16-colour textures, alpha cut-outs, groups become parts) | `model_load(&m, "models/model.mcm")`, `model_draw(&m)`, or a `model` object in the editor |
| Sounds | `python3 engine/tools/make_soundbank.py assets/sound/bank.mcs jump.wav loop:music.wav` (writes `bank_ids.h` too) | `audio_init()`, `audio_play(SND_JUMP, 1, 0, 1)`, `audio_loop(...)` |
| Fonts | `python3 engine/tools/gen_font.py font.otf out.mxf` (any 12 px pixel font) | `font_load(path)` |
| Anything else | put it in `assets/` | `file_load("data/level1.json", &size)`, `json_load(...)` |

To build a release, run `mexnya build release`. The game folder in `build/release`
(the executable, `mexnya/`, `assets/` and `engine-resources/`) is what you ship.

## The editor

`mexnya run` opens the editor on the project in the current folder (the folder that
holds `mexnya.json`). A project without `mexnya/` gets a starter scene.

| Page | What it does |
|---|---|
| **Scene** | Scene layers (show/hide/lock, order), hierarchy, inspector, notes, 3D view / 2D canvas, console (log, operations, build output, Lua) |
| **Graph** | Scenes and Tunnels as nodes. Drag from an output port to an input port to make a Line, edit ports, and simulate a port firing |
| **Build** | Every target, test and package in `mexnya.json`, a toolchain check, and "run game" |
| **Git** | Branch, changed files with diffs, commit-all, fetch, fast-forward pull, and push (confirmed) |
| **Agent** | How to connect, who connected, and the live operation log |
| **Plugins** | Installed plugins, status, enable/disable, reload, and a sample-plugin generator |

### 3D view

| Input | Action |
|---|---|
| Right drag (or Alt + left drag) | Orbit |
| Middle drag (or Shift + right drag) | Pan |
| Wheel | Zoom |
| Hold right button + WASD / Q E | Fly (Shift = faster) |
| F | Frame the selection |
| Persp / Top / Front / Right buttons, or numpad 7 / 1 / 3 | Preset views; numpad 5 toggles orthographic |
| Q W E R | Select, move, rotate, scale |
| Ctrl/Cmd while dragging | Snap (0.5 m, 15°, 0.25 scale) |

The 2D view pans with the middle or right button, zooms with the wheel, moves rects by
dragging and resizes them from the corner. Ctrl/Cmd snaps to 4 px.

### Notes

Choose a note tool and mark the scene. The tools are rectangle (B), circle (C), arrow
(A), pin (N) and freehand (P). Type what you want and press Enter. Clicking a note in
the list returns the 3D view to the camera it was made from. From the list you can
resolve, reopen, reply to or delete a note.

### Shortcuts

| Key | Action |
|---|---|
| Ctrl/Cmd + S | Save (the editor also autosaves) |
| Ctrl/Cmd + Z, Ctrl/Cmd + Shift + Z (or Y) | Undo, redo |
| Ctrl/Cmd + D, Del | Duplicate, delete |
| Ctrl/Cmd + 1…6 | Pages |
| Ctrl/Cmd + = / − | UI scale |
| F5 | Reload files changed on disk |
| Esc | Deselect, close dialogs |

### Settings

**Settings** in the top bar has three options, and your choice is kept per user:

- **Language:** English, 简体中文, 繁體中文, 日本語, 한국어.
- **Font:** *Pixel* (bundled, works everywhere) or *System* (smooth, from the OS font
  engine on macOS and Windows).
- **UI scale:** 1× to 4×. It follows the screen's pixel density by default.

## Concepts

| | |
|---|---|
| **Scene** | One place: a 3D world or a 2D UI screen. Scenes stack like layers in a paint program; every visible scene is drawn bottom to top, and only the active one is edited. |
| **Port** | A named connection point on a scene or Tunnel: `in` (e.g. `show`, `hide`), `out` (e.g. `exit`, `use`), or `both` on a Tunnel. |
| **Line** | `from: "node.port"` → `to: "node.port"`. When a port fires, the port at the other end fires. |
| **Tunnel** | An abstraction layer: a named bus whose ports pass signals through. Scenes connect to a Tunnel instead of to each other, so either side can change without touching the other. |
| **Note** | A mark on a scene plus text, by a person or an agent, saved with everything needed to act on it. |
| **Operation** | The only way a project changes, for example `object.add` or `line.add`. The GUI, Lua, plugins and agents all use operations, and each one is logged and can be undone. |
| **Plugin** | A folder with `plugin.json` plus Lua and/or a native library that adds operations and event handlers. |

At runtime, `mx_emit(project, "main.use")` follows the Lines from that port. A
Tunnel's ports pass a signal on to the Lines that leave them. A scene's built-in `show`
and `hide` in-ports change its visibility. Your `mx_on_port` handler hears every port
reached.

## Working with AI agents

The editor writes **`mexnya/AGENTS.md`** into every project, a guide an agent can read
first. An agent then calls operations from the project folder:

```bash
mexnya agent help                                            # every operation and its arguments
mexnya agent note.list '{"status": "open"}' --as my-agent    # what the person marked
mexnya agent object.set '{"id": "player", "fields": {"color": "#E06C60"}}' --as my-agent
mexnya agent view.snapshot '{"scene": "main", "preset": "top"}'   # a PNG to look at
mexnya agent note.resolve '{"id": "note_1", "reply": "Recoloured."}' --as my-agent
mexnya agent batch '[{"op": "object.add", "args": {"type": "box"}}, {"op": "undo"}]'
```

How a call is carried out depends on whether the editor is open:

- **Editor open:** the call goes through `mexnya/agent/inbox/<id>.json`, the result
  comes back in `outbox/`, and the person watches the change happen.
- **Editor closed:** the same operation runs headless on the files
  (`MexnyaEditor --project . --exec '<json>'`).

Requests are accepted without asking, so agents need no special setup. Every request is
recorded in `mexnya/agent/log.jsonl` with its time, caller, operation, arguments,
result and duration. An agent's change never moves the person's selection.

Notes are written for agents to act on. A 3D note records:

- the shape in viewport pixels;
- the camera and viewport size;
- `targets`, the objects inside the shape;
- `world`, the point under the shape's centre;
- `image`, a PNG with the mark drawn in.

On a 2D scene the shape is in canvas pixels, and the snapshot is the canvas at 1:1.

### All operations

| Operation | Arguments → result | Changes the project |
|---|---|---|
| `help` | {} -> every operation with its arguments |  |
| `project.get` | {} -> scenes (layers, bottom first), tunnels, lines |  |
| `project.save` | {} -> writes mexnya/*.json and NOTES.md |  |
| `project.reload` | {} -> re-reads files changed outside the editor |  |
| `scene.create` | {name, kind: "3d"\|"2d", id?, index?} -> {id} | yes |
| `scene.delete` | {scene} (also removes its lines) | yes |
| `scene.get` | {scene?} -> the whole scene document |  |
| `scene.set` | {scene?, fields: {name?, visible?, locked?, settings?: {...}}} | yes |
| `scene.move` | {scene, index} layer order (0 = bottom) | yes |
| `scene.select` | {scene} make it the active scene in the GUI |  |
| `object.add` | {scene?, type, name?, id?, pos?, rot?, scale?, color?, model?, rect?, text?, props?} -> {id} | yes |
| `object.set` | {scene?, id, fields: {...}} (null removes a field) | yes |
| `object.delete` | {scene?, id} | yes |
| `object.get` | {scene?, id} |  |
| `object.list` | {scene?} |  |
| `object.duplicate` | {scene?, id} -> {id} | yes |
| `note.add` | {scene?, kind: box\|circle\|arrow\|pin\|pen, text, shape: {rect:[x,y,w,h]} \| {circle:[cx,cy,r]} \| {arrow:[x0,y0,x1,y1]} \| {pin:[x,y]} \| {points:[[x,y],...]}, camera?, color?} -> {id, note} | yes |
| `note.set` | {scene?, id, fields: {text?, status?, color?, shape?}} | yes |
| `note.reply` | {scene?, id, text} | yes |
| `note.resolve` | {scene?, id, reply?, reopen?} | yes |
| `note.delete` | {scene?, id} | yes |
| `note.list` | {scene?, status?: open\|done} -> notes with shapes, targets and image paths |  |
| `tunnel.add` | {name, id?, ports: ["open", {name, dir: in\|out\|both}], note?} -> {id} | yes |
| `tunnel.set` | {id, fields} | yes |
| `tunnel.delete` | {id} (also removes its lines) | yes |
| `port.add` | {node: scene or tunnel id, name, dir: in\|out\|both} | yes |
| `port.delete` | {node, name} | yes |
| `line.add` | {from: "node.port", to: "node.port", note?} -> {id} | yes |
| `line.set` | {id, fields: {from?, to?, note?}} | yes |
| `line.delete` | {id} | yes |
| `graph.layout` | {id, pos: [x, y]} node position on the Graph page | yes |
| `graph.emit` | {from: "node.port"} -> ports reached through lines and tunnels |  |
| `view.get` | {scene?} -> camera and viewport size |  |
| `view.set` | {scene?, camera?: {target, yaw, pitch, dist, fov, ortho}, preset?: top\|front\|right\|back\|left\|persp} |  |
| `view.snapshot` | {scene?, camera?, preset?, width?, height?, path?: "snapshots/x.png", notes?: true} -> {path} |  |
| `select` | {scene?, id} select an object in the GUI |  |
| `script.run` | {code} or {file: path inside mexnya/} runs Lua with the mx API -> {output} |  |
| `build.run` | {target: debug\|release\|windows\|all\|test\|package\|package-&lt;name&gt;} |  |
| `build.status` | {} -> running and the last lines |  |
| `git.status`, `git.log`, `git.diff` | {}, {n?}, {path?} |  |
| `git.commit` | {message, all?: true} saves, stages and commits |  |
| `git.push`, `git.pull`, `git.fetch`, `git.init` | {} (pull is fast-forward only) |  |
| `git.branch` | {switch?: name} |  |
| `plugin.list`, `plugin.reload` | {} |  |
| `log.tail` | {n?, channel?: agent\|log\|build\|lua} |  |
| `undo`, `redo` | {} |  |
| `editor.message` | {text} show a message to the person in the editor |  |
| `editor.mode` | {mode: scene\|graph\|build\|git\|agent\|plugins} |  |

`scene` defaults to the scene that is active in the editor. Plugins add their own
operations, named `<plugin>.<verb>`.

## Engine API

All headers are included from the repository root (`#include "engine/..."`) and link
with `mexnya::engine`. The table lists each module's main calls; every function is
documented in its header.

| Module | Header | Main calls |
|---|---|---|
| Engine | `engine/engine.h` | `engine_init(&EngineConfig)`, `engine_loop(update, draw, user, frames)`, `engine_request_quit()`, `engine_shutdown()`, `MX_ENGINE_VERSION` |
| Platform | `engine/platform/platform.h` | `plat_time()`, files (`plat_read_file`, `plat_write_file`, `plat_dir_list`, `plat_mkdirs`), `plat_user_dir`, `plat_exe_dir`, threads/mutexes, `plat_video_*`, `plat_audio_open`, `plat_lib_open/sym/close`, `plat_preferred_language` |
| Game data | `engine/platform/file.h` | `file_set_game_dir(argv0)` (data = `<exe>/assets`), `file_set_data_dir`, `file_load(rel, &size)`, `file_path` |
| Input | `engine/platform/input.h`, `actions.h` | `input_get_last(&in)`: `held`, `pressed`, `repeat` bits (`ACT_BIT(ACT_JUMP)`), `move_x/y`, `look_x/y`, mouse; `input_set_binding(BIND_ACTION(a), key)`; raw keys `input_key_pressed(KEY_...)`; `input_text_start`, `input_clipboard_*` |
| Frame | `engine/render/gfx.h` | `gfx_begin_3d/2d`, `gfx_set_native`, `gfx_set_fog(near, far, rgb)` (also the clear colour), `gfx_set_projection`, `gfx_viewport`, `gfx_copy_frame_stride` |
| Renderer | `engine/render/render_api.h` | matrices (`r_matrix_mode`, `r_look_at`, `r_perspective`, `r_translate`…), state (`r_set_blend`, `r_set_depth_test`, `r_set_fog`…), `r_bind_texture(&RTexDesc)`, `r_draw(prim, verts, n)`, `r_size`, `r_set_frame_size`, `r_set_ui_scale`, `r_copy_frame_rgb` |
| 2D | `engine/render/ui.h` | `ui_rect`, `ui_frame`, `ui_tri`, `ui_clip`, `ui_clip_reset` |
| Text | `engine/render/font.h` | `font_load(path)`, `font_set_mode(FONT_PIXEL \| FONT_SYSTEM, lang, scale)`, `font_draw`, `font_drawf`, `font_width`, `font_fit` |
| Models | `engine/render/model.h` | `model_load`, `model_draw`, `model_draw_part`, `model_draw_posed` (per-part rotations), `model_find_part`, `model_preview` |
| Audio | `engine/audio/audio.h` | `audio_init`, `audio_play(id, vol, pan, pitch)`, `audio_loop`, `audio_set`, `audio_stop`, `audio_set_master`, `audio_set_music_mixer` |
| Scenes | `engine/scene/scene.h` | `mx_project_load(dir)`, `mx_scene_find`, `mx_object(p, scene, id)`, `mx_scene_camera`, `mx_camera_apply`, `mx_scene_draw_3d/2d`, `mx_project_draw`, `mx_emit`, `mx_on_port`, `mx_scene_set_visible` |
| JSON | `engine/core/json.h` | `json_parse/load/write/save`, `json_get/gets/getn/getb/getv`, `json_set/sets/setn/setv`, `json_push`, `json_find_id`, `json_path("a.2.#id")`, `json_clone`, `json_free` |
| PNG | `engine/core/png.h` | `png_save_rgb`, `png_save_565` |
| i18n | `engine/core/i18n.h` | `i18n_add_domain(dir, "game")`, `i18n_set_language("ja")`, `tr("menu.start")`, `i18n_system_language()` |
| Console | `engine/console/console.h` | `console_register(name, usage, help, fn)`, `console_exec_line`, `console_lua_run_string`, `console_print` |
| Replay | `engine/platform/replay.h` | deterministic input recording and playback per logic step |

**`EngineConfig`**: `{width, height, scale, headless, title, org, app, backend, native}`.

- `width` × `height` is the frame in retro mode, or the first and headless size in
  native mode.
- `backend` is `"auto"`, `"gl"` or `"software"`.
- `org` and `app` name the per-user folder used for saves.

**Input actions.**

| Action | Keyboard and mouse | Gamepad |
|---|---|---|
| `ACT_CONFIRM` | Return, left click | A |
| `ACT_BACK` | Esc | B |
| `ACT_NAV_*` | Arrow keys | D-pad |
| `ACT_JUMP` | Space | A |
| `ACT_SPRINT` | Left Shift | left stick press |
| `ACT_USE` | E | X |
| `ACT_ATTACK` | left mouse | RT |
| `ACT_AIM` | right mouse | LT |
| `ACT_MENU` | Tab | Start / Back |
| `ACT_CAMERA` | C | Y |
| `ACT_NEXT` / `ACT_PREV` | wheel | shoulders |
| `ACT_ZOOM_IN` / `ACT_ZOOM_OUT` | = / − | |
| `ACT_USER_0` … `ACT_USER_15` | unbound, yours to name | |

Movement is `move_x`/`move_y` (WASD or the left stick).

Two conventions apply throughout the engine:

- **Colours** are `0xRRGGBB` in 2D and ABGR in vertices.
- **3D** uses metres with y up and rotations in degrees `[x, y, z]`, applied Y, then X,
  then Z.

## Build tool and mexnya.json

`mexnya` (`bin/mexnya`, or `python3 engine/tools/mexnya.py`) works in any folder with a
`mexnya.json`:

| Command | Does |
|---|---|
| `mexnya new <folder> [--name N]` | Create a game from `templates/game` |
| `mexnya build [debug\|release\|<target>\|all]` | Configure and compile with a progress bar and coloured warnings |
| `mexnya test` | Build the test target and run the `test` script |
| `mexnya package [<name>\|all]` | Build and run a packaging script |
| `mexnya run [--no-build]` | Build the editor (once, inside the engine repository) and open it on this project |
| `mexnya agent <op> ['<json>'] [--as NAME]` | Run one editor operation (live in the open editor, or headless) |
| `mexnya doctor` | Show which compilers each target would use |
| `mexnya assets` | Run the project's asset command |

```jsonc
{
  "name": "My Game", "version": "0.1.0",
  "banner": "art/banner.txt",                   // optional: shown by the tool
  "assets": ["python3", "tools/make_assets.py"],    // optional: `mexnya assets`
  "targets": {
    "debug":   {"dir": "build/debug",   "type": "Debug"},
    "release": {"dir": "build/release", "type": "Release", "defines": {"MEXNYA_OPENGL": "ON"}},
    "windows": {"dir": "build/windows", "type": "Release",                  // cross-compile in Docker
                "docker": "dockcross/windows-static-x64",
                "toolchain": "path/to/git-MEXNya/cmake/toolchains/mingw-w64.cmake"}
  },
  "packages": {"macos": {"target": "release", "script": "scripts/package_macos.sh"}},
  "test":     {"target": "debug", "script": "scripts/test.sh", "args": []},
  "editor":   {"run": {"target": "debug", "binary": "my-game"}}            // "Run Game" in the editor
}
```

CMake options:

- `MEXNYA_OPENGL` (default ON).
- `MEXNYA_SDL3_DIR`, a local SDL3 source tree.
- `MEXNYA_SDL3_TAG`, the SDL3 release to download.
- `MEXNYA_BUILD_EDITOR` and `MEXNYA_BUILD_SAMPLES`, for this repository.

## File formats

All project files are JSON that keeps its key order, so diffs stay small and readable.
The editor autosaves, and it reloads files that are changed outside it.

```
mexnya/project.json        {format, version, name, scenes: [ids, bottom layer first], tunnels, lines, graph, plugins}
mexnya/scenes/<id>.json    {id, name, kind: "3d"|"2d", visible, locked, settings, ports, camera, objects, notes}
mexnya/NOTES.md            every note as text (generated)
mexnya/AGENTS.md           the agent guide (generated)
mexnya/notes/<scene>/*.png note snapshots
mexnya/agent/log.jsonl     one JSON line per operation
mexnya/plugins/<name>/     plugins
```

- **3D object:** `{id, name, type: box|plane|sphere|cylinder|cone|model|light|marker|camera, pos, rot, scale, color: "#RRGGBB", model?: "models/x.mcm", visible?, props?: {...}, script?}`.
- **2D object:** `{id, name, type: rect|text|button|image|panel, rect: [x, y, w, h], color, text?, image?, props?}`.
- **Scene settings:** for 3D, `background`, `fog`, `fog_color` and `fog_enabled`; for
  2D, `canvas: [w, h]`.
- **Note:** `{id, kind, text, status, author, created, color, shape, camera?, view_size?, targets, world?, image, replies?}`.
- **`props`** is free-form JSON for your game and plugins. The editor keeps it untouched.
- **Binary formats:**
  - `.mcm` model and `.mct` texture: see `engine/tools/mcm.py` (4-bit paletted
    textures, parts with pivots, batches with flags).
  - `.mcs` sound bank: see `engine/tools/make_soundbank.py`.
  - `.mxf` font: see `engine/tools/gen_font.py`.

## Plugin specification

A plugin is a folder in the project:

```
mexnya/plugins/<name>/
├── plugin.json
├── main.lua                 (optional) Lua entry
└── lib<name>.dylib|.so|.dll (optional) native library
```

```json
{
  "name": "terrain",
  "version": "1.0.0",
  "description": "Terrain tools: raise, flatten, scatter.",
  "lua": "main.lua",
  "native": {"macos": "libterrain.dylib", "linux": "libterrain.so", "windows": "terrain.dll"}
}
```

**Rules**

1. **Name operations `<plugin>.<verb>`**, for example `terrain.raise`. Names without a
   dot and names of built-in operations are refused, because built-ins are the
   contract with agents.
2. **Change the project only through operations** (`mx.op`, `api->op`). Then every
   change is logged as `plugin:<name>` and can be undone.
3. **Keep identifiers English.** Use English IDs, operation names and data. Text for
   people goes through translation keys (see [Localization](#localization)).
4. **Use `props` for your own data.** Unknown fields are kept, but `props` is the
   documented place for them.
5. **Expect reloads.** Plugins load at start-up and on `plugin.reload`. A reload drops
   every plugin operation and handler, then runs the plugins again. Lua plugins share
   one state, so keep your variables `local`.
6. **Return `{ok: true, ...}` or `{ok: false, error: "..."}`.** A thrown Lua error
   becomes `ok: false` automatically.
7. **The project can disable you.** `project.json` → `plugins.disabled: [names]`.
   The Plugins page sets this.

**Lua API (`mx`)**

| Call | Does |
|---|---|
| `mx.op(name, args)` | Run any operation; returns the result table |
| `mx.help()` | List the operations |
| `mx.register_op(name, help, fn)` | Add an operation; `fn(args)` returns a table |
| `mx.on(event, fn)` | `fn(op, args)` after an operation succeeds (`"*"` = any) |
| `mx.log(...)`, `print(...)` | Output to the Lua console or the caller |
| `mx.status(text)` | Message in the status bar |
| `mx.scene()`, `mx.selection()` | Active scene and selected object IDs |
| `mx.json(value)`, `mx.tr(key)`, `mx.version` | Helpers |
| `mx.wait(n)`, `mx.mouse(x, y, buttons, pressed)`, `mx.key(code, down)`, `mx.type(text)`, `mx.shot(path)`, `mx.quit()` | Frame-stepped scripts started with `--script` (GUI automation and tests) |
| `mx.set_language(code)`, `mx.set_font("pixel"\|"system")` | Presentation settings |

The Lua sandbox has only `base`, `table`, `string`, `math` and `utf8`: no `io`, `os`,
`package`, `debug`, `dofile` or `loadfile`. An instruction budget stops runaway loops.

```lua
-- mexnya/plugins/scatter/main.lua
mx.register_op("scatter.ring", "{count?, radius?} boxes in a ring", function(args)
  local ids = {}
  for i = 1, (args.count or 6) do
    local a = (i - 1) / (args.count or 6) * math.pi * 2
    local r = mx.op("object.add", {type = "box", pos = {math.cos(a) * 3, 0.5, math.sin(a) * 3}})
    ids[#ids + 1] = r.id
  end
  return {ok = true, ids = ids}
end)
```

**Native ABI (`editor/mx_plugin.h`, version 1)**

- The boundary is JSON text only, so any language with a C FFI works and no engine
  headers are needed.
- Export `int mx_plugin_init(const MxApi *api)` (return 0 for success) and, optionally,
  `void mx_plugin_shutdown(void)`.
- Through `api` you get:
  - `api->op(request_json)`, which returns result JSON, valid until the next call;
  - `api->log(text)`;
  - `api->register_op(name, help, fn, user)`, where `fn(args_json, user)` returns
    JSON owned by the plugin (or `NULL` for `{"ok": true}`).
- Check `api->api_version == MX_PLUGIN_API`.

There is a working sample: `editor/samples/hello_plugin.c` (target `mx_plugin_hello`)
with its `plugin.json`. Copy both into `mexnya/plugins/hello/`.

## Localization

- **Keys, not text, in code.** Code names strings by English keys (`tr("menu.save")`).
  Widget identities, project data, operation names and log entries never depend on the
  language, so you can switch language at any time, as often as you like, without
  changing behaviour.
- **Loading.** Tables are JSON files `<dir>/<domain>.<lang>.json`.
  - English is the fallback, and missing keys fall back to English, then to the key.
  - A translation whose `printf` conversions differ from English is rejected when it
    loads.
  - Pointers returned by `tr()` stay valid across language switches.
- **The editor's text.** It lives in `engine/tools/lang_editor.py`, one row per key with
  all five languages side by side. Run it to regenerate
  `engine/resources/lang/editor.*.json`.
- **Your game's text.** Ship `<domain>.<lang>.json` files and call
  `i18n_add_domain(dir, "game")`.
- **The check.** `scripts/check_i18n.py`, part of the tests, fails on:
  - a CJK literal in C code;
  - a missing key;
  - a format mismatch.

## Repository layout

```
engine/            the engine library (CMake target mexnya::engine)
  core/            json, png, i18n
  platform/        platform API; desktop/ (SDL3) backend, input, system fonts
  render/          render API, gfx, ui, font, model; soft/ and gl/ renderers
  audio/           mixer (+ minimp3)
  console/         terminal console and Lua
  scene/           runtime for editor projects
  resources/       mx12.mxf font, UI text tables, banner
  tools/           mexnya.py, obj2mcm.py, make_soundbank.py, gen_font.py, mcm.py, lang_editor.py
editor/            the MEXNya Editor (C), mx_plugin.h, samples/hello_plugin.c
templates/game/    what `mexnya new` copies
samples/           engine-smoke (minimal), hello (the template, built in-tree)
tests/             runtime_test.c
scripts/           test.sh, check_boundaries.py, check_i18n.py
third_party/lua/   Lua 5.4 (MIT); SDL3 is fetched by CMake
bin/               mexnya, mexnya.cmd
```

## Platform status

| Platform | Status |
|---|---|
| **macOS** (Apple Silicon, Intel) | Developed and tested: engine, editor (OpenGL, system font), sample game, test suite |
| **Windows** (x64) | Cross-compiles with MinGW-w64 (editor, sample, tests). The OpenGL loader and GDI system font are compiled but not yet tested on a Windows machine |
| **Linux** (x64) | Supported by the code; not yet built by the author. The system font falls back to the pixel font |

This is a first release (0.1.0). Not yet in this release:

- skeletal animation import;
- physics;
- a model editor inside the editor;
- mobile and web targets.

## Contributing

Issues and pull requests are welcome. Please:

- Keep C99, the dependency boundaries (`scripts/check_boundaries.py`) and the
  English-only code rule (`scripts/check_i18n.py`).
- Run `bash scripts/test.sh` after a debug build.
- Add UI text as rows in `engine/tools/lang_editor.py`, translated into all five
  languages.
- Change the project file formats only in a backward-compatible way, or bump
  `version`.

## License

MEXNya is free software, released under the **GNU General Public License v3.0**; see
[LICENSE](../LICENSE). Games you build link the engine and therefore fall under the GPL
when you distribute them.

Bundled components keep their own licences; see [THIRD_PARTY.md](../THIRD_PARTY.md):

- SDL3 (zlib, downloaded at build time);
- Lua 5.4 (MIT);
- minimp3 (CC0);
- the Fusion Pixel Font data in `mx12.mxf` (SIL OFL 1.1).
