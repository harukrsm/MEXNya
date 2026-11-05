#!/usr/bin/env bash
# MEXNya self-test: static checks, then the engine, the editor (headless) and the sample
# game, each rendering a frame. Run after a debug build:
#     python3 engine/tools/mexnya.py test      (builds build/debug first)
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD="${BUILD:-build/debug}"
OUT="$BUILD/test-out"
rm -rf "$OUT" && mkdir -p "$OUT"

python3 scripts/check_boundaries.py
python3 scripts/check_i18n.py

"$BUILD/runtime_test" "$PWD"

mkdir -p "$OUT/core"
"$BUILD/core_test" "$OUT/core"
echo "core OK"

mkdir -p "$OUT/atomic"
"$BUILD/atomic_test" "$OUT/atomic"
echo "atomic OK"

"$BUILD/timestep_test"
echo "timestep OK"

"$BUILD/engine-smoke" --headless --frames 30 --shot "$OUT/smoke.bmp" > "$OUT/smoke.log"
grep -q "engine-smoke: 30 frames" "$OUT/smoke.log"
echo "engine smoke OK"

# The editor's operation layer, headless, on a fresh project.
P="$OUT/project"
mkdir -p "$P" && echo '{"name": "Test"}' > "$P/mexnya.json"
E="$BUILD/MexnyaEditor"
"$E" --project "$P" --exec '{"op": "project.get"}' > "$OUT/op1.json"
grep -q '"ok": true' "$OUT/op1.json"
"$E" --project "$P" --as test --exec '{"op": "object.add", "args": {"type": "box", "id": "crate", "pos": [0, 0.5, 0]}}' > "$OUT/op2.json"
grep -q '"id": "crate"' "$OUT/op2.json"
"$E" --project "$P" --as test --exec '{"op": "note.add", "args": {"kind": "box", "text": "check", "shape": {"rect": [200, 120, 160, 160]}}}' > "$OUT/op3.json"
grep -q '"targets": \[.*"crate"' "$OUT/op3.json"
"$E" --project "$P" --exec '{"op": "view.snapshot", "args": {"path": "snapshots/t.png"}}' > "$OUT/op4.json"
test -s "$P/mexnya/snapshots/t.png"
# Undo lives in one session: add and undo in one batch, the object must be gone.
"$E" --project "$P" --as test --exec '{"batch": [{"op": "object.add", "args": {"type": "sphere", "id": "tmp"}}, {"op": "undo"}]}' > "$OUT/op5.json"
grep -q '"ok": true' "$OUT/op5.json"
"$E" --project "$P" --exec '{"op": "object.get", "args": {"id": "tmp"}}' > "$OUT/op6.json" && exit 1 || true
grep -q '"op":"note.add"' "$P/mexnya/agent/log.jsonl"
echo "editor operations OK"

# The sample game renders its scenes.
"$BUILD/samples/hello/hello" --headless --frames 20 --shot "$OUT/hello.png" > "$OUT/hello.log"
test -s "$OUT/hello.png"
echo "sample game OK"
echo "all tests OK"
