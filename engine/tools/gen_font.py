#!/usr/bin/env python3
"""Bakes a pixel font into engine/resources/fonts/mx12.mxf for engine/render/font.c.

The editor (and any tool built on the engine) needs to show and type Chinese, so the
whole GB2312 set is baked, not just the characters the game happens to use.

    python3 engine/tools/gen_font.py <font.otf|ttf> [out.mxf]

The shipped mx12.mxf was baked from Fusion Pixel Font 12px proportional (zh_hans) by
TakWolf (SIL OFL 1.1, https://github.com/TakWolf/fusion-pixel-font). Any 12 px pixel
font works; respect its licence (and keep its notice next to the output).

File layout (little endian):
    char magic[4] "MXF1"
    u32 glyph_count
    u16 cell_w, cell_h, line_h, ascent
    glyph_count x { u32 codepoint; u8 advance; u8 width; u16 reserved }   sorted by codepoint
    glyph_count x cell_h rows x 2 bytes (16 px, MSB = leftmost pixel)
"""
import os
import shutil
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_OUT = os.path.join(ROOT, "engine", "resources", "fonts", "mx12.mxf")
SIZE = 12

EXTRA = ("…—–“”‘’·•→←↑↓↔★☆●○■□▲△▼▽◆◇✓✗×÷±°№§¶©®™"
         "　、。，．：；？！（）【】《》〈〉「」『』〔〕＋－＝＜＞％＃＆＊＠＿｜～￥"
         "①②③④⑤⑥⑦⑧⑨⑩"
         "█▓▒░▀▄▌▐▖▗▘▝▚▞─│┌┐└┘├┤┬┴┼═║╔╗╚╝╠╣╦╩╬▶◀▷◁►◄")


def decode_table(codec, his, los):
    out = []
    for hi in his:
        for lo in los:
            try:
                out.append(bytes([hi, lo]).decode(codec))
            except UnicodeDecodeError:
                pass
    return out


def charset():
    """Every language the engine's UI speaks: English (with Latin-1), simplified Chinese
    (GB2312), traditional Chinese (Big5 level 1), Japanese (JIS X 0208: kana and kanji),
    Korean (all 11,172 Hangul syllables plus jamo)."""
    chars = [chr(c) for c in range(32, 127)] + [chr(c) for c in range(0xA0, 0x100)]
    chars += decode_table("gb2312", range(0xA1, 0xF8), range(0xA1, 0xFF))
    chars += decode_table("big5", range(0xA1, 0xC7), list(range(0x40, 0x7F)) + list(range(0xA1, 0xFF)))
    chars += decode_table("euc_jp", range(0xA1, 0xF5), range(0xA1, 0xFF))
    chars += [chr(c) for c in range(0xAC00, 0xD7A4)]          # Hangul syllables
    chars += [chr(c) for c in range(0x3131, 0x318F)]          # Hangul compatibility jamo
    chars += [chr(c) for c in range(0x3041, 0x3097)] + [chr(c) for c in range(0x30A0, 0x3100)]
    chars.extend(EXTRA)
    seen, out = set(), []
    for ch in chars:
        if ch not in seen:
            seen.add(ch)
            out.append(ch)
    return sorted(out, key=ord)


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip())
        return
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUT
    font = ImageFont.truetype(src, SIZE)
    ascent, descent = font.getmetrics()
    cell_h = ascent + descent
    cell_w = 16
    glyphs = []
    for ch in charset():
        if ch != " " and font.getmask(ch).getbbox() is None:
            continue  # the font lacks it
        adv = int(round(font.getlength(ch)))
        img = Image.new("1", (cell_w, cell_h), 0)
        d = ImageDraw.Draw(img)
        d.fontmode = "1"
        d.text((0, 0), ch, font=font, fill=1)
        box = img.getbbox()
        width = box[2] if box else 0
        rows = []
        px = img.load()
        for y in range(cell_h):
            bits = 0
            for x in range(cell_w):
                if px[x, y]:
                    bits |= 0x8000 >> x
            rows.append(bits)
        glyphs.append((ord(ch), max(1, min(adv, 255)), width, rows))

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(b"MXF1")
        f.write(struct.pack("<I4H", len(glyphs), cell_w, cell_h, cell_h + 2, ascent))
        for cp, adv, width, _ in glyphs:
            f.write(struct.pack("<IBBH", cp, adv, width, 0))
        for _, _, _, rows in glyphs:
            f.write(struct.pack(">%dH" % cell_h, *rows))
    lic_src = os.path.join(os.path.dirname(src), "OFL.txt")
    if os.path.exists(lic_src):
        shutil.copy(lic_src, os.path.join(os.path.dirname(out), "OFL.txt"))
    print("gen_font: %d glyphs, cell %dx%d -> %s" % (len(glyphs), cell_w, cell_h,
                                                     os.path.relpath(out, ROOT)))


if __name__ == "__main__":
    main()
