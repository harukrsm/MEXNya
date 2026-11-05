#!/usr/bin/env python3
"""Wavefront OBJ (+ MTL) -> MEXNya model (.mcm).

    python3 engine/tools/obj2mcm.py model.obj assets/models/model.mcm [--scale 1] [--ground]
                                    [--max-tex 256] [--light]

- Materials: `Kd` becomes the vertex colour; `map_Kd` becomes a 16-colour texture
  (Pillow quantises it; sizes are rounded to the format's powers of two, at most
  --max-tex). `d`/`Tr` < 1 or a texture with transparency gives alpha-tested pixels.
- Each `o`/`g` group becomes a model part (drawn and posed separately by the engine).
- --ground moves the model so its lowest point sits at y = 0, centred on x and z.
- --light bakes simple top-down shading into the vertex colours (flat-looking models
  read better with it; the engine itself does no lighting on models).
Faces with more than three corners are fanned into triangles.
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mcm  # noqa: E402


def load_mtl(path):
    mats, cur = {}, None
    if not os.path.exists(path):
        return mats
    for line in open(path, encoding="utf-8", errors="replace"):
        p = line.split()
        if not p:
            continue
        if p[0] == "newmtl":
            cur = mats.setdefault(" ".join(p[1:]), {"kd": (1, 1, 1), "map": None, "d": 1.0})
        elif cur is None:
            continue
        elif p[0] == "Kd" and len(p) >= 4:
            cur["kd"] = tuple(float(x) for x in p[1:4])
        elif p[0] == "map_Kd":
            cur["map"] = os.path.join(os.path.dirname(path), p[-1])
        elif p[0] == "d":
            cur["d"] = float(p[1])
        elif p[0] == "Tr":
            cur["d"] = 1.0 - float(p[1])
    return mats


def pow2(n, lo, hi):
    v = lo
    while v < n and v < hi:
        v *= 2
    return v


def make_texture(path, max_size):
    from PIL import Image
    img = Image.open(path).convert("RGBA")
    w = pow2(img.width, 32, max_size)
    h = pow2(img.height, 8, max_size)
    img = img.resize((w, h), Image.NEAREST)
    alpha = any(a < 128 for a in img.getchannel("A").getdata())
    rgb = img.convert("RGB").quantize(colors=15 if alpha else 16, method=Image.MEDIANCUT)
    pal = rgb.getpalette()[: 3 * (15 if alpha else 16)]
    colours = [(pal[i] << 16) | (pal[i + 1] << 8) | pal[i + 2] for i in range(0, len(pal), 3)]
    tex = mcm.Texture(os.path.splitext(os.path.basename(path))[0][:15], w, h,
                      ([0] + colours) if alpha else colours, alpha0=alpha)
    px = rgb.load()
    a = img.getchannel("A").load()
    for y in range(h):
        for x in range(w):
            idx = px[x, y]
            tex.set(x, y, 0 if alpha and a[x, y] < 128 else (idx + 1 if alpha else idx))
    return tex, alpha


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("obj")
    ap.add_argument("out")
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--ground", action="store_true")
    ap.add_argument("--max-tex", type=int, default=256)
    ap.add_argument("--light", action="store_true")
    a = ap.parse_args()

    vs, vts, groups, mats = [], [], {}, {}
    cur_mat, cur_group = None, "root"
    for line in open(a.obj, encoding="utf-8", errors="replace"):
        p = line.split()
        if not p:
            continue
        if p[0] == "v":
            vs.append([float(x) * a.scale for x in p[1:4]])
        elif p[0] == "vt":
            vts.append((float(p[1]), float(p[2]) if len(p) > 2 else 0.0))
        elif p[0] == "mtllib":
            mats.update(load_mtl(os.path.join(os.path.dirname(a.obj), " ".join(p[1:]))))
        elif p[0] == "usemtl":
            cur_mat = " ".join(p[1:])
        elif p[0] in ("o", "g"):
            cur_group = (" ".join(p[1:]) or "part")[:15]
        elif p[0] == "f":
            corners = []
            for c in p[1:]:
                k = c.split("/")
                vi = int(k[0])
                ti = int(k[1]) if len(k) > 1 and k[1] else 0
                corners.append((vi - 1 if vi > 0 else len(vs) + vi, (ti - 1 if ti > 0 else len(vts) + ti) if ti else None))
            for i in range(1, len(corners) - 1):
                groups.setdefault(cur_group, []).append((cur_mat, corners[0], corners[i], corners[i + 1]))
    if not vs:
        sys.exit("obj2mcm: no vertices in " + a.obj)
    if a.ground:
        lo = [min(v[i] for v in vs) for i in range(3)]
        hi = [max(v[i] for v in vs) for i in range(3)]
        off = ((lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2)
        vs = [[v[0] - off[0], v[1] - off[1], v[2] - off[2]] for v in vs]

    model = mcm.Model()
    tex_cache = {}
    for name, faces in groups.items():
        part = model.part(name)
        for mat_name, *tri in faces:
            mat = mats.get(mat_name, {"kd": (0.8, 0.8, 0.8), "map": None, "d": 1.0})
            tex, alpha = None, False
            if mat["map"] and os.path.exists(mat["map"]):
                if mat["map"] not in tex_cache:
                    tex_cache[mat["map"]] = make_texture(mat["map"], a.max_tex)
                tex, alpha = tex_cache[mat["map"]]
            shade = 1.0
            if a.light:
                p0, p1, p2 = (vs[c[0]] for c in tri)
                e1 = [p1[i] - p0[i] for i in range(3)]
                e2 = [p2[i] - p0[i] for i in range(3)]
                n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
                ln = math.sqrt(sum(x * x for x in n)) or 1.0
                shade = 0.45 + 0.55 * max(0.0, (n[0] * 0.42 + n[1] * 0.81 + n[2] * 0.40) / ln)
            kd = mat["kd"] if tex is None else (1, 1, 1)
            col = mcm.pack_colour(kd[0] * shade, kd[1] * shade, kd[2] * shade, 1.0)
            verts = []
            for vi, ti in tri:
                u, v = vts[ti] if ti is not None and ti < len(vts) else (0.0, 0.0)
                verts.append((u, 1.0 - v, col, tuple(vs[vi])))
            flags = mcm.BATCH_ALPHA if alpha or mat["d"] < 1.0 else 0
            model.tri(part, tex, verts, flags)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    model.write(a.out)
    print("obj2mcm: %d triangles, %d parts, %d textures -> %s" % (model.triangle_count(), len(model.parts),
                                                                   len(model.textures), a.out))


if __name__ == "__main__":
    main()
