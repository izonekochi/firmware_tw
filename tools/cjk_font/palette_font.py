"""
palette_font.py -- shrink cubicFont.h by palette-indexing the glyph metadata.

The 7-byte GFXglyph table (bitmapOffset u16 + w,h,xAdvance,xOffset,yOffset) is
~89% one repeated (w,h,xAdvance,xOffset,yOffset) tuple. This rewrites it as:
  * cubicTuplePalette[]  : the <=256 distinct (w,h,xAdvance,xOffset,yOffset) tuples (5 B each)
  * cubicGlyphTupleIdx[] : 1 byte/glyph -> palette index
  * cubicBitmapOffset[]  : u16/glyph (kept; deriving it would cost scarce RAM)
plus an inline `cubicGlyph(code)` accessor that reconstructs the fields.
Saves ~20 KB flash, 0 RAM. Glyph indices are unchanged, so the MPH lookup and
bopomofoTable stay valid. The renderer (Applet.cpp/AppletFont.cpp) must be
updated to call cubicGlyph() instead of gfxFont->glyph[code] (see the change set).

Usage:
  python tools/cjk_font/palette_font.py            # dry-run report
  python tools/cjk_font/palette_font.py --in-place  # rewrite cubicFont.h
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cubic_parse

HDR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "..", "src", "graphics", "niche", "Fonts", "cubicFont.h")


def _wrap(items, per_line, indent="  "):
    out = []
    for i in range(0, len(items), per_line):
        out.append(indent + ", ".join(items[i:i + per_line]))
    return ",\n".join(out)


def build_block(font):
    # Palette ordered by frequency (common tuple first — purely cosmetic).
    from collections import Counter
    freq = Counter((g["w"], g["h"], g["xadv"], g["xoff"], g["yoff"]) for g in font.glyphs)
    palette = [t for t, _ in freq.most_common()]
    idx_of = {t: i for i, t in enumerate(palette)}
    if len(palette) > 256:
        sys.exit("REFUSING: %d distinct tuples (>256); would need a u16 index." % len(palette))

    tuple_idx = [idx_of[(g["w"], g["h"], g["xadv"], g["xoff"], g["yoff"])] for g in font.glyphs]
    bmp_off = [g["bo"] for g in font.glyphs]

    # Verify each glyph reconstructs byte-identically from the palette.
    for i, g in enumerate(font.glyphs):
        w, h, xa, xo, yo = palette[tuple_idx[i]]
        assert (w, h, xa, xo, yo) == (g["w"], g["h"], g["xadv"], g["xoff"], g["yoff"]), i
        assert bmp_off[i] == g["bo"], i

    pal_rows = ",\n".join(
        "  {%d, %d, %d, %d, %d}" % (w, h, xa, xo, yo) for (w, h, xa, xo, yo) in palette)

    block = []
    block.append("// Glyph metrics, palette-indexed (replaces the per-glyph GFXglyph table).")
    block.append("// ~89%% of glyphs share one (w,h,xAdvance,xOffset,yOffset) tuple, so we store a")
    block.append("// small palette + a 1-byte index per glyph, plus the per-glyph bitmap offset.")
    block.append("// Glyph indices are unchanged; call cubicGlyph(code) in place of gfxFont->glyph[code].")
    block.append("struct CubicTuple { uint8_t width, height, xAdvance; int8_t xOffset, yOffset; };")
    block.append("const CubicTuple cubicTuplePalette[] = {")
    block.append(pal_rows)
    block.append("};")
    block.append("const uint8_t cubicGlyphTupleIdx[] = {")
    block.append(_wrap([str(v) for v in tuple_idx], 24))
    block.append("};")
    block.append("const uint16_t cubicBitmapOffset[] = {")
    block.append(_wrap([str(v) for v in bmp_off], 20))
    block.append("};")
    block.append("static_assert(sizeof(cubicTuplePalette) / sizeof(cubicTuplePalette[0]) <= 256,")
    block.append('              "tuple palette must be indexable by uint8_t");')
    block.append("struct CubicGlyphMetrics { uint16_t bitmapOffset; uint8_t width, height, xAdvance; int8_t xOffset, yOffset; };")
    block.append("static inline CubicGlyphMetrics cubicGlyph(uint16_t code) {")
    block.append("  const CubicTuple &t = cubicTuplePalette[cubicGlyphTupleIdx[code]];")
    block.append("  return CubicGlyphMetrics{cubicBitmapOffset[code], t.width, t.height, t.xAdvance, t.xOffset, t.yOffset};")
    block.append("}")
    return "\n".join(block), palette, tuple_idx, bmp_off


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("header", nargs="?", default=HDR)
    ap.add_argument("--in-place", action="store_true")
    ap.add_argument("-o", "--out", default=None)
    args = ap.parse_args()

    with open(args.header, "r", encoding="utf-8") as fh:
        text = fh.read()
    if "cubicTuplePalette" in text or "const GFXglyph cubicGlyphs" not in text:
        sys.exit("Header is already palette-format (or has no cubicGlyphs[]). Nothing to do.")

    font = cubic_parse.parse(args.header)
    N = font.nglyphs
    block, palette, tuple_idx, bmp_off = build_block(font)

    # Splice: replace the whole `const GFXglyph cubicGlyphs[] PROGMEM = { ... };` decl.
    m = re.search(r"const\s+GFXglyph\s+cubicGlyphs\s*\[\s*\]\s*PROGMEM\s*=\s*\{", text)
    if not m:
        sys.exit("could not locate cubicGlyphs[] declaration")
    i = text.index("{", m.start())
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                # consume trailing "};" and the generator's dangling "// 0x.. 'char'" comment
                end = j + 1
                while end < len(text) and text[end] in " ;\r":
                    end += 1
                if text[end:end + 2] == "//":
                    nl = text.find("\n", end)
                    end = nl + 1 if nl != -1 else len(text)
                break
    out = text[: m.start()] + block + text[end:]

    # cubicFont GFXfont no longer has a glyph array -> null the glyph pointer.
    out2 = re.sub(r"\(GFXglyph\s*\*\)\s*cubicGlyphs", "(GFXglyph *)nullptr", out, count=1)
    if out2 == out:
        sys.exit("could not repoint cubicFont.glyph")
    out = out2

    old_meta = 7 * N
    new_meta = 2 * N + 1 * N + len(palette) * 5
    print("=" * 60)
    print("Palette-index cubicFont.h")
    print("=" * 60)
    print("glyphs           : %d" % N)
    print("distinct tuples  : %d  (palette = %d B)" % (len(palette), len(palette) * 5))
    print("glyph metadata   : %d B -> %d B  (save %d B = %.1f KB)"
          % (old_meta, new_meta, old_meta - new_meta, (old_meta - new_meta) / 1024))
    print("verify           : PASS (all %d glyphs reconstruct byte-identically)" % N)

    if not args.in_place and not args.out:
        print("\n(dry run -- pass --in-place or -o OUT to write)")
        return
    dest = args.header if args.in_place else args.out
    with open(dest, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(out)
    print("\nwrote %s (%d bytes)" % (os.path.normpath(dest), len(out)))
    print("NEXT: update Applet.cpp (drawCharCJK/writeCJK/charBoundsCJK) and")
    print("      AppletFont.cpp (glyph scan + spaceCharWidth) to use cubicGlyph(code).")


if __name__ == "__main__":
    main()
