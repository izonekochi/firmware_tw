#!/usr/bin/env bash
#
# Regenerate src/graphics/niche/Fonts/cubicFont.h from the origin generator
# (../MeshGFXFont/fontconvert.cpp). Produces a header that is DROP-IN compatible
# with the current InkHUD renderer (same G/V/exactMap/lookup/cubicBitmaps/
# cubicGlyphs/overflowTable/cubicFont/bopomofoTable layout — verified against
# fontconvert.cpp emit order).
#
# Run on Linux / WSL / macOS with a host C++17 compiler and FreeType dev headers.
# On Windows this does NOT run in Git Bash (no g++/FreeType) — use WSL:
#     wsl --install         # once, then reopen a WSL shell
#     sudo apt-get install -y build-essential libfreetype6-dev pkg-config
#     cd /mnt/c/Users/kochi/source/repos/firmware_tw
#     bash tools/cjk_font/regen.sh
#
# After it writes the header, VALIDATE it back on Windows with the Python tools
# (they run fine there):  python tools/cjk_font/categorize.py
#
set -euo pipefail

# Resolve paths relative to this script (tools/cjk_font/regen.sh -> repo root).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
GEN_DIR="${MESHGFXFONT_DIR:-$REPO_ROOT/../MeshGFXFont}"
DEST="$REPO_ROOT/src/graphics/niche/Fonts/cubicFont.h"

echo "generator dir : $GEN_DIR"
echo "dest header   : $DEST"

[ -f "$GEN_DIR/fontconvert.cpp" ] || { echo "ERROR: $GEN_DIR/fontconvert.cpp not found. Set MESHGFXFONT_DIR."; exit 1; }
command -v g++ >/dev/null || { echo "ERROR: g++ not found (need a host C++17 compiler)."; exit 1; }
command -v pkg-config >/dev/null && FT_FLAGS="$(pkg-config --cflags --libs freetype2)" || FT_FLAGS="-I/usr/include/freetype2 -lfreetype"

cd "$GEN_DIR"

# Inputs (must exist alongside fontconvert.cpp — they do in the repo).
FONT_TTF="Cubic_11.ttf"
EMOJI1_TTF="NotoEmoji-Regular.ttf"
EMOJI2_TTF="AppleColorEmoji.ttf"
SIZE="11"
DICT="dictionary.txt"
BOPOMOFO="bopomofo.txt"
for f in "$FONT_TTF" "$EMOJI1_TTF" "$EMOJI2_TTF" "$DICT" "$BOPOMOFO"; do
    [ -f "$f" ] || { echo "ERROR: missing input $GEN_DIR/$f"; exit 1; }
done

echo "== building fontconvert =="
g++ -std=c++17 -Wall -O2 fontconvert.cpp $FT_FLAGS -o fontconvert

echo "== running fontconvert (writes ./cubicFont.h) =="
./fontconvert "$FONT_TTF" "$EMOJI1_TTF" "$EMOJI2_TTF" "$SIZE" "$DICT" "$BOPOMOFO"

[ -f cubicFont.h ] || { echo "ERROR: fontconvert did not produce cubicFont.h"; exit 1; }

echo "== installing to firmware =="
cp cubicFont.h "$DEST"
GLYPHS="$(grep -c '},.*// 0x' "$DEST" || true)"
echo "wrote $DEST"
echo "  size    : $(wc -c < "$DEST") bytes"
echo "  glyphs~ : $GLYPHS  (last field in GFXfont def is glyph count - 1)"
echo
echo "NEXT (on Windows):"
echo "  1) validate:   python tools/cjk_font/categorize.py"
echo "  2) (optional) drop the ~inline emoji again: python tools/cjk_font/trim_font.py --in-place"
echo "     (keeps the 31 control-byte reaction emoji at indices 0..31)"
echo "  3) build:       pio run -e heltec-mesh-pocket-5000-inkhud-mod"
