"""
trim_font.py -- drop a top-suffix block of glyphs (default: emoji) from cubicFont.h
to save flash, WITHOUT touching the Bopomofo IME table.

Safe by construction: it only removes glyphs that form a contiguous suffix of the
glyph-index space (the emoji block is indices 5186..5475, the highest indices, and
no Bopomofo IME candidate references them). Because no kept glyph is renumbered,
`bopomofoTable`, `hash()` and `lookup()` are left byte-for-byte unchanged. The
minimal-perfect-hash (G[]/V[]/exactMap/exactIndex) IS regenerated so that removed
keys resolve to -1 (clean fallback char) instead of a now-out-of-range index.

What changes in the emitted header:
  cubicBitmaps[]  truncated   cubicGlyphs[]  truncated
  exactMap[] exactIndex[]     regenerated    G[] V[]        regenerated (same G_size/V_size)
  overflowTable[] + OVERFLOW_TABLE_SIZE   recomputed for the smaller blob
  GFXfont cubicFont `last`    updated
Unchanged: hash(), lookup(), bopomofoTable[], V_size/G_size.

Usage:
  python tools/cjk_font/trim_font.py                 # dry run (report only)
  python tools/cjk_font/trim_font.py -o out.h        # write trimmed header
  python tools/cjk_font/trim_font.py --in-place      # overwrite cubicFont.h
  python tools/cjk_font/trim_font.py --drop-min 0x1F000 --drop-max 0x1FAFF
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cubic_parse

HDR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "..", "src", "graphics", "niche", "Fonts", "cubicFont.h")
FNV_PRIME = 0x01000193
INT16_MAX = 0x7FFF


def fnv(d, key):
    """Byte-exact port of hash() in cubicFont.h."""
    if d == 0:
        d = FNV_PRIME
    for b in key:
        d = ((d * FNV_PRIME) ^ b) & 0xFFFFFFFF
    return d


def gen_mph(keys, values, g_size, v_size):
    """CHD-style perfect hash matching the runtime lookup():
        d = G[hash(0,key) % g_size]; slot = d<0 ? -d-1 : hash(d,key) % v_size; value = V[slot]
    Empty V slots are 0 (a lookup landing there fails strncmp -> -1). Seeds are bounded
    to int16 so the generated G[]/V[] fit `const int16_t`."""
    buckets = [[] for _ in range(g_size)]
    for i, k in enumerate(keys):
        buckets[fnv(0, k) % g_size].append(i)
    order = sorted(range(g_size), key=lambda b: len(buckets[b]), reverse=True)

    G = [0] * g_size
    V = [0] * v_size
    used = [False] * v_size

    for b in order:
        bucket = buckets[b]
        if len(bucket) <= 1:
            break                      # rest are singletons/empties
        d, item, slots, seen = 1, 0, [], set()
        while item < len(bucket):
            slot = fnv(d, keys[bucket[item]]) % v_size
            if used[slot] or slot in seen:
                d += 1
                if d > INT16_MAX:
                    raise RuntimeError("seed exceeded int16 for bucket size %d" % len(bucket))
                item, slots, seen = 0, [], set()
            else:
                seen.add(slot)
                slots.append(slot)
                item += 1
        G[b] = d
        for item, ki in enumerate(bucket):
            V[slots[item]] = values[ki]
            used[slots[item]] = True

    freelist = [s for s in range(v_size) if not used[s]]
    fp = 0
    for b in order:
        bucket = buckets[b]
        if len(bucket) != 1:
            continue
        slot = freelist[fp]; fp += 1
        if slot > INT16_MAX:
            raise RuntimeError("free slot exceeded int16")
        G[b] = -slot - 1
        V[slot] = values[bucket[0]]
        used[slot] = True
    return G, V


def sim_lookup(key, G, V, exactmap, exactindex, g_size, v_size, nglyphs):
    """Mirror lookup() (incl. the caller's code<=last / bounds guard) for verification."""
    d = G[fnv(0, key) % g_size]
    d = V[(-d - 1) if d < 0 else (fnv(d, key) % v_size)]
    if d < 0 or d >= nglyphs:
        return -1
    s = exactindex[d]
    return d if exactmap[s:s + len(key)] == key else -1


# ---- C array emitters --------------------------------------------------------

def _wrap(items, per_line, indent="  "):
    out = []
    for i in range(0, len(items), per_line):
        out.append(indent + ", ".join(items[i:i + per_line]))
    return ",\n".join(out)


def emit_u8(data):
    return _wrap(["0x%02x" % b for b in data], 16)


def emit_ints(vals, per_line=20):
    return _wrap([str(v) for v in vals], per_line)


def emit_glyphs(font, keep_n):
    lines = []
    for code in range(keep_n):
        g = font.glyphs[code]
        try:
            cp = "U+%04X" % ord(font.glyph_key(code).decode("utf-8")[0])
        except Exception:
            cp = "?"
        lines.append("  {%d, %d, %d, %d, %d, %d}, // %d %s"
                     % (g["bo"], g["w"], g["h"], g["xadv"], g["xoff"], g["yoff"], code, cp))
    return "\n".join(lines)


def replace_array(text, name, new_body):
    m = re.search(r"\b" + re.escape(name) + r"\s*\[\s*\]", text)
    if not m:
        raise ValueError("array %r not found for splice" % name)
    i = text.index("{", m.end())
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[:i] + "{\n" + new_body + "\n}" + text[j + 1:]
    raise ValueError("unbalanced braces for %r" % name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("header", nargs="?", default=HDR)
    ap.add_argument("-o", "--out", default=None, help="output path (default: dry run)")
    ap.add_argument("--in-place", action="store_true", help="overwrite the input header")
    ap.add_argument("--drop-min", default="0x1F000", help="first codepoint to drop (default emoji)")
    ap.add_argument("--drop-max", default="0x1FAFF", help="last codepoint to drop (inclusive)")
    args = ap.parse_args()

    dmin, dmax = int(args.drop_min, 0), int(args.drop_max, 0)
    font = cubic_parse.parse(args.header)
    with open(args.header, "r", encoding="utf-8") as fh:
        text = fh.read()
    g_size = int(re.search(r"#define\s+G_size\s+(\d+)", text).group(1))
    v_size = int(re.search(r"#define\s+V_size\s+(\d+)", text).group(1))

    def first_cp(code):
        try:
            return ord(font.glyph_key(code).decode("utf-8")[0])
        except Exception:
            return -1

    drop = {c for c in range(font.nglyphs) if dmin <= first_cp(c) <= dmax}
    keep = [c for c in range(font.nglyphs) if c not in drop]
    K = len(keep)

    if not drop:
        print("Nothing to drop in range U+%X..U+%X." % (dmin, dmax))
        return
    # SAFETY: kept glyphs must be exactly indices 0..K-1 (drop is a top suffix),
    # otherwise renumbering would corrupt bopomofoTable's stored indices.
    if keep != list(range(K)):
        bad = [c for c in drop if c < max(keep)]
        sys.exit("REFUSING: drop set is not a top suffix (e.g. index %d sits below kept "
                 "glyphs). That would renumber glyphs and break bopomofoTable. Use a "
                 "range that only covers the highest glyph indices." % bad[0])

    old_bytes = len(font.bitmaps)
    kept_bytes = sum(font.glyph_len(font.glyphs[c]) for c in keep)
    new_bitmaps = font.bitmaps[:kept_bytes]          # kept glyphs are a byte-aligned prefix

    new_keys = [font.glyph_key(c) for c in keep]
    new_exactmap = b"".join(new_keys)
    new_exactindex, off = [], 0
    for k in new_keys:
        new_exactindex.append(off)
        off += len(k)

    G, V = gen_mph(new_keys, list(range(K)), g_size, v_size)

    # verify BEFORE writing: kept keys resolve to their index, dropped keys -> -1
    for idx, k in enumerate(new_keys):
        got = sim_lookup(k, G, V, new_exactmap, new_exactindex, g_size, v_size, K)
        if got != idx:
            sys.exit("MPH self-check FAILED: kept key #%d resolved to %d" % (idx, got))
    for c in drop:
        k = font.glyph_key(c)
        got = sim_lookup(k, G, V, new_exactmap, new_exactindex, g_size, v_size, K)
        if got != -1:
            sys.exit("MPH self-check FAILED: dropped key U+%X resolved to %d" % (first_cp(c), got))

    # overflow banks for the smaller blob (thresholds below K stay; sentinel = K)
    thresholds = [t for t in font.overflow[:-1] if t < K]
    new_overflow = thresholds + [K]

    # ---- splice ----
    out = text
    out = replace_array(out, "cubicBitmaps", emit_u8(new_bitmaps))
    out = replace_array(out, "cubicGlyphs", emit_glyphs(font, K))
    out = replace_array(out, "exactMap", emit_u8(new_exactmap))
    out = replace_array(out, "exactIndex", emit_ints(new_exactindex))
    out = replace_array(out, "G", emit_ints(G))
    out = replace_array(out, "V", emit_ints(V))
    out = replace_array(out, "overflowTable", "  " + ", ".join(str(v) for v in new_overflow))
    out = re.sub(r"(#define\s+OVERFLOW_TABLE_SIZE\s+)\d+", r"\g<1>%d" % len(new_overflow), out)
    # GFXfont: keep first & yAdvance, set last = K-1
    out = re.sub(
        r"(cubicFont\b[^=]*=\s*)\{[^}]*\}",
        lambda m: m.group(1) + "{\n  (uint8_t  *)cubicBitmaps,\n  (GFXglyph *)cubicGlyphs,\n  0x%X, 0x%X, %d }" % (font.first, K - 1, font.yadv),
        out, count=1)

    # savings report
    print("=" * 64)
    print("Trim cubicFont.h  --  drop U+%X..U+%X" % (dmin, dmax))
    print("=" * 64)
    print("glyphs        : %d -> %d   (dropped %d)" % (font.nglyphs, K, len(drop)))
    print("bitmap blob   : %d -> %d B   (-%d B)" % (old_bytes, kept_bytes, old_bytes - kept_bytes))
    print("glyph table   : -%d B  (%d glyphs x 7)" % (len(drop) * 7, len(drop)))
    print("exactMap/Index: %d -> %d B  /  %d -> %d entries"
          % (len(font.exactmap), len(new_exactmap), font.nglyphs, K))
    print("overflowTable : %s  (OVERFLOW_TABLE_SIZE=%d)" % (new_overflow, len(new_overflow)))
    print("MPH self-check: PASS (all %d kept keys resolve; all %d dropped keys -> -1)"
          % (K, len(drop)))

    if not args.out and not args.in_place:
        print("\n(dry run -- pass -o OUT.h or --in-place to write)")
        return

    dest = args.header if args.in_place else args.out
    with open(dest, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(out)
    print("\nwrote %s (%d bytes)" % (os.path.normpath(dest), len(out)))

    # end-to-end: re-parse the emitted file and re-verify lookups on it
    rf = cubic_parse.parse(dest)
    assert rf.nglyphs == K, "re-parsed glyph count mismatch"
    assert len(rf.bitmaps) == kept_bytes, "re-parsed blob size mismatch"
    rg = int(re.search(r"#define\s+G_size\s+(\d+)", out).group(1))
    rv = int(re.search(r"#define\s+V_size\s+(\d+)", out).group(1))
    rG = cubic_parse._ints(cubic_parse._array_body(cubic_parse._strip_comments(out), "G"))
    rV = cubic_parse._ints(cubic_parse._array_body(cubic_parse._strip_comments(out), "V"))
    bad = 0
    for idx in range(K):
        if sim_lookup(rf.glyph_key(idx), rG, rV, rf.exactmap, rf.exactindex, rg, rv, K) != idx:
            bad += 1
    print("re-parse verify: %s (%d/%d kept keys resolve on the emitted file)"
          % ("PASS" if bad == 0 else "FAIL", K - bad, K))


if __name__ == "__main__":
    main()
