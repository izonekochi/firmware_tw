"""
Parser for src/graphics/niche/Fonts/cubicFont.h (the CJK AdafruitGFX font blob).

Shared by measure_lz4.py (compression feasibility measurement) and
trim_font.py (glyph-set trimmer). Pure stdlib, no external deps.

The font is stored in Adafruit-GFX form:
  - cubicBitmaps[]  : concatenated 1bpp glyph bitmaps, MSB-first, per-glyph
  - cubicGlyphs[]   : GFXglyph { bitmapOffset(u16), w(u8), h(u8), xAdvance(u8), xOffset(i8), yOffset(i8) }
  - cubicFont       : GFXfont { bitmap*, glyph*, first, last, yAdvance }
  - overflowTable[] : uint16 bank thresholds; glyphs with index >= overflowTable[0]
                      live in the 2nd 64 KB bitmap bank (bitmapOffset is only u16).
                      See Applet.cpp:drawCharCJK (295-303).
  - exactMap[]      : pool of raw UTF-8 bytes, one entry per glyph index
  - exactIndex[]    : u16 start offsets into exactMap (per glyph index)
  - G[]/V[]         : FNV-1a minimal-perfect-hash tables for UTF-8 -> glyph index
"""

import math
import re


def _strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def _array_body(text, name):
    """Return the text between the braces of `<...> name[] ... = { ... };`."""
    m = re.search(r"\b" + re.escape(name) + r"\s*\[\s*\]", text)
    if not m:
        raise ValueError("array %r not found" % name)
    i = text.index("{", m.end())
    depth = 0
    for j in range(i, len(text)):
        c = text[j]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[i + 1 : j]
    raise ValueError("unbalanced braces for %r" % name)


# Match integer literals that are NOT part of a C identifier, so casts like
# "(uint8_t*)" in the GFXfont descriptor don't leak their "8" as a value.
_INT_RE = re.compile(r"(?<![A-Za-z0-9_])(-?(?:0[xX][0-9a-fA-F]+|\d+))(?![A-Za-z0-9_])")


def _ints(body):
    return [int(tok, 0) for tok in _INT_RE.findall(body)]


class Font:
    def __init__(self):
        self.bitmaps = b""          # bytes
        self.glyphs = []            # list of dict(bo,w,h,xadv,xoff,yoff)
        self.overflow = []          # bank thresholds
        self.first = 0
        self.last = 0
        self.yadv = 0
        self.exactmap = b""
        self.exactindex = []        # len == nglyphs (+1 sentinel appended here)

    @property
    def nglyphs(self):
        return len(self.glyphs)

    def bank_of(self, code):
        """Mirror drawCharCJK: number of 0x10000 banks to add for this glyph index."""
        bank = 0
        n = len(self.overflow)
        for i in range(n):
            if code >= self.overflow[i]:
                if i == n - 1:      # sentinel == glyph count => "real overflow" guard, resets
                    return 0
                bank += 1
            else:
                break
        return bank

    def glyph_len(self, g):
        return (g["w"] * g["h"] + 7) // 8

    def glyph_bytes(self, code):
        """The exact bytes drawCharCJK reads for `code`: ceil(w*h/8) from bank+bo."""
        g = self.glyphs[code]
        n = self.glyph_len(g)
        base = self.bank_of(code) * 0x10000 + g["bo"]
        return self.bitmaps[base : base + n]

    def glyph_key(self, code):
        """The raw UTF-8 byte string this glyph matches (may be a multi-codepoint emoji seq)."""
        s = self.exactindex[code]
        e = self.exactindex[code + 1]
        return self.exactmap[s:e]


def parse(path):
    with open(path, "r", encoding="utf-8") as f:
        raw = f.read()
    text = _strip_comments(raw)

    fnt = Font()
    fnt.bitmaps = bytes(_ints(_array_body(text, "cubicBitmaps")))
    fnt.exactmap = bytes(_ints(_array_body(text, "exactMap")))
    fnt.exactindex = _ints(_array_body(text, "exactIndex"))
    fnt.overflow = _ints(_array_body(text, "overflowTable"))

    gflat = _ints(_array_body(text, "cubicGlyphs"))
    if len(gflat) % 6 != 0:
        raise ValueError("cubicGlyphs not a multiple of 6 ints (%d)" % len(gflat))
    for k in range(0, len(gflat), 6):
        bo, w, h, xadv, xoff, yoff = gflat[k : k + 6]
        fnt.glyphs.append({"bo": bo, "w": w, "h": h, "xadv": xadv, "xoff": xoff, "yoff": yoff})

    fm = re.search(r"\bcubicFont\b[^=]*=\s*\{([^}]*)\}", text)
    if not fm:
        raise ValueError("cubicFont GFXfont descriptor not found")
    fvals = _ints(fm.group(1))
    fnt.first, fnt.last, fnt.yadv = fvals[0], fvals[1], fvals[2]

    # exactIndex should have one entry per glyph; append a sentinel = len(exactmap)
    # so glyph_key() can slice [idx[d]:idx[d+1]] uniformly.
    if len(fnt.exactindex) == fnt.nglyphs:
        fnt.exactindex = fnt.exactindex + [len(fnt.exactmap)]
    elif len(fnt.exactindex) == fnt.nglyphs + 1:
        pass  # already has sentinel
    else:
        # tolerate: pad/truncate defensively but warn via exception context
        raise ValueError(
            "exactIndex len %d does not match glyph count %d (+/-1)"
            % (len(fnt.exactindex), fnt.nglyphs)
        )
    return fnt
