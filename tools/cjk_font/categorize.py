"""Quick glyph inventory by Unicode category + flash bytes per category.
Also reports how many glyphs are reachable from the Bopomofo IME table."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cubic_parse

DEFAULT_HDR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "src", "graphics", "niche", "Fonts", "cubicFont.h")

def cat(cp):
    if cp < 0x80: return "ASCII"
    if cp < 0x400: return "Latin-1/ext"
    if 0x2E80 <= cp <= 0x2EFF or 0x2F00 <= cp <= 0x2FDF: return "CJK radicals"
    if 0x3000 <= cp <= 0x303F: return "CJK punctuation"
    if 0x3100 <= cp <= 0x312F or 0x31A0 <= cp <= 0x31BF: return "Bopomofo"
    if 0x3040 <= cp <= 0x30FF: return "Kana (JP)"
    if 0x3400 <= cp <= 0x4DBF: return "CJK ext-A"
    if 0x4E00 <= cp <= 0x9FFF: return "CJK unified"
    if 0xF900 <= cp <= 0xFAFF: return "CJK compat"
    if 0xFF00 <= cp <= 0xFFEF: return "Fullwidth forms"
    if 0x20000 <= cp <= 0x2FFFF: return "CJK ext-B+"
    if 0x1F000 <= cp <= 0x1FAFF: return "Emoji"
    if 0x2600 <= cp <= 0x27BF or 0x2190 <= cp <= 0x21FF or 0x2B00 <= cp <= 0x2BFF: return "Symbols/arrows"
    return "Other"

def first_cp(key):
    try:
        return ord(key.decode("utf-8")[0]) if key else -1
    except Exception:
        return -1

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_HDR
    f = cubic_parse.parse(path)
    from collections import defaultdict
    cnt = defaultdict(int); byts = defaultdict(int); multi = 0
    for code in range(f.nglyphs):
        key = f.glyph_key(code)
        # multi-codepoint keys (emoji ZWJ / flags): count separately
        try:
            s = key.decode("utf-8")
        except Exception:
            s = ""
        if len(s) > 1:
            multi += 1
        c = cat(first_cp(key))
        cnt[c] += 1
        byts[c] += len(f.glyph_bytes(code))
    total_b = sum(byts.values())
    print("category          glyphs     bitmap-bytes   %ofblob")
    print("-" * 56)
    for c in sorted(cnt, key=lambda k: -byts[k]):
        print("%-16s  %6d   %9d B     %5.1f%%" % (c, cnt[c], byts[c], 100.0*byts[c]/total_b))
    print("-" * 56)
    print("%-16s  %6d   %9d B" % ("TOTAL", f.nglyphs, total_b))
    print("multi-codepoint keys (emoji seq/flags): %d" % multi)

if __name__ == "__main__":
    main()
