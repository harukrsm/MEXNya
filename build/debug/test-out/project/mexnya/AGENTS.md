# MEXNya Editor: guide for AI agents

This folder is a MEXNya project. A person edits it in the MEXNya Editor (`mexnya run`);
you work on the same project through operations. Everything you do is logged in
`mexnya/agent/log.jsonl` with your name, and the person sees it live in the editor.

## Read first

- `mexnya/NOTES.md`: what the person marked and asked for. Each note has a shape in
  pixels, the objects inside it (`targets`), a world point and a PNG snapshot.
  Open the PNG to see exactly what was circled.
- `mexnya/project.json`: scenes (layers, bottom first), tunnels, lines.
- `mexnya/scenes/<id>.json`: one scene with its objects and notes.

## Concepts

- **SCENE**: one place: a 3D world or a 2D UI screen. Scenes stack like layers.
- **Line**: `from: "scene.port"` -> `to: "scene.port"`: when a port fires, the next fires.
- **Tunnel**: an abstraction layer (a bus). Scenes connect to a tunnel, not to each other.
- **Plugin**: `mexnya/plugins/<name>/plugin.json` + Lua or a native library; plugins add operations.
- **Note**: a person's (or your) mark on a scene: box, circle, arrow, pin, pen + text.

## How to call an operation

Run from the project folder (where mexnya.json is). `mexnya` below is
`python3 /Users/matt/Desktop/Vibe Project/git-MEXNya/engine/tools/mexnya.py`.

```bash
mexnya agent project.get
mexnya agent note.list '{"status": "open"}'
mexnya agent object.set '{"scene": "main", "id": "player", "fields": {"pos": [2, 0.5, -1]}}'
mexnya agent view.snapshot '{"scene": "main", "preset": "top"}'
mexnya agent note.resolve '{"scene": "main", "id": "note_1", "reply": "moved it"}'
```

Set `MEXNYA_AGENT=<your name>` (or pass `--as <name>`) so the log shows who you are.
With the editor open the call goes through `mexnya/agent/inbox/` and the person sees it
happen; without it the same operation runs headless on the files. Both save at once.

Raw protocol: write `mexnya/agent/inbox/<id>.json` with `{"agent", "op", "args"}` or
`{"agent", "batch": [{"op", "args"}, ...]}`; read `mexnya/agent/outbox/<id>.json`.

Lua: `script.run {"code": "..."}` runs with the `mx` API (`mx.op(name, args)`, `mx.log`).

## Working on notes

1. `note.list {"status": "open"}` and look at each `image`.
2. Change the scene with operations (or edit code for game logic).
3. `note.resolve {id, reply}` to tell the person what you did. Ask with `note.reply`.
4. You can mark things for the person too: `note.add` with a shape.

## Operations

| op | args |
|---|---|
| `help` | {} -> every operation with its arguments |
| `project.get` | {} -> scenes (layers, bottom first), tunnels, lines |
| `project.save` | {} -> writes mexnya/*.json and NOTES.md |
| `project.reload` | {} -> re-reads files changed outside the editor |
| `scene.create` | {name, kind: "3d"/"2d", id?, index?} -> {id} |
| `scene.delete` | {scene} (also removes its lines) |
| `scene.get` | {scene?} -> the whole scene document |
| `scene.set` | {scene?, fields: {name?, visible?, locked?, settings?: {...}}} |
| `scene.move` | {scene, index} layer order (0 = bottom) |
| `scene.select` | {scene} make it the active scene in the GUI |
| `object.add` | {scene?, type, name?, id?, pos?, rot?, scale?, color?, model?, rect?, text?, props?} -> {id} |
| `object.set` | {scene?, id, fields: {...}} (null removes a field) |
| `object.delete` | {scene?, id} |
| `object.get` | {scene?, id} |
| `object.list` | {scene?} |
| `object.duplicate` | {scene?, id} -> {id} |
| `note.add` | {scene?, kind: box/circle/arrow/pin/pen, text, shape: {rect:[x,y,w,h]} / {circle:[cx,cy,r]} / {arrow:[x0,y0,x1,y1]} / {pin:[x,y]} / {points:[[x,y],...]}, camera?, color?} -> {id, note} |
| `note.set` | {scene?, id, fields: {text?, status?, color?, shape?}} |
| `note.reply` | {scene?, id, text} |
| `note.resolve` | {scene?, id, reply?, reopen?} |
| `note.delete` | {scene?, id} |
| `note.list` | {scene?, status?: open/done} -> notes with shapes, targets and image paths |
| `tunnel.add` | {name, id?, ports: ["open", {name, dir: in/out/both}], note?} -> {id} |
| `tunnel.set` | {id, fields} |
| `tunnel.delete` | {id} (also removes its lines) |
| `port.add` | {node: scene or tunnel id, name, dir: in/out/both} |
| `port.delete` | {node, name} |
| `line.add` | {from: "node.port", to: "node.port", note?} -> {id} |
| `line.set` | {id, fields: {from?, to?, note?}} |
| `line.delete` | {id} |
| `graph.layout` | {id, pos: [x, y]} node position on the GRAPH page |
| `graph.emit` | {from: "node.port"} -> ports reached through lines and tunnels |
| `view.get` | {scene?} -> camera and viewport size |
| `view.set` | {scene?, camera?: {target, yaw, pitch, dist, fov, ortho}, preset?: top/front/right/back/left/persp} |
| `view.snapshot` | {scene?, camera?, preset?, width?, height?, path?: "snapshots/x.png", notes?: true} -> {path} PNG an agent can look at |
| `select` | {scene?, id} select an object in the GUI |
| `script.run` | {code} or {file: path inside mexnya/} runs Lua with the mx API -> {output} |
| `build.run` | {target: debug/release/windows/all/test/package/package-macos/package-windows} |
| `build.status` | {} -> running and the last lines |
| `git.status` | {} |
| `git.log` | {n?} |
| `git.diff` | {path?} |
| `git.commit` | {message, all?: true} saves, stages and commits |
| `git.push` | {} |
| `git.pull` | {} (fast-forward only) |
| `git.fetch` | {} |
| `git.branch` | {switch?: name} |
| `git.init` | {} |
| `plugin.list` | {} |
| `plugin.reload` | {} |
| `log.tail` | {n?, channel?: agent/log/build/lua} |
| `undo` | {} |
| `redo` | {} |
| `editor.message` | {text} show a message to the person in the editor |
| `editor.mode` | {mode: scene/graph/build/git/agent/plugins} |

`scene` defaults to the scene that is active in the editor. Coordinates: 3D in metres
(y up), `rot` in degrees [x, y, z]; 2D `rect` [x, y, w, h] in canvas pixels.
