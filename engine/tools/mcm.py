"""Writer for MEXNya engine binary assets (used by obj2mcm.py and your own tools).

.mcm  model:   textures + parts + batches + vertices
.mct  texture: a single 4-bit texture

All little-endian. Layout matches engine/render/model.h.

    McmHeader  (64 bytes)
      char  magic[4]        "MCM1" / "MCT1"
      u32   tex_count, part_count, batch_count, vert_count
      u32   tex_offset, part_offset, batch_offset, vert_offset
      float bounds_min[3], bounds_max[3]
      u32   reserved
    McmTexture (16 bytes)  u16 w, h; u32 data_offset; u32 clut_offset; u32 flags
    McmPart    (40 bytes)  char name[16]; float pivot[3]; i32 parent; u32 first_batch, batch_count
    McmBatch   (16 bytes)  i32 tex; u32 first_vertex, vertex_count, flags
    vertex     (24 bytes)  float u, v; u32 colour (ABGR); float x, y, z

Each texture's data block is 16-byte aligned: the CLUT
(16 x u32 ABGR) followed by 4-bit texels (low nibble = left texel).
"""
import struct

MAGIC_MODEL = b"MCM1"
MAGIC_TEXTURE = b"MCT1"

# Batch flags (must match render/model.h)
BATCH_NOFOG = 1      # emissive bits (lamp heads, tail lights): ignore fog
BATCH_ADDITIVE = 2   # light cones / glows: additive blend, no depth write
BATCH_ALPHA = 4      # alpha-tested cut-outs (palette index 0 transparent)
BATCH_DOUBLE = 8     # no back-face culling (thin or hollow shells seen from inside)
BATCH_STRIP = 32     # world chunks: the batch is one triangle strip (degenerate joins), not a list

# Texture flags
TEX_ALPHA0 = 1       # palette entry 0 is fully transparent


def rgb_to_abgr(rgb, alpha=255):
    r, g, b = (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF
    return (alpha << 24) | (b << 16) | (g << 8) | r


def pack_colour(r, g, b, a=1.0):
    """Floats 0..1 -> ABGR u32 (vertex colour)."""
    c = lambda x: max(0, min(255, int(round(x * 255))))
    return (c(a) << 24) | (c(b) << 16) | (c(g) << 8) | c(r)


class Texture:
    """A 4-bit indexed texture. `palette` is up to 16 0xRRGGBB values."""

    def __init__(self, name, w, h, palette, alpha0=False):
        # 4-bit rows must be at least 16 bytes (32 texels) wide for the format.
        assert w in (32, 64, 128, 256, 512) and h in (8, 16, 32, 64, 128, 256, 512)
        assert len(palette) <= 16
        self.name = name
        self.w, self.h = w, h
        self.palette = list(palette) + [0] * (16 - len(palette))
        self.alpha0 = alpha0
        self.px = [[0] * w for _ in range(h)]

    def set(self, x, y, idx):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[int(y)][int(x)] = idx

    def clut(self):
        out = []
        for i, c in enumerate(self.palette):
            out.append(rgb_to_abgr(c, 0 if (self.alpha0 and i == 0) else 255))
        return out

    def data(self):
        b = bytearray()
        for y in range(self.h):
            for x in range(0, self.w, 2):
                b.append((self.px[y][x] & 0xF) | ((self.px[y][x + 1] & 0xF) << 4))
        return bytes(b)

    def to_image(self):
        from PIL import Image
        img = Image.new("P", (self.w, self.h))
        pal = []
        for c in self.palette:
            pal += [(c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF]
        img.putpalette(pal)
        img.putdata([self.px[y][x] for y in range(self.h) for x in range(self.w)])
        return img

    def sample_rgb(self, u, v):
        x = int(u * self.w) % self.w
        y = int(v * self.h) % self.h
        c = self.palette[self.px[y][x]]
        return ((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF), (self.alpha0 and self.px[y][x] == 0)


class Model:
    """Parts hold batches; a batch is a run of triangles sharing a texture and flags."""

    def __init__(self):
        self.textures = []
        self.parts = []  # [name, pivot, parent, {(tex, flags): [verts]}]

    def texture_index(self, tex):
        if tex is None:
            return -1
        if tex not in self.textures:
            self.textures.append(tex)
        return self.textures.index(tex)

    def part(self, name, pivot=(0.0, 0.0, 0.0), parent=-1):
        self.parts.append([name, pivot, parent, {}])
        return len(self.parts) - 1

    def tri(self, part, tex, verts, flags=0):
        """verts: three (u, v, abgr, (x, y, z)) tuples, CCW seen from the front."""
        key = (self.texture_index(tex), flags)
        self.parts[part][3].setdefault(key, []).extend(verts)

    def triangle_count(self):
        return sum(len(v) for p in self.parts for v in p[3].values()) // 3

    def write(self, path):
        write_file(path, MAGIC_MODEL, self.textures, self.parts)


def _bounds(parts):
    lo = [1e30] * 3
    hi = [-1e30] * 3
    for p in parts:
        for verts in p[3].values():
            for v in verts:
                for i in range(3):
                    lo[i] = min(lo[i], v[3][i])
                    hi[i] = max(hi[i], v[3][i])
    if lo[0] > hi[0]:
        lo, hi = [0.0] * 3, [0.0] * 3
    return lo, hi


def _align(n, a=16):
    return (n + a - 1) & ~(a - 1)


def write_file(path, magic, textures, parts):
    batches = []
    verts = []
    part_recs = []
    for name, pivot, parent, groups in parts:
        first_batch = len(batches)
        for (tex, flags), vs in groups.items():
            batches.append((tex, len(verts), len(vs), flags))
            verts.extend(vs)
        part_recs.append((name, pivot, parent, first_batch, len(batches) - first_batch))

    HDR, TEXREC, PARTREC, BATCHREC, VERT = 64, 16, 40, 16, 24
    tex_offset = HDR
    part_offset = tex_offset + TEXREC * len(textures)
    batch_offset = part_offset + PARTREC * len(part_recs)
    vert_offset = _align(batch_offset + BATCHREC * len(batches))
    data_offset = _align(vert_offset + VERT * len(verts))

    out = bytearray()
    lo, hi = _bounds(parts)
    out += struct.pack("<4s8I6fI", magic, len(textures), len(part_recs), len(batches), len(verts),
                       tex_offset, part_offset, batch_offset, vert_offset, *lo, *hi, 0)
    tex_blobs = []
    off = data_offset
    for t in textures:
        clut = struct.pack("<16I", *t.clut())
        blob = clut + t.data()
        out += struct.pack("<HHIII", t.w, t.h, off + len(clut), off, 1 if t.alpha0 else 0)
        tex_blobs.append(blob)
        off = _align(off + len(blob))
    for name, pivot, parent, fb, bc in part_recs:
        out += struct.pack("<16s3fiII", name.encode()[:15], *pivot, parent, fb, bc)
    for tex, first, count, flags in batches:
        out += struct.pack("<iIII", tex, first, count, flags)
    out += b"\0" * (vert_offset - len(out))
    for u, v, col, p in verts:
        out += struct.pack("<ffIfff", u, v, col, p[0], p[1], p[2])
    out += b"\0" * (data_offset - len(out))
    for blob in tex_blobs:
        out += blob
        out += b"\0" * (_align(len(out)) - len(out))
    with open(path, "wb") as f:
        f.write(out)


def write_texture(path, tex):
    write_file(path, MAGIC_TEXTURE, [tex], [])


def render_preview(model, path, size=200, views=(0, 35, 90, 180), zoom=0.78, centre_y=0.55,
                   bg=(0x34, 0x38, 0x3C)):
    """Software rasteriser for quick reviews: textured, Gouraud, nearest sampling."""
    from PIL import Image
    img = Image.new("RGB", (size * len(views), size), bg)
    pix = img.load()
    for vi, deg in enumerate(views):
        import math
        zbuf = [[-1e9] * size for _ in range(size)]
        a = math.radians(deg)
        ca, sa = math.cos(a), math.sin(a)

        def xf(p):
            x = p[0] * ca + p[2] * sa
            z = -p[0] * sa + p[2] * ca
            return size / 2 + x * size * zoom, size / 2 - (p[1] - centre_y) * size * zoom, z

        for name, pivot, parent, groups in model.parts:
            for (ti, flags), vs in groups.items():
                tex = model.textures[ti] if ti >= 0 else None
                for i in range(0, len(vs), 3):
                    tri = vs[i:i + 3]
                    s = [xf(v[3]) for v in tri]
                    area = (s[1][0] - s[0][0]) * (s[2][1] - s[0][1]) - (s[2][0] - s[0][0]) * (s[1][1] - s[0][1])
                    if area >= 0:
                        continue
                    minx = max(int(min(q[0] for q in s)), 0)
                    maxx = min(int(max(q[0] for q in s)) + 1, size - 1)
                    miny = max(int(min(q[1] for q in s)), 0)
                    maxy = min(int(max(q[1] for q in s)) + 1, size - 1)
                    cols = [((v[2] & 0xFF) / 255, ((v[2] >> 8) & 0xFF) / 255, ((v[2] >> 16) & 0xFF) / 255)
                            for v in tri]
                    for y in range(miny, maxy + 1):
                        for x in range(minx, maxx + 1):
                            fx, fy = x + 0.5, y + 0.5
                            w0 = ((s[1][0] - fx) * (s[2][1] - fy) - (s[2][0] - fx) * (s[1][1] - fy)) / area
                            w1 = ((s[2][0] - fx) * (s[0][1] - fy) - (s[0][0] - fx) * (s[2][1] - fy)) / area
                            w2 = 1.0 - w0 - w1
                            if w0 < 0 or w1 < 0 or w2 < 0:
                                continue
                            z = w0 * s[0][2] + w1 * s[1][2] + w2 * s[2][2]
                            if z <= zbuf[y][x]:
                                continue
                            if tex:
                                u = w0 * tri[0][0] + w1 * tri[1][0] + w2 * tri[2][0]
                                v = w0 * tri[0][1] + w1 * tri[1][1] + w2 * tri[2][1]
                                t, transparent = tex.sample_rgb(u, v)
                                if transparent:
                                    continue
                            else:
                                t = (255, 255, 255)
                            zbuf[y][x] = z
                            c = [w0 * cols[0][k] + w1 * cols[1][k] + w2 * cols[2][k] for k in range(3)]
                            pix[vi * size + x, y] = tuple(int(t[k] * c[k]) for k in range(3))
    img.save(path)
