"""
measure_lz4.py  --  Does the InkHUD map-tile LZ4 codec actually help the CJK font?

Throwaway measurement. Parses cubicFont.h, reconstructs each glyph's exact 1bpp
bitmap bytes, and measures:
  * per-glyph LZ4 (the ONLY scheme compatible with O(1) random glyph access),
    using an LZ4 block encoder whose output is verified to round-trip through a
    byte-for-byte port of the firmware decoder (MapApplet.cpp lz4_decompress).
  * per-glyph zlib(raw deflate) as a stronger-codec reference.
  * whole-blob zlib / LZ4 (the theoretical best ratio -- but RAM-blocked on device).
  * blob entropy (Shannon), to sanity-check "the data doesn't compress".

Usage:
  python tools/cjk_font/measure_lz4.py [path/to/cubicFont.h]
"""

import math
import os
import sys
import zlib
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cubic_parse

DEFAULT_HDR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "src", "graphics", "niche", "Fonts", "cubicFont.h",
)


# ---- LZ4 block codec (matches src/.../Map/MapApplet.cpp:144-188) --------------

def lz4_decompress(src, dst_cap):
    """Byte-for-byte port of the firmware's lz4_decompress. Raises on malformed input."""
    s, s_end = 0, len(src)
    d = bytearray()
    while s < s_end:
        token = src[s]; s += 1
        lit = token >> 4
        if lit == 15:
            while True:
                x = src[s]; s += 1; lit += x
                if x != 255 or s >= s_end:
                    break
        if len(d) + lit > dst_cap or s + lit > s_end:
            raise ValueError("literal overrun")
        d += src[s : s + lit]; s += lit
        if s >= s_end:
            break
        if s + 2 > s_end:
            raise ValueError("offset truncated")
        offset = src[s] | (src[s + 1] << 8); s += 2
        if offset == 0 or len(d) - offset < 0:
            raise ValueError("bad offset")
        mat = (token & 0xF) + 4
        if (token & 0xF) == 15:
            while True:
                x = src[s]; s += 1; mat += x
                if x != 255 or s >= s_end:
                    break
        if len(d) + mat > dst_cap:
            raise ValueError("match overrun")
        start = len(d) - offset
        for i in range(mat):
            d.append(d[start + i])
    return bytes(d)


def lz4_compress(src, max_candidates=64):
    """Greedy LZ4 block encoder. Output is a valid raw LZ4 block (no frame header),
    decodable by lz4_decompress. Not size-optimal, but near-optimal on tiny inputs."""
    n = len(src)
    out = bytearray()
    table = defaultdict(list)  # 4-byte key -> positions
    MIN = 4
    i = 0
    lit_start = 0

    def emit(lit_a, lit_b, mlen, off):
        litlen = lit_b - lit_a
        hi = 15 if litlen >= 15 else litlen
        if mlen == 0:
            lo = 0
        else:
            m = mlen - MIN
            lo = 15 if m >= 15 else m
        out.append((hi << 4) | lo)
        if litlen >= 15:
            r = litlen - 15
            while r >= 255:
                out.append(255); r -= 255
            out.append(r)
        out.extend(src[lit_a:lit_b])
        if mlen == 0:
            return
        out.append(off & 0xFF); out.append((off >> 8) & 0xFF)
        m = mlen - MIN
        if m >= 15:
            r = m - 15
            while r >= 255:
                out.append(255); r -= 255
            out.append(r)

    while i < n:
        best_len, best_off = 0, 0
        if i + MIN <= n:
            key = src[i : i + 4]
            cands = table.get(key)
            if cands:
                for p in cands[-max_candidates:][::-1]:
                    off = i - p
                    if off > 65535:
                        continue
                    l, maxl = 0, n - i
                    while l < maxl and src[p + l] == src[i + l]:
                        l += 1
                    if l > best_len:
                        best_len, best_off = l, off
            table[key].append(i)
        if best_len >= MIN:
            emit(lit_start, i, best_len, best_off)
            i += best_len
            lit_start = i
        else:
            i += 1
    emit(lit_start, n, 0, 0)  # trailing literals (also the required terminator)
    return bytes(out)


# ---- helpers -----------------------------------------------------------------

def entropy_bits_per_byte(data):
    if not data:
        return 0.0
    c = Counter(data)
    n = len(data)
    return -sum((v / n) * math.log2(v / n) for v in c.values())


def raw_deflate(data, level=9):
    co = zlib.compressobj(level, zlib.DEFLATED, -15)  # -15 = raw, no zlib header
    return co.compress(data) + co.flush()


def human(nbytes):
    return "%d B (%.1f KiB)" % (nbytes, nbytes / 1024.0)


# ---- main --------------------------------------------------------------------

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_HDR
    fnt = cubic_parse.parse(path)
    N = fnt.nglyphs

    print("=" * 74)
    print("CJK font compression measurement  --  %s" % os.path.normpath(path))
    print("=" * 74)
    print("glyphs            : %d  (first=%d last=%d yAdvance=%d)"
          % (N, fnt.first, fnt.last, fnt.yadv))
    print("cubicBitmaps blob : %s" % human(len(fnt.bitmaps)))
    print("overflow banks    : %s" % fnt.overflow)

    # Reconstruct per-glyph bitmap bytes; validate coverage + byte-alignment.
    glyph_data = []
    total_glyph_bytes = 0
    blank = 0
    misalign = 0
    sizes = []
    for code in range(N):
        b = fnt.glyph_bytes(code)
        glyph_data.append(b)
        total_glyph_bytes += len(b)
        sizes.append(len(b))
        if len(b) == 0:
            blank += 1
        # byte-alignment check: within a bank, next bo should be bo+len
        if code + 1 < N and fnt.bank_of(code) == fnt.bank_of(code + 1):
            if fnt.glyphs[code + 1]["bo"] != fnt.glyphs[code]["bo"] + len(b):
                misalign += 1

    sizes_sorted = sorted(sizes)
    print("sum glyph bytes   : %s   (blob is %s)"
          % (human(total_glyph_bytes), human(len(fnt.bitmaps))))
    print("glyph byte sizes  : min=%d  median=%d  mean=%.1f  max=%d"
          % (sizes_sorted[0], sizes_sorted[N // 2],
             total_glyph_bytes / N, sizes_sorted[-1]))
    print("blank glyphs      : %d" % blank)
    print("byte-align breaks : %d  (0 => each glyph starts on a byte boundary)" % misalign)
    print("blob entropy      : %.3f bits/byte  (8.0 = incompressible by a byte coder)"
          % entropy_bits_per_byte(fnt.bitmaps))
    print("-" * 74)

    # Per-glyph LZ4 (the only random-access-compatible scheme), round-trip verified.
    lz4_sum = 0
    lz4_best_sum = 0     # min(lz4, raw): a real scheme stores raw when LZ4 expands
    expanded = 0
    rt_fail = 0
    zlib_glyph_sum = 0
    for code in range(N):
        b = glyph_data[code]
        if not b:
            continue
        comp = lz4_compress(b)
        if lz4_decompress(comp, len(b)) != b:
            rt_fail += 1
        lz4_sum += len(comp)
        lz4_best_sum += min(len(comp), len(b))
        if len(comp) >= len(b):
            expanded += 1
        zlib_glyph_sum += min(len(raw_deflate(b)), len(b))

    print("PER-GLYPH (random-access compatible):")
    print("  round-trip failures        : %d  (must be 0 to trust numbers)" % rt_fail)
    print("  per-glyph LZ4, pure        : %s   (%+.1f%% vs raw)"
          % (human(lz4_sum), 100.0 * (lz4_sum - total_glyph_bytes) / total_glyph_bytes))
    print("  glyphs that EXPANDED       : %d / %d  (%.1f%%)"
          % (expanded, N, 100.0 * expanded / N))
    print("  per-glyph best(LZ4,raw)    : %s   (%+.1f%% vs raw)"
          % (human(lz4_best_sum), 100.0 * (lz4_best_sum - total_glyph_bytes) / total_glyph_bytes))
    print("    + 1 byte/glyph kind flag : %s   (%+.1f%% vs raw)"
          % (human(lz4_best_sum + N), 100.0 * (lz4_best_sum + N - total_glyph_bytes) / total_glyph_bytes))
    print("  per-glyph best(deflate,raw): %s   (%+.1f%% vs raw)  [stronger codec ref]"
          % (human(zlib_glyph_sum), 100.0 * (zlib_glyph_sum - total_glyph_bytes) / total_glyph_bytes))
    print("-" * 74)

    # Whole-blob (theoretical best ratio; BLOCKED by RAM on device -- see report).
    whole = fnt.bitmaps
    z = raw_deflate(whole)
    l4 = lz4_compress(whole)
    ok = lz4_decompress(l4, len(whole)) == whole
    print("WHOLE-BLOB (best ratio, but cannot fit decompressed in device RAM):")
    print("  deflate(level9)            : %s   (%+.1f%% vs raw)"
          % (human(len(z)), 100.0 * (len(z) - len(whole)) / len(whole)))
    print("  LZ4 (this encoder)         : %s   (%+.1f%% vs raw)%s"
          % (human(len(l4)), 100.0 * (len(l4) - len(whole)) / len(whole),
             "" if ok else "  [RT FAIL!]"))
    print("=" * 74)

    # Verdict.
    print("VERDICT")
    net = lz4_best_sum - total_glyph_bytes
    print("  Incremental metadata for a per-glyph scheme is ~0: cubicGlyphs already")
    print("  stores a per-glyph bitmapOffset; a compressed length is derivable from")
    print("  consecutive offsets. So the honest flash delta is the per-glyph number.")
    if net >= 0:
        print("  => Per-glyph LZ4 does NOT shrink the font (%+d B). Not worth it." % net)
    elif -net < 0.05 * total_glyph_bytes:
        print("  => Per-glyph LZ4 saves only %s (%.1f%%). Not worth the complexity."
              % (human(-net), 100.0 * -net / total_glyph_bytes))
    else:
        print("  => Per-glyph LZ4 saves %s (%.1f%%). Possibly worth prototyping."
              % (human(-net), 100.0 * -net / total_glyph_bytes))
    print("  (Trimming unused glyphs saves ~%d B each with zero decode cost -- see trim_font.py.)"
          % (total_glyph_bytes // N + 7 + 3))


if __name__ == "__main__":
    main()
