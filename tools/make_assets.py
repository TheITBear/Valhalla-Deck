# Genera assets.h per il Deck: font (formato GFXfont 1 bit) + logo Valhalla (alfa 4 bit).
# uso: python3 -I make_assets.py font.otf logo.png out.h   (logo '-' = tiene il logo gia' presente in out.h)
import sys
from PIL import Image, ImageDraw, ImageFont
font_path, logo_path, out_path = sys.argv[1:4]
out = ["// Generato da tools/make_assets.py - non modificare a mano.", "#pragma once", "#include <stdint.h>", ""]

def gfx_font(name, size):
    f = ImageFont.truetype(font_path, size)
    asc, desc = f.getmetrics()
    bits, glyphs = [], []
    for c in range(0x20, 0x7F):
        ch = chr(c)
        x0, y0, x1, y1 = f.getbbox(ch, anchor='ls')
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        adv = int(round(f.getlength(ch)))
        off = len(bits)
        if w and h:
            im = Image.new('L', (w, h), 0)
            ImageDraw.Draw(im).text((-x0, -y0), ch, font=f, fill=255, anchor='ls')
            px = im.load()
            for y in range(h):
                for x in range(w):
                    bits.append(1 if px[x, y] >= 110 else 0)
        glyphs.append((off, w, h, adv, x0, y0))
    # gli offset del GFXfont sono in byte: ogni glifo parte allineato al byte
    by2, g2, pos = bytearray(), [], 0
    for (off, w, h, adv, xo, yo) in glyphs:
        n = w * h
        gb = bits[off:off + n]
        start = len(by2)
        for i in range(0, n, 8):
            v = 0
            for j, b in enumerate(gb[i:i + 8]):
                if b: v |= 0x80 >> j
            by2.append(v)
        g2.append((start, w, h, adv, xo, yo))
    out.append(f"static const uint8_t {name}_bmp[] PROGMEM = {{" + ",".join(str(b) for b in by2) + "};")
    out.append(f"static const lgfx::GFXglyph {name}_gl[] PROGMEM = {{" + ",".join("{%d,%d,%d,%d,%d,%d}" % g for g in g2) + "};")
    out.append(f"static const lgfx::GFXfont {name}((uint8_t*){name}_bmp, (lgfx::GFXglyph*){name}_gl, 0x20, 0x7E, {asc + desc});")
    out.append("")
    return len(by2)

sizes = {"E16": 16, "E22": 22, "E30": 30, "E64": 64}
tot = sum(gfx_font(n, s) for n, s in sizes.items())

if logo_path == '-':
    out += [l for l in open(out_path).read().split("\n") if l.startswith(("static const int LOGO_W", "static const uint8_t LOGO_A"))]
    assert len(out[-2:]) == 2 and out[-1].startswith("static const uint8_t LOGO_A"), "nessun logo in " + out_path
    open(out_path, 'w').write("\n".join(out) + "\n")
    print("font bytes", tot, "logo riusato")
else:
    # logo: arancio su bianco -> alfa dal canale verde (bianco G=255, arancio G~77); solo l'emblema, senza scritta
    img = Image.open(logo_path).convert('RGB')
    W, H = img.size
    a = Image.new('L', img.size)
    src, dst = img.load(), a.load()
    for y in range(H):
        for x in range(W):
            r, g, b = src[x, y]
            v = (255 - g) / (255 - 77)
            dst[x, y] = max(0, min(255, int(v * 255)))
    emb = a.crop((0, 0, int(W * 0.9), int(H * 0.665)))   # a destra il file ha una barra spuria
    bb = emb.getbbox(); emb = emb.crop(bb)
    LW = 150
    LH = round(emb.height * LW / emb.width)
    emb = emb.resize((LW, LH), Image.LANCZOS)
    p = emb.load()
    nib = [p[x, y] >> 4 for y in range(LH) for x in range(LW)]
    pk = bytearray((nib[i] << 4) | (nib[i + 1] if i + 1 < len(nib) else 0) for i in range(0, len(nib), 2))
    out.append(f"static const int LOGO_W = {LW}, LOGO_H = {LH};   // alfa 4 bit, 2 pixel per byte")
    out.append("static const uint8_t LOGO_A[] PROGMEM = {" + ",".join(str(b) for b in pk) + "};")
    open(out_path, 'w').write("\n".join(out) + "\n")
    emb.save(out_path + ".logo.png")
    print("font bytes", tot, "logo", LW, LH, len(pk))
