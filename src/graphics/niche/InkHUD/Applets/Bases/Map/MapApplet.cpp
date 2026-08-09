#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./MapApplet.h"
#include "./MapTile.h"

#include <math.h>
#include <string.h>

// The two tile transports are mutually exclusive: one build cannot pull tiles from BOTH the companion keyboard
// (over Serial2) and the local filesystem. They share the format/logic layer below but differ in every byte-fetch.
#if defined(MOD_LOCAL_MAP_TILES) && defined(MOD_KEYBOARD_MAP_TILES)
#error "MOD_LOCAL_MAP_TILES and MOD_KEYBOARD_MAP_TILES are mutually exclusive tile sources -- define at most one"
#endif

#if defined(MOD_KEYBOARD_MAP_TILES) || defined(MOD_LOCAL_MAP_TILES)
#include "DebugConfiguration.h"
#include "main.h" // nrf52Loop() (keyboard build) / esp32Loop() (local build) -- the per-source watchdog feed
#include <stdio.h>
#endif

#ifdef MOD_LOCAL_MAP_TILES
#include "FSCommon.h" // FSCom == LittleFS (internal fallback tile source)
#include "SPILock.h"  // spiLock -- microSD shares the FSPI bus with LoRa + the e-ink panel
#include <FS.h>
#ifdef HAS_SDCARD
#include <SD.h> // the SD global (primary tile source), mounted at boot by FSCommon.cpp setupSDCard()
#endif
#endif

using namespace NicheGraphics;

bool InkHUD::MapApplet::s_zoomLocked = false;
int InkHUD::MapApplet::s_lockedZoom = -1;
int InkHUD::MapApplet::s_lastRenderedZoom = -1;
int InkHUD::MapApplet::s_autoFitZoom = -1;

static bool usesGridTileLayout();
static int gridTilesPerBlock();
static int tileZoomAt(int tileIndex);
static int tileTxAt(int tileIndex);
static int tileTyAt(int tileIndex);
static int tileMetadataZoomCount();
static int tileMetadataZoomAt(int index);

// Observe GPS position updates so the map redraws whenever a new location arrives.
InkHUD::MapApplet::MapApplet()
{
    if (gpsStatus)
        gpsStatusObserver.observe(&gpsStatus->onNewStatus);
}

int InkHUD::MapApplet::onGpsStatusUpdate(const meshtastic::Status *status)
{
    if (status->getStatusType() != STATUS_TYPE_GPS)
        return 0;
    if (!isActive() || !gpsStatus->getHasLock())
        return 0;

    requestUpdate();
    return 0;
}

// Zoom in one step from the current display zoom.
void InkHUD::MapApplet::zoomIn()
{
    int baseZoom = s_zoomLocked ? s_lockedZoom : s_lastRenderedZoom;
    if (baseZoom < 0)
        return;

    if (map_tile_count == 0) {
        if (baseZoom < ZOOM_MAX_NO_TILES) {
            s_lockedZoom = baseZoom + 1;
            s_zoomLocked = true;
        }
        return;
    }

    // Jump to the next tile zoom strictly above current, not just +1
    int next = -1;
    for (int i = 0; i < tileMetadataZoomCount(); i++) {
        int z = tileMetadataZoomAt(i);
        if (z > baseZoom && (next < 0 || z < next))
            next = z;
    }
    if (next < 0)
        return;

    s_lockedZoom = next;
    s_zoomLocked = true;
}

void InkHUD::MapApplet::resetZoom()
{
    s_zoomLocked = false;
    s_lockedZoom = -1;
}

bool InkHUD::MapApplet::canZoomIn() const
{
    if (s_lastRenderedZoom < 0)
        return false;
    int ref = s_zoomLocked ? s_lockedZoom : s_lastRenderedZoom;
    if (map_tile_count == 0)
        return ref < ZOOM_MAX_NO_TILES;
    for (int i = 0; i < tileMetadataZoomCount(); i++) {
        if (tileMetadataZoomAt(i) > ref)
            return true;
    }
    return false;
}

void InkHUD::MapApplet::zoomOut()
{
    int baseZoom = s_zoomLocked ? s_lockedZoom : s_lastRenderedZoom;
    if (baseZoom < 0) {
        s_zoomLocked = false;
        s_lockedZoom = -1;
        return;
    }

    if (map_tile_count == 0) {
        int floor = (s_autoFitZoom >= 0) ? s_autoFitZoom : baseZoom;
        if (baseZoom > floor) {
            s_lockedZoom = baseZoom - 1;
            s_zoomLocked = true;
        } else {
            s_zoomLocked = false;
            s_lockedZoom = -1;
        }
        return;
    }

    // Jump to the next tile zoom strictly below current, not just -1
    int next = -1;
    for (int i = 0; i < tileMetadataZoomCount(); i++) {
        int z = tileMetadataZoomAt(i);
        if (z < baseZoom && (next < 0 || z > next))
            next = z;
    }
    if (next < 0) {
        // Already at the widest available zoom: STAY here. Unlocking (the old behaviour) dropped back to
        // auto-fit, whose zoom depends on the node spread -- with nearby nodes that is a sudden zoom IN,
        // which reads as the map "jumping" when the user tries to zoom out past the range.
        return;
    }

    s_lockedZoom = next;
    s_zoomLocked = true;
}

bool InkHUD::MapApplet::canZoomOut() const
{
    if (s_lastRenderedZoom < 0)
        return false;
    int ref = s_zoomLocked ? s_lockedZoom : s_lastRenderedZoom;
    if (map_tile_count == 0)
        return s_autoFitZoom >= 0 ? ref > s_autoFitZoom : false;
    for (int i = 0; i < tileMetadataZoomCount(); i++) {
        if (tileMetadataZoomAt(i) < ref)
            return true;
    }
    return false;
}

// Raw LZ4 block decompressor. Returns bytes written, or -1 on error.
static int lz4_decompress(const uint8_t *src, int src_len, uint8_t *dst, int dst_cap)
{
    const uint8_t *s = src;
    const uint8_t *s_end = src + src_len;
    uint8_t *d = dst;
    const uint8_t *d_end = dst + dst_cap;
    while (s < s_end) {
        uint8_t token = *s++;
        int lit_len = (token >> 4) & 0xF;
        if (lit_len == 15) {
            uint8_t x;
            do {
                if (s >= s_end)
                    return -1; // truncated length-extension token: bail before reading past src (untrusted UART)
                x = *s++;
                lit_len += x;
            } while (x == 255);
        }
        if (d + lit_len > d_end || s + lit_len > s_end)
            return -1;
        memcpy(d, s, lit_len);
        d += lit_len;
        s += lit_len;
        if (s >= s_end)
            break;
        if (s + 2 > s_end)
            return -1;
        int offset = (int)s[0] | ((int)s[1] << 8);
        s += 2;
        if (offset == 0 || d - offset < dst)
            return -1;
        int mat_len = (token & 0xF) + 4;
        if (mat_len == 4 + 15) {
            uint8_t x;
            do {
                if (s >= s_end)
                    return -1; // truncated length-extension token: bail before reading past src (untrusted UART)
                x = *s++;
                mat_len += x;
            } while (x == 255);
        }
        if (d + mat_len > d_end)
            return -1;
        const uint8_t *m = d - offset;
        for (int i = 0; i < mat_len; i++)
            *d++ = m[i];
    }
    return (int)(d - dst);
}

// Tiles are 1 bit/pixel, column-major: [bx=0..31][y=0..255], 8 pixels per byte.
static uint8_t s_tileCacheBuffer[8192];
static constexpr uint8_t MAP_TILE_LAYOUT_SPARSE = 0;
static constexpr uint8_t MAP_TILE_LAYOUT_GRID = 1;
static constexpr uint8_t MAP_TILE_KIND_LZ4 = 0;
static constexpr uint8_t MAP_TILE_KIND_WHITE = 1;
static constexpr uint8_t MAP_TILE_KIND_BLACK = 2;

#if defined(MOD_KEYBOARD_MAP_TILES) || defined(MOD_LOCAL_MAP_TILES)
// Streamed zoom set + coverage (from /map/manifest). File-scope so tileMetadataZoomCount/At() below can read it
// -> the zoom picker (onRender / zoomIn / zoomOut / canZoom*) selects among the manifest's real zooms instead of
// the compiled MapTile.h metadata. Filled identically by the keyboard (UART) and local-FS manifest readers.
struct KbZoom {
    int z, minTx, maxTx, minTy, maxTy;
};
static KbZoom s_kbZooms[12]; // 12 slots: a whole-Taiwan SD bundle ships z9..z16 plus an urban z17 tier
static int s_kbZoomCount = 0;

// Parse one manifest coverage line "z minTx maxTx minTy maxTy" and merge it into s_kbZooms (union per zoom).
// Source-agnostic: both transports read the same text manifest. Returns true if the line parsed as a record.
static bool parseManifestLine(const char *line)
{
    int z, a, b, c, d;
    if (sscanf(line, "%d %d %d %d %d", &z, &a, &b, &c, &d) != 5)
        return false;
    int idx = -1;
    for (int i = 0; i < s_kbZoomCount; i++)
        if (s_kbZooms[i].z == z) {
            idx = i;
            break;
        }
    if (idx < 0) {
        if (s_kbZoomCount >= (int)(sizeof(s_kbZooms) / sizeof(s_kbZooms[0])))
            return false;
        idx = s_kbZoomCount++;
        s_kbZooms[idx] = {z, a, b, c, d}; // {z, minTx, maxTx, minTy, maxTy}
    } else {
        s_kbZooms[idx].minTx = min(s_kbZooms[idx].minTx, a);
        s_kbZooms[idx].maxTx = max(s_kbZooms[idx].maxTx, b);
        s_kbZooms[idx].minTy = min(s_kbZooms[idx].minTy, c);
        s_kbZooms[idx].maxTy = max(s_kbZooms[idx].maxTy, d);
    }
    return true;
}
#endif

static bool usesGridTileLayout()
{
    return map_tile_layout == MAP_TILE_LAYOUT_GRID && map_tile_grid_cols > 0 && map_tile_grid_rows > 0 &&
           map_tile_block_count > 0;
}

static int gridTilesPerBlock()
{
    return (int)map_tile_grid_cols * (int)map_tile_grid_rows;
}

static int tileZoomAt(int tileIndex)
{
    if (!usesGridTileLayout())
        return map_tile_zooms[tileIndex];
    int tilesPerBlock = gridTilesPerBlock();
    int blockIndex = tilesPerBlock > 0 ? (tileIndex / tilesPerBlock) : 0;
    return map_tile_block_zooms[blockIndex];
}

static int tileTxAt(int tileIndex)
{
    if (!usesGridTileLayout())
        return map_tile_tx[tileIndex];
    int rows = map_tile_grid_rows;
    int tilesPerBlock = gridTilesPerBlock();
    int blockIndex = tilesPerBlock > 0 ? (tileIndex / tilesPerBlock) : 0;
    int localIndex = tilesPerBlock > 0 ? (tileIndex % tilesPerBlock) : 0;
    return map_tile_block_tx[blockIndex] + (rows > 0 ? (localIndex / rows) : 0);
}

static int tileTyAt(int tileIndex)
{
    if (!usesGridTileLayout())
        return map_tile_ty[tileIndex];
    int rows = map_tile_grid_rows;
    int tilesPerBlock = gridTilesPerBlock();
    int blockIndex = tilesPerBlock > 0 ? (tileIndex / tilesPerBlock) : 0;
    int localIndex = tilesPerBlock > 0 ? (tileIndex % tilesPerBlock) : 0;
    return map_tile_block_ty[blockIndex] + (rows > 0 ? (localIndex % rows) : 0);
}

static int tileMetadataZoomCount()
{
#if defined(MOD_KEYBOARD_MAP_TILES) || defined(MOD_LOCAL_MAP_TILES)
    return s_kbZoomCount; // streamed tile source: drive the zoom picker off the manifest zooms, not MapTile.h
#else
    if (usesGridTileLayout())
        return map_tile_block_count;
    return map_tile_count;
#endif
}

static int tileMetadataZoomAt(int index)
{
#if defined(MOD_KEYBOARD_MAP_TILES) || defined(MOD_LOCAL_MAP_TILES)
    return s_kbZooms[index].z;
#else
    return usesGridTileLayout() ? map_tile_block_zooms[index] : map_tile_zooms[index];
#endif
}

static const uint8_t *decodeSparseTile(int tileIndex)
{
    const uint8_t kind = map_tile_kinds[tileIndex];
    if (kind == MAP_TILE_KIND_WHITE) {
        memset(s_tileCacheBuffer, 0x00, sizeof(s_tileCacheBuffer));
        return s_tileCacheBuffer;
    }
    if (kind == MAP_TILE_KIND_BLACK) {
        memset(s_tileCacheBuffer, 0xFF, sizeof(s_tileCacheBuffer));
        return s_tileCacheBuffer;
    }
    const uint8_t *compressed = map_tile_data + map_tile_offsets[tileIndex];
    int n = lz4_decompress(compressed, map_tile_sizes[tileIndex], s_tileCacheBuffer, sizeof(s_tileCacheBuffer));
    return n == sizeof(s_tileCacheBuffer) ? s_tileCacheBuffer : nullptr;
}

// Draw tiles centered on latCenter/lngCenter. Falls back to the nearest available zoom if
// no tiles exist at exactly zoom (upsamples), enabling smooth zoom steps.
void InkHUD::MapApplet::drawMapTileBackground(int zoom)
{
#if defined(MOD_KEYBOARD_MAP_TILES)
    drawMapTileBackgroundFromKeyboard(zoom); // tiles come from the keyboard's LittleFS, not compiled-in MapTile.h
    return;
#elif defined(MOD_LOCAL_MAP_TILES)
    drawMapTileBackgroundLocal(zoom); // tiles come from the local microSD / LittleFS bundle, not MapTile.h
    return;
#endif
    if (map_tile_count == 0 || metersToPx <= 0.0f)
        return;

    const float R = 6378137.0f;
    const float latRad = latCenter * DEG_TO_RAD;
    const float mpp = (2.0f * M_PI * R / (256.0f * (float)(1 << zoom))) * cosf(latRad);
    const float worldPxPerScreenPx = 1.0f / (metersToPx * mpp);

    // Find best tile zoom: highest available <= zoom, or lowest available if none below.
    int tileZoom = -1;
    for (int i = 0; i < tileMetadataZoomCount(); i++) {
        int z = tileMetadataZoomAt(i);
        if (z <= zoom && (tileZoom < 0 || z > tileZoom))
            tileZoom = z;
    }
    if (tileZoom < 0) {
        for (int i = 0; i < tileMetadataZoomCount(); i++) {
            int z = tileMetadataZoomAt(i);
            if (tileZoom < 0 || z < tileZoom)
                tileZoom = z;
        }
    }
    if (tileZoom < 0)
        return;

    // Convert screen-pixel movement into tileZoom coordinate space.
    // When tileZoom < zoom, tile pixels are upsampled (each tile pixel covers >1 screen px).
    const float tileWorldPx = worldPxPerScreenPx * ((float)(1 << tileZoom) / (float)(1 << zoom));

    const float sinLat = sinf(latRad);
    const float gpxX = ((lngCenter + 180.0f) / 360.0f) * (float)(1 << tileZoom) * 256.0f;
    const float gpxY = (0.5f - logf((1.0f + sinLat) / (1.0f - sinLat)) / (4.0f * M_PI)) * (float)(1 << tileZoom) * 256.0f;

    const float minWx = gpxX - width() * 0.5f * tileWorldPx;
    const float maxWx = gpxX + width() * 0.5f * tileWorldPx;
    const float minWy = gpxY - height() * 0.5f * tileWorldPx;
    const float maxWy = gpxY + height() * 0.5f * tileWorldPx;

    for (int i = 0; i < map_tile_count; i++) {
        if (tileZoomAt(i) != tileZoom)
            continue;

        const int tx = tileTxAt(i);
        const int ty = tileTyAt(i);
        const float tileMinWx = tx * 256.0f;
        const float tileMaxWx = tileMinWx + 256.0f;
        const float tileMinWy = ty * 256.0f;
        const float tileMaxWy = tileMinWy + 256.0f;
        if (tileMaxWx < minWx || tileMinWx > maxWx || tileMaxWy < minWy || tileMinWy > maxWy)
            continue;

        const uint8_t *tile = decodeSparseTile(i);
        if (!tile)
            continue;

        blitTile(tile, tileMinWx, tileMinWy, gpxX, gpxY, tileWorldPx);
    }
}

// Screen-map and blit one decoded tile. Extracted so the compiled-in and keyboard tile sources share it.
void InkHUD::MapApplet::blitTile(const uint8_t *tile, float tileMinWx, float tileMinWy, float gpxX, float gpxY,
                                 float tileWorldPx)
{
    // Work in TILE-RELATIVE world pixels. Building an ABSOLUTE coordinate first (wx = gpxX + ...) rounds the
    // result to float32's ULP, and at z16 gpxX is ~1.4e7 where ULP == 1.0 -- all sub-pixel precision is gone.
    // Two consequences, both fixed here:
    //   1. With an ODD applet width, width()*0.5f ends in .5, so wx lands on a half-integer, rounds to even, and
    //      px aliases to 0,0,2,2,4,4... : the tile renders at HALF horizontal resolution. (Even widths, e.g. the
    //      default 1-tile 250px layout, escape this -- but a 2-tile layout is 123px wide.)
    //   2. The old (int) cast truncated TOWARD ZERO, so a genuine px in (-1,0) -- which happens on the leftmost
    //      columns of a partially-visible tile whenever gpxX has a fractional part, i.e. at every zoom below 16 --
    //      became 0, passed the px<0 cull, and blitted column 0 over a pixel owned by the tile to its left.
    //      floorf() yields -1 and culls it correctly.
    // dx0/dy0 are computed EXACTLY: tileMinWx is an exact multiple of 256 (tx*256 <= 2^24 at z16), 256 is >= the
    // ULP of gpxX at every reachable zoom, and the visibility cull bounds |dx0|, so the difference is exactly
    // representable. Everything downstream stays small, preserving sub-pixel precision at any zoom.
    const float dx0 = tileMinWx - gpxX; // tile origin relative to the map centre, in world px (small)
    const float dy0 = tileMinWy - gpxY;
    const float halfW = width() * 0.5f;
    const float halfH = height() * 0.5f;

    const int sxStart = max(0, (int)floorf(dx0 / tileWorldPx + halfW));
    const int sxEnd = min(width() - 1, (int)ceilf((dx0 + 256.0f) / tileWorldPx + halfW) - 1);
    const int syStart = max(0, (int)floorf(dy0 / tileWorldPx + halfH));
    const int syEnd = min(height() - 1, (int)ceilf((dy0 + 256.0f) / tileWorldPx + halfH) - 1);

    for (int sy = syStart; sy <= syEnd; sy++) {
        const int py = (int)floorf((sy - halfH) * tileWorldPx - dy0);
        if (py < 0 || py > 255)
            continue;

        for (int sx = sxStart; sx <= sxEnd; sx++) {
            const int px = (int)floorf((sx - halfW) * tileWorldPx - dx0);
            if (px < 0 || px > 255)
                continue;

            if (!(tile[(px / 8) * 256 + py] & (1 << (px % 8))))
                continue;

            drawPixel(sx, sy, BLACK);
        }
    }
}

// ==== Shared tile/label machinery (source-agnostic) =====================================================
// Compiled for either streamed tile source. The per-byte fetch differs (keyboard UART vs local FS) but the
// scratch buffers, the tiled-label file format, and the label parse/binary-search are identical, so they live
// here instead of being duplicated per transport. Both manifest readers fill s_kbZooms (above).
#if defined(MOD_KEYBOARD_MAP_TILES) || defined(MOD_LOCAL_MAP_TILES)

// Scratch for one compressed tile blob (read off the wire / off the FS), decoded into s_tileCacheBuffer.
// Separate buffer: lz4_decompress must not read its source from the same buffer it writes. Max LZ4 block for
// 8192 input is ~8240 bytes; oversize blobs are rejected (drawn white) rather than overflowing.
static uint8_t s_compressedBuf[8320];

// ---- Map-label overlay (tiled, streamed per-view) ------------------------------------------------------
// Labels live in per-zoom tiled files that mirror the map tiles, so only the labels for the CURRENT view are
// fetched, keeping RAM + round-trips bounded at every zoom:
//   /map/labels_<z>.idx : sorted [tx:u16][ty:u16][offset:u32][len:u16] (10-byte records, binary-searched)
//   /map/labels_<z>.dat : per-tile [count:u16] then count x
//                           [ lat:i32(deg*1e7) | lng:i32 | rank:u8 | category:u8 | strLen:u8 | str[strLen] ]
// A label is bucketed into every native zoom it should appear at (LOD = the zoom bucket), so records carry no
// minZoom. Collected labels are rank-sorted before renderLabels() so its greedy declutter favours the important.
// The .idx is read on demand into the shared s_tileCacheBuffer (free during the label pass -- the tile pass has
// finished blitting), so this feature adds NO large static buffer of its own.
static constexpr int KB_MAX_LABELS = 128; // working set for one view; renderLabels caps DRAWN at MAX_PLACED (24)
static constexpr int LABEL_IDX_REC = 10;  // idx record: [tx:u16][ty:u16][offset:u32][len:u16]
static InkHUD::MapApplet::MapLabel s_labels[KB_MAX_LABELS];
static char s_labelStrBuf[2048]; // backs the MapLabel.text pointers (icon-only labels use none)
static int s_labelCount = 0;

// The view (tileZoom + visible tile rect) the tile-background pass computed this render; -1 = no tile draw.
static int s_kbViewZoom = -1, s_kbViewTx0 = 0, s_kbViewTx1 = -1, s_kbViewTy0 = 0, s_kbViewTy1 = -1;
// The view s_labels was last loaded for -- skip the refetch while the view is unchanged.
static int s_lblViewZoom = -2, s_lblViewTx0 = 0, s_lblViewTx1 = -1, s_lblViewTy0 = 0, s_lblViewTy1 = -1;

// Binary-search the idx (in s_tileCacheBuffer, `count` 10-byte records sorted by (tx,ty)) for tile (tx,ty).
static bool findLabelTile(int count, uint32_t tx, uint32_t ty, uint32_t &offset, uint32_t &len)
{
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const uint8_t *r = s_tileCacheBuffer + (size_t)mid * LABEL_IDX_REC;
        uint32_t rtx = r[0] | (r[1] << 8);
        uint32_t rty = r[2] | (r[3] << 8);
        if (rtx == tx && rty == ty) {
            offset = r[4] | (r[5] << 8) | (r[6] << 16) | ((uint32_t)r[7] << 24);
            len = r[8] | (r[9] << 8);
            return true;
        }
        if (rtx < tx || (rtx == tx && rty < ty))
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return false;
}

// Parse one tile blob ([count:u16] then records) into s_labels, appending until KB_MAX_LABELS / buffer full.
static void parseLabelBlob(const uint8_t *b, uint32_t len, int &strPos)
{
    if (len < 2)
        return;
    uint16_t count = (uint16_t)(b[0] | (b[1] << 8));
    uint32_t p = 2;
    for (int i = 0; i < count && s_labelCount < KB_MAX_LABELS; i++) {
        if (p + 11 > len)
            break; // record header = lat(4)+lng(4)+rank(1)+category(1)+strLen(1)
        int32_t lat = (int32_t)(b[p] | (b[p + 1] << 8) | (b[p + 2] << 16) | ((uint32_t)b[p + 3] << 24));
        int32_t lng = (int32_t)(b[p + 4] | (b[p + 5] << 8) | (b[p + 6] << 16) | ((uint32_t)b[p + 7] << 24));
        uint8_t rank = b[p + 8], category = b[p + 9], strLen = b[p + 10];
        p += 11;
        if (p + strLen > len || strPos + strLen + 1 > (int)sizeof(s_labelStrBuf))
            break;
        memcpy(s_labelStrBuf + strPos, b + p, strLen);
        s_labelStrBuf[strPos + strLen] = 0;
        s_labels[s_labelCount].lat = lat * 1e-7f;
        s_labels[s_labelCount].lng = lng * 1e-7f;
        s_labels[s_labelCount].minZoom = 0; // tiled mode: LOD is the zoom bucket -> always shown at this zoom
        s_labels[s_labelCount].rank = rank;
        s_labels[s_labelCount].category = category;
        s_labels[s_labelCount].text = s_labelStrBuf + strPos;
        s_labelCount++;
        strPos += strLen + 1;
        p += strLen;
    }
}

#endif // shared tile/label machinery

#ifdef MOD_KEYBOARD_MAP_TILES

// Keyboard command bytes (must match MeshPocketKB/src/main.cpp).
static constexpr uint8_t KB_READ_FILE_CMD = 0x0A;
static constexpr uint8_t KB_READ_RANGE_CMD = 0x0C; // read <length> bytes at <offset> of a file (tiled labels)
static constexpr uint8_t KB_GET_TILE_CMD = 0x0D;
static constexpr uint8_t KB_NOOP_CMD = 0x55; // reserved no-op; doubles as the UART wake preamble byte

// ---- Keyboard wake ----------------------------------------------------------------------------------
// The keyboard light-sleeps after ~5s idle (30s after any file command). While asleep its UART is NOT clocked,
// so a plain GET_TILE is lost outright -- and we have no other wire to it (the nRF52 can't reach the key matrix,
// which is why only a keypress used to revive the map). Its firmware therefore arms an edge-triggered UART wake;
// but by design the bytes that TRIGGER that wake are consumed/garbled, so the request can never be the trigger.
// Hence: spend a throwaway KB_NOOP_CMD preamble, wait for the keyboard to wake and run its post-wake RX drain,
// and only then send the real command. 0x55 = 0b01010101 toggles every bit, giving ~5 RX rising edges per byte
// (the wake threshold minimum is 3); 0x00 would give just one. KB_NOOP_CMD is a reserved no-op on the keyboard,
// so even a preamble byte that outlives the drain and reaches the dispatcher does nothing.
static constexpr uint32_t KB_WAKE_SETTLE_MS = 150;    // keyboard: light-sleep exit + delay(50) + pin restore +
                                                      // its 30ms post-wake RX drain. ~90ms typical; 150 for margin
static constexpr uint32_t KB_AWAKE_ASSUME_MS = 25000; // < the keyboard's 30s file-command keep-alive

static uint32_t s_kbLastOkMs = 0; // millis() of the last reply of any kind; 0 = never / assumed asleep

// Cheap: while the keyboard is answering, its own keep-alive holds it awake, so skip the preamble entirely.
static bool kbMayBeAsleep()
{
    return s_kbLastOkMs == 0 || (millis() - s_kbLastOkMs) > KB_AWAKE_ASSUME_MS;
}

static void kbSendWakePreamble()
{
    static const uint8_t preamble[4] = {KB_NOOP_CMD, KB_NOOP_CMD, KB_NOOP_CMD, KB_NOOP_CMD};
    Serial2.write(preamble, sizeof(preamble));
    Serial2.flush(); // don't start the settle timer until the bytes are actually on the wire
    delay(KB_WAKE_SETTLE_MS);
    while (Serial2.available())
        Serial2.read(); // discard anything the sleep/wake UART transition pushed back at us
    LOG_DEBUG("InkHUD Map: keyboard wake preamble sent");
}

// ---- Tile diagnostics -------------------------------------------------------------------------------
// GET_TILE carries NO integrity check (unlike WRITE_CHUNK's CRC), so a single byte corrupted on the shared
// UART can still LZ4-decode to exactly 8192 bytes -- passing the length check and blitting garbage (LZ4
// match-copies smear one bad byte over a large region). Logging a CRC-32 of the DECODED tile lets us compare
// against the value computed offline from the packed map: match => the bytes arrived intact (any ugliness is
// the source tile / dithering); mismatch => UART corruption. Set to 0 to silence the per-tile logging.
#ifndef MOD_MAP_TILE_DEBUG
#define MOD_MAP_TILE_DEBUG 1
#endif

#if MOD_MAP_TILE_DEBUG
// CRC-32/IEEE (reflected, poly 0xEDB88320) -- byte-identical to Python's zlib.crc32.
static uint32_t kbDbgCrc32(const uint8_t *d, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= d[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// Count set bits = BLACK pixels, directly comparable to the offline tile analysis.
static uint32_t kbDbgBlackPixels(const uint8_t *d, size_t len)
{
    uint32_t n = 0;
    for (size_t i = 0; i < len; i++)
        n += (uint32_t)__builtin_popcount(d[i]);
    return n;
}
#endif // MOD_MAP_TILE_DEBUG

// Flush Serial2 to a frame boundary after a desync. The keyboard also emits unsolicited 2-byte key events on
// this same line (modCode has bit7 set), so one can land inside a tile/manifest reply and corrupt its size
// header; when that leaves the real reply still streaming, a one-shot drain can't clear it. This reads until
// the line has been idle for idleMs, bounded by maxMs, so at most the current tile is lost, not the whole render.
static void drainSerial2ToIdle(uint32_t idleMs, uint32_t maxMs)
{
    // Compare elapsed DIFFERENCES, never millis() against a precomputed deadline: the latter wraps wrong in the
    // ~1.5s window before the 49.7-day rollover, making the drain a no-op that leaves a streaming reply in the ring.
    const uint32_t start = millis();
    uint32_t lastByte = millis();
    while (millis() - start < maxMs) {
        nrf52Loop(); // watchdog: fed sub-millisecond so a slow/stuck line can never reboot us mid-render
        if (Serial2.available()) {
            Serial2.read();
            lastByte = millis();
        } else if (millis() - lastByte >= idleMs) {
            break;
        } else {
            delay(1);
        }
    }
}

// Read exactly n bytes from Serial2 into dst within an overall deadline. Unlike Stream::readBytes (which resets
// its timeout every byte and so has NO total deadline -- a stuck key auto-repeating at ~10 B/s on this shared wire
// keeps it alive until `size` bytes arrive, blocking the render past the 90s watchdog), this bounds total wall time
// and feeds the watchdog. Returns true only on a complete read. overallMs covers the keyboard's FS-lookup latency
// (idx binary search + flash seeks, up to a few hundred ms) plus wire time (~90us/byte at 921600 baud).
static bool kbReadExact(uint8_t *dst, uint32_t n, uint32_t overallMs)
{
    const uint32_t start = millis();
    uint32_t got = 0;
    while (got < n) {
        nrf52Loop();
        if (millis() - start > overallMs)
            return false; // truncated/trickled reply: caller drains to a frame boundary
        if (Serial2.available())
            dst[got++] = (uint8_t)Serial2.read();
        else
            delay(1);
    }
    return true;
}

// Read /map/manifest (READ_FILE 0x0A) to learn which zooms exist and their coverage. Runs once, retrying each
// render until it succeeds. Doubles as the firmware-side link check: a success log means the Serial2 wiring +
// keyboard filesystem are good.
void InkHUD::MapApplet::readKeyboardManifestIfNeeded()
{
    if (s_kbZoomCount > 0)
        return; // already have the zoom set -> never re-read (the only "done" state; do NOT latch on a bad/
                // corrupted header, or a stray key byte leading one reply would blank the map until reboot)
    if (kbManifestLastAttempt != 0 && (millis() - kbManifestLastAttempt) < 5000)
        return; // rate-limit retries so a missing/unpowered keyboard doesn't stall every render (self-heals)
    kbManifestLastAttempt = millis();

    if (kbMayBeAsleep()) // first contact after boot always lands here
        kbSendWakePreamble();

    while (Serial2.available()) // clear any stale key byte
        Serial2.read();

    const char *path = "/map/manifest";
    uint32_t pathSize = strlen(path);
    Serial2.write(KB_READ_FILE_CMD);
    Serial2.write((uint8_t *)&pathSize, sizeof(uint32_t));
    Serial2.write((const uint8_t *)path, pathSize);

    uint32_t fileSize = 0;
    if (!kbReadExact((uint8_t *)&fileSize, sizeof(uint32_t), 800)) {
        LOG_WARN("InkHUD Map: keyboard manifest read timed out (Serial2 wiring? keyboard powered?)");
        s_kbLastOkMs = 0; // still unreachable -> keep waking on every retry
        drainSerial2ToIdle(15, 300);
        return; // no response -> stay unchecked, retry (rate-limited) on a later render
    }
    s_kbLastOkMs = millis(); // it answered, so it's awake -- whatever the payload turns out to be
    static char s_manifestBuf[2048];
    if (fileSize == 0 || fileSize >= sizeof(s_manifestBuf)) {
        // fileSize 0 = keyboard's open-fail sentinel (manifest missing); a huge value = a desync (e.g. a stray
        // key byte led the reply). Either way don't parse; leave s_kbZoomCount==0 so it retries (rate-limited).
        LOG_WARN("InkHUD Map: keyboard manifest bad/absent size %u -- will retry", (unsigned)fileSize);
        drainSerial2ToIdle(15, 1500);
        return;
    }
    char *buf = s_manifestBuf;
    if (!kbReadExact((uint8_t *)buf, fileSize, 1500)) {
        LOG_WARN("InkHUD Map: keyboard manifest short read");
        drainSerial2ToIdle(15, 1500);
        return;
    }
    buf[fileSize] = 0;

    // Line 1 = version ("v3 0 0"); each remaining line = "z minTx maxTx minTy maxTy". Merge per zoom.
    s_kbZoomCount = 0;
    strtok(buf, "\r\n"); // discard the version line
    for (char *line = strtok(nullptr, "\r\n"); line; line = strtok(nullptr, "\r\n")) {
        int z, a, b, c, d;
        if (sscanf(line, "%d %d %d %d %d", &z, &a, &b, &c, &d) != 5)
            continue;
        int idx = -1;
        for (int i = 0; i < s_kbZoomCount; i++)
            if (s_kbZooms[i].z == z) {
                idx = i;
                break;
            }
        if (idx < 0) {
            if (s_kbZoomCount >= (int)(sizeof(s_kbZooms) / sizeof(s_kbZooms[0])))
                continue;
            idx = s_kbZoomCount++;
            s_kbZooms[idx] = {z, a, b, c, d}; // {z, minTx, maxTx, minTy, maxTy}
        } else {
            s_kbZooms[idx].minTx = min(s_kbZooms[idx].minTx, a);
            s_kbZooms[idx].maxTx = max(s_kbZooms[idx].maxTx, b);
            s_kbZooms[idx].minTy = min(s_kbZooms[idx].minTy, c);
            s_kbZooms[idx].maxTy = max(s_kbZooms[idx].maxTy, d);
        }
    }
    LOG_INFO("InkHUD Map: keyboard manifest OK, %d zoom(s):", s_kbZoomCount);
    for (int i = 0; i < s_kbZoomCount; i++)
        LOG_INFO("  z%d tx[%d..%d] ty[%d..%d]", s_kbZooms[i].z, s_kbZooms[i].minTx, s_kbZooms[i].maxTx, s_kbZooms[i].minTy,
                 s_kbZooms[i].maxTy);
}

// ---- Keyboard label transport (READ_RANGE) -------------------------------------------------------------
// The tiled-label file format + parse (findLabelTile / parseLabelBlob) and the s_labels / s_kbView* state are
// shared above; only the per-tile byte fetch over Serial2 is keyboard-specific.

// One READ_RANGE (0x0C): [pathSize:u32][path][offset:u32][length:u32] -> [actualLen:u32][data]. Reads up to
// `cap` bytes into `out`; returns bytes read, 0 (empty/error reply), or -1 (timeout / desync / oversize).
static int fetchRangeFromKeyboard(const char *path, uint32_t offset, uint32_t length, uint8_t *out, uint32_t cap)
{
    if (length == 0 || length > cap)
        return -1;
    if (kbMayBeAsleep())
        kbSendWakePreamble();
    while (Serial2.available())
        Serial2.read();

    uint32_t pathSize = strlen(path);
    Serial2.write(KB_READ_RANGE_CMD);
    Serial2.write((uint8_t *)&pathSize, sizeof(uint32_t));
    Serial2.write((const uint8_t *)path, pathSize);
    Serial2.write((uint8_t *)&offset, sizeof(uint32_t));
    Serial2.write((uint8_t *)&length, sizeof(uint32_t));

    uint32_t got = 0;
    if (!kbReadExact((uint8_t *)&got, sizeof(uint32_t), 800)) {
        s_kbLastOkMs = 0;
        drainSerial2ToIdle(15, 1500);
        return -1;
    }
    s_kbLastOkMs = millis();
    if (got == 0)
        return 0; // keyboard reported error / empty range
    if (got > cap) {
        drainSerial2ToIdle(15, 1500);
        return -1; // implausible length (stray key byte / desync)
    }
    if (!kbReadExact(out, got, 1500)) {
        drainSerial2ToIdle(15, 1500);
        return -1;
    }
    return (int)got;
}

// Read /map/labels_<zoom>.idx into the shared s_tileCacheBuffer (free during the label pass). Returns the record
// count, or 0 (absent / comms error / too big / corrupt). Re-read on each view change -- NOT cached, so it needs
// no persistent buffer; the parsed labels are what get cached (in s_labels, keyed by the view rect). A later
// upload of the file is therefore picked up automatically (nothing to latch).
static int loadLabelIndex(int zoom)
{
    if (kbMayBeAsleep())
        kbSendWakePreamble();
    while (Serial2.available())
        Serial2.read();

    char path[40];
    snprintf(path, sizeof(path), "/map/labels_%d.idx", zoom);
    uint32_t pathSize = strlen(path);
    Serial2.write(KB_READ_FILE_CMD);
    Serial2.write((uint8_t *)&pathSize, sizeof(uint32_t));
    Serial2.write((const uint8_t *)path, pathSize);

    uint32_t fileSize = 0;
    if (!kbReadExact((uint8_t *)&fileSize, sizeof(uint32_t), 800)) {
        s_kbLastOkMs = 0;
        drainSerial2ToIdle(15, 300);
        return 0; // comms failure
    }
    s_kbLastOkMs = millis();
    if (fileSize == 0)
        return 0; // no label index at this zoom
    if (fileSize > sizeof(s_tileCacheBuffer) || (fileSize % LABEL_IDX_REC) != 0) {
        drainSerial2ToIdle(15, 1500); // too big for the scratch buffer, or a corrupt/desync-shaped header
        return 0;
    }
    if (!kbReadExact(s_tileCacheBuffer, fileSize, 1500)) {
        drainSerial2ToIdle(15, 1500);
        return 0;
    }
    return (int)(fileSize / LABEL_IDX_REC);
}

// Fetch the labels for the CURRENT view (set by drawMapTileBackgroundFromKeyboard) into s_labels, skipping the
// UART work if the view rect is unchanged since the last load.
static void readKeyboardLabelsForView()
{
    if (s_kbViewZoom < 0) { // no keyboard tile draw this render -> nothing to anchor labels to
        s_labelCount = 0;
        s_lblViewZoom = -2;
        return;
    }
    if (s_kbViewZoom == s_lblViewZoom && s_kbViewTx0 == s_lblViewTx0 && s_kbViewTx1 == s_lblViewTx1 &&
        s_kbViewTy0 == s_lblViewTy0 && s_kbViewTy1 == s_lblViewTy1)
        return; // labels already loaded for this exact view

    // Empty / inverted view rect: the centre is outside this zoom's coverage, so the range clamp gave txStart >
    // txEnd (no visible tiles -> no labels). Skip loadLabelIndex: with no tile fetch to synchronise the UART
    // first, its mis-framed blocking read can stall the render into a watchdog reboot.
    if (s_kbViewTx0 > s_kbViewTx1 || s_kbViewTy0 > s_kbViewTy1) {
        s_labelCount = 0;
        s_lblViewZoom = s_kbViewZoom;
        s_lblViewTx0 = s_kbViewTx0;
        s_lblViewTx1 = s_kbViewTx1;
        s_lblViewTy0 = s_kbViewTy0;
        s_lblViewTy1 = s_kbViewTy1;
        return;
    }

    s_labelCount = 0;
    int strPos = 0;
    int idxCount = loadLabelIndex(s_kbViewZoom); // reads the idx into the shared s_tileCacheBuffer
    if (idxCount > 0) {
        char datPath[40];
        snprintf(datPath, sizeof(datPath), "/map/labels_%d.dat", s_kbViewZoom);
        const int MAX_LABEL_TILES = 16; // bound the per-view round-trips (mirrors the tile budget)
        int fetched = 0, lblTimeouts = 0;
        bool lblStop = false;
        for (int ty = s_kbViewTy0; ty <= s_kbViewTy1 && s_labelCount < KB_MAX_LABELS && fetched < MAX_LABEL_TILES && !lblStop; ty++)
            for (int tx = s_kbViewTx0; tx <= s_kbViewTx1 && fetched < MAX_LABEL_TILES && !lblStop; tx++) {
                uint32_t off = 0, len = 0;
                if (!findLabelTile(idxCount, (uint32_t)tx, (uint32_t)ty, off, len) || len == 0)
                    continue; // no labels in this tile
                fetched++;
                int got = fetchRangeFromKeyboard(datPath, off, len, s_compressedBuf, sizeof(s_compressedBuf));
                if (got > 0) {
                    parseLabelBlob(s_compressedBuf, (uint32_t)got, strPos);
                    lblTimeouts = 0;
                } else if (got < 0 && ++lblTimeouts >= 2) {
                    // Keyboard went unresponsive mid-loop (slept/reset after the idx read): bail like the tile loop
                    // so the render can't stall ~16s. got==0 (empty-but-alive) does NOT count -- only -1 (timeout).
                    LOG_WARN("InkHUD Map: keyboard unresponsive during label fetch -- aborting, labels partial");
                    lblStop = true;
                }
            }
    }
    // Insertion-sort the collected labels by rank so renderLabels' greedy declutter favours hospitals/shelters.
    for (int i = 1; i < s_labelCount; i++) {
        InkHUD::MapApplet::MapLabel key = s_labels[i];
        int j = i - 1;
        while (j >= 0 && s_labels[j].rank > key.rank) {
            s_labels[j + 1] = s_labels[j];
            j--;
        }
        s_labels[j + 1] = key;
    }
    s_lblViewZoom = s_kbViewZoom; // remember the view (even on a partial/failed load) -> no per-render retry storm
    s_lblViewTx0 = s_kbViewTx0;
    s_lblViewTx1 = s_kbViewTx1;
    s_lblViewTy0 = s_kbViewTy0;
    s_lblViewTy1 = s_kbViewTy1;
    LOG_DEBUG("InkHUD Map: view z%d tx%d..%d ty%d..%d -> %d labels", s_kbViewZoom, s_kbViewTx0, s_kbViewTx1,
              s_kbViewTy0, s_kbViewTy1, s_labelCount);
}

void InkHUD::MapApplet::drawKeyboardLabels()
{
    readKeyboardLabelsForView();
    renderLabels(s_labels, s_labelCount);
}

// One GET_TILE (0x0D) round-trip. Returns the decoded 8192-byte tile (in s_tileCacheBuffer) or nullptr for
// white / any error (timeout, short read, bad size, LZ4 failure) -> the caller leaves that tile white.
const uint8_t *InkHUD::MapApplet::fetchTileFromKeyboard(int z, uint32_t tx, uint32_t ty)
{
    kbFetchTimedOut = false;
    if (kbMayBeAsleep()) // only the first fetch of an idle render pays the ~150ms settle
        kbSendWakePreamble();

    while (Serial2.available()) // drop a stale key byte that may have arrived between fetches
        Serial2.read();

    uint8_t req[10]; // 0x0D + z(1) + x(4) + y(4)
    req[0] = KB_GET_TILE_CMD;
    req[1] = (uint8_t)z;
    memcpy(req + 2, &tx, sizeof(uint32_t)); // little-endian on both nRF52 and ESP32-C3
    memcpy(req + 6, &ty, sizeof(uint32_t));
    Serial2.write(req, sizeof(req));

    uint32_t size = 0;
    if (!kbReadExact((uint8_t *)&size, sizeof(uint32_t), 800)) {
        kbFetchTimedOut = true;       // no reply at all -> keyboard likely asleep/absent (vs a served white tile)
        s_kbLastOkMs = 0;             // it slept unexpectedly: make the NEXT tile send a wake preamble, so a
                                      // render recovers after one lost tile instead of bailing out entirely
        drainSerial2ToIdle(15, 1500); // partial/late reply -> resync so it isn't misread as the next tile
        LOG_DEBUG("  tile z%d (%u,%u) TIMEOUT -> white", z, (unsigned)tx, (unsigned)ty);
        return nullptr; // timeout -> white
    }
    s_kbLastOkMs = millis(); // it answered -> awake; its own keep-alive covers the rest of this render
    if (size == 0) {
        LOG_DEBUG("  tile z%d (%u,%u) ABSENT (no tile in map) -> white", z, (unsigned)tx, (unsigned)ty);
        return nullptr; // tile absent -> white (a clean 4-byte reply; nothing else in flight)
    }
    if (size == 1) {
        uint8_t b = 0;
        if (!kbReadExact(&b, 1, 800) || b != 0x02) {
            drainSerial2ToIdle(15, 1500);
            LOG_DEBUG("  tile z%d (%u,%u) BAD black-sentinel -> white", z, (unsigned)tx, (unsigned)ty);
            return nullptr;
        }
        memset(s_tileCacheBuffer, 0xFF, sizeof(s_tileCacheBuffer)); // all-black
        LOG_DEBUG("  tile z%d (%u,%u) ALL-BLACK sentinel", z, (unsigned)tx, (unsigned)ty);
        return s_tileCacheBuffer;
    }
    // A stray keyboard keycode landing in the size header almost always makes `size` implausibly large; on
    // ANY abort below, drain to a frame boundary so the real reply still streaming can't desync the next tile.
    if (size > sizeof(s_compressedBuf)) {
        drainSerial2ToIdle(15, 1500);
        LOG_WARN("  tile z%d (%u,%u) IMPLAUSIBLE size=%u (desync/stray key byte?) -> white", z, (unsigned)tx,
                 (unsigned)ty, (unsigned)size);
        return nullptr; // implausible -> white (and never overflow the buffer)
    }
    if (!kbReadExact(s_compressedBuf, size, 1500)) {
        drainSerial2ToIdle(15, 1500);
        LOG_WARN("  tile z%d (%u,%u) SHORT READ (wanted %u) -> white", z, (unsigned)tx, (unsigned)ty, (unsigned)size);
        return nullptr; // short read -> white
    }
    int n = lz4_decompress(s_compressedBuf, (int)size, s_tileCacheBuffer, sizeof(s_tileCacheBuffer));
    if (n != (int)sizeof(s_tileCacheBuffer)) {
        drainSerial2ToIdle(15, 1500);
        LOG_WARN("  tile z%d (%u,%u) LZ4 DECODE FAIL n=%d size=%u (corrupt blob) -> white", z, (unsigned)tx,
                 (unsigned)ty, n, (unsigned)size);
        return nullptr; // bad decode (garbage/desync) -> white
    }
#if MOD_MAP_TILE_DEBUG
    // blobCrc = CRC of the COMPRESSED bytes as received; tileCrc = CRC of the DECODED 8192-byte image.
    // Compare both against the offline values from the packed map: blobCrc mismatch = corrupted in transit.
    LOG_DEBUG("  tile z%d (%u,%u) OK size=%u blobCrc=%08x tileCrc=%08x black=%u", z, (unsigned)tx, (unsigned)ty,
              (unsigned)size, (unsigned)kbDbgCrc32(s_compressedBuf, size),
              (unsigned)kbDbgCrc32(s_tileCacheBuffer, sizeof(s_tileCacheBuffer)),
              (unsigned)kbDbgBlackPixels(s_tileCacheBuffer, sizeof(s_tileCacheBuffer)));
#endif
    return s_tileCacheBuffer;
}

// Early manifest fetch (see MapApplet.h) -- lets a derived applet grab the zoom set during the keyboard's
// short post-boot awake window, before the first render.
void InkHUD::MapApplet::primeKeyboardTiles()
{
    readKeyboardManifestIfNeeded();
}

#endif // MOD_KEYBOARD_MAP_TILES (paused: the next two coverage helpers are shared with MOD_LOCAL_MAP_TILES)
#if defined(MOD_KEYBOARD_MAP_TILES) || defined(MOD_LOCAL_MAP_TILES)

// True if the map centre's tile at zoom z lies inside the manifest's coverage rectangle for z. A jumped or
// trace-recentred position outside coverage makes the tile draw's range clamp invert (txStart > txEnd) ->
// empty tile loop; on the keyboard transport the label pass then blocks on an unsynchronised UART read ->
// watchdog reboot, and on the local transport it draws a blank map that never re-homes.
static bool kbZoomCoversCentre(int z, float lat, float lng)
{
    int idx = -1;
    for (int i = 0; i < s_kbZoomCount; i++)
        if (s_kbZooms[i].z == z) {
            idx = i;
            break;
        }
    if (idx < 0)
        return false;
    const float sinLat = sinf(lat * DEG_TO_RAD);
    const int ctx = (int)floorf(((lng + 180.0f) / 360.0f) * (float)(1 << z));
    const int cty =
        (int)floorf((0.5f - logf((1.0f + sinLat) / (1.0f - sinLat)) / (4.0f * (float)M_PI)) * (float)(1 << z));
    return ctx >= s_kbZooms[idx].minTx && ctx <= s_kbZooms[idx].maxTx && cty >= s_kbZooms[idx].minTy &&
           cty <= s_kbZooms[idx].maxTy;
}

// Highest bundle zoom whose coverage includes the centre, or -1 if the centre is outside every zoom.
static int kbHighestZoomCoveringCentre(float lat, float lng)
{
    int best = -1;
    for (int i = 0; i < s_kbZoomCount; i++)
        if (s_kbZooms[i].z > best && kbZoomCoversCentre(s_kbZooms[i].z, lat, lng))
            best = s_kbZooms[i].z;
    return best;
}

#endif // shared coverage helpers
#ifdef MOD_KEYBOARD_MAP_TILES

// Keyboard-served equivalent of drawMapTileBackground: pick the best available zoom <= chosenZoom (upsampling
// like the compiled path), then fetch + blit the visible tile range from the keyboard.
void InkHUD::MapApplet::drawMapTileBackgroundFromKeyboard(int zoom)
{
    s_kbViewZoom = -1; // invalidate the label view until we've resolved this render's tile zoom + range
    if (metersToPx <= 0.0f)
        return;

    // The keyboard's per-tile FS lookup (idx binary search + flash seeks) can occasionally exceed the 250ms
    // key-read timeout; give reply reads more headroom. Safe: InputMenuApplet reads keys via read()/available(),
    // which ignore this Stream timeout.
    Serial2.setTimeout(1000);

    readKeyboardManifestIfNeeded();
    if (s_kbZoomCount == 0)
        return; // no manifest yet (logged); nothing we can draw

    // Best available tile zoom <= chosenZoom, else the lowest available (mirrors the compiled-tile picker).
    int tileZoom = -1, covIdx = -1;
    for (int i = 0; i < s_kbZoomCount; i++)
        if (s_kbZooms[i].z <= zoom && (tileZoom < 0 || s_kbZooms[i].z > tileZoom)) {
            tileZoom = s_kbZooms[i].z;
            covIdx = i;
        }
    if (tileZoom < 0)
        for (int i = 0; i < s_kbZoomCount; i++)
            if (tileZoom < 0 || s_kbZooms[i].z < tileZoom) {
                tileZoom = s_kbZooms[i].z;
                covIdx = i;
            }
    if (tileZoom < 0)
        return;

    const float R = 6378137.0f;
    const float latRad = latCenter * DEG_TO_RAD;
    const float mpp = (2.0f * M_PI * R / (256.0f * (float)(1 << zoom))) * cosf(latRad);
    const float worldPxPerScreenPx = 1.0f / (metersToPx * mpp);
    const float tileWorldPx = worldPxPerScreenPx * ((float)(1 << tileZoom) / (float)(1 << zoom));

    const float sinLat = sinf(latRad);
    const float gpxX = ((lngCenter + 180.0f) / 360.0f) * (float)(1 << tileZoom) * 256.0f;
    const float gpxY = (0.5f - logf((1.0f + sinLat) / (1.0f - sinLat)) / (4.0f * M_PI)) * (float)(1 << tileZoom) * 256.0f;

    const float minWx = gpxX - width() * 0.5f * tileWorldPx;
    const float maxWx = gpxX + width() * 0.5f * tileWorldPx;
    const float minWy = gpxY - height() * 0.5f * tileWorldPx;
    const float maxWy = gpxY + height() * 0.5f * tileWorldPx;

    int txStart = (int)floorf(minWx / 256.0f);
    int txEnd = (int)floorf(maxWx / 256.0f);
    int tyStart = (int)floorf(minWy / 256.0f);
    int tyEnd = (int)floorf(maxWy / 256.0f);
    // Clamp to this zoom's manifest coverage so we don't waste round-trips on tiles that certainly don't exist.
    txStart = max(txStart, s_kbZooms[covIdx].minTx);
    txEnd = min(txEnd, s_kbZooms[covIdx].maxTx);
    tyStart = max(tyStart, s_kbZooms[covIdx].minTy);
    tyEnd = min(tyEnd, s_kbZooms[covIdx].maxTy);

    // Publish this render's view so drawKeyboardLabels() (called later in onRender) fetches the same tiles' labels.
    s_kbViewZoom = tileZoom;
    s_kbViewTx0 = txStart;
    s_kbViewTx1 = txEnd;
    s_kbViewTy0 = tyStart;
    s_kbViewTy1 = tyEnd;

#if MOD_MAP_TILE_DEBUG
    // Full coordinate chain, so a bad centre / projection / range is visible without guessing.
    LOG_DEBUG("InkHUD Map: kbd render tileZ=%d chosenZ=%d centre=%.6f,%.6f gpx=%.1f,%.1f tileWorldPx=%.4f "
              "tx %d..%d ty %d..%d",
              tileZoom, zoom, latCenter, lngCenter, gpxX, gpxY, tileWorldPx, txStart, txEnd, tyStart, tyEnd);
#endif

    // Bound the blocking round-trips: each tile is a synchronous UART exchange on the render thread, and a
    // wide / zoomed-out view can span many tiles. Cap it so one render can't stall for many seconds.
    const int MAX_TILES_PER_RENDER = 16;
    int req = 0, drawn = 0, timeouts = 0;
    bool stop = false;
    for (int ty = tyStart; ty <= tyEnd && !stop; ty++)
        for (int tx = txStart; tx <= txEnd; tx++) {
            if (req >= MAX_TILES_PER_RENDER) {
                LOG_WARN("InkHUD Map: tile budget %d reached (view too wide); background truncated", MAX_TILES_PER_RENDER);
                stop = true;
                break;
            }
            req++;
            const uint8_t *tile = fetchTileFromKeyboard(tileZoom, (uint32_t)tx, (uint32_t)ty);
            // Bail if the keyboard is genuinely unresponsive (unpowered / miswired): stop after 2 consecutive
            // timeouts so a render caps at ~2s, not ~16s. A single timeout is NOT fatal -- it clears s_kbLastOkMs,
            // so the next tile sends a wake preamble and the render recovers after losing just that one tile.
            if (kbFetchTimedOut) {
                if (++timeouts >= 2) {
                    LOG_WARN("InkHUD Map: keyboard unresponsive (asleep?) -- render aborted, tiles white");
                    stop = true;
                    break;
                }
            } else {
                timeouts = 0;
            }
            if (!tile)
                continue;
            drawn++;
            blitTile(tile, tx * 256.0f, ty * 256.0f, gpxX, gpxY, tileWorldPx);
        }
    LOG_DEBUG("InkHUD Map: keyboard z%d tiles req=%d drawn=%d (chosenZoom=%d)", tileZoom, req, drawn, zoom);
}

#endif // MOD_KEYBOARD_MAP_TILES

#ifdef MOD_LOCAL_MAP_TILES

// ==== Local-filesystem tile transport ===================================================================
// Twins of the keyboard transport, reading the SAME bundle format from a local FS instead of over Serial2.
// TWO tile layouts, tagged by /map/manifest's first token (mirrors tools/FORMAT.md in the keyboard repo):
//   /map/manifest          : "vN a b" version line + "z minTx maxTx minTy maxTy" coverage lines.
//                            "v3 ..." = packed layout below; anything else = flat per-tile files.
//   flat (default):
//   /map/t_<z>_<tx>_<ty>.lz4 : one tile file per (zoom, tileX, tileY). File contents mirror the keyboard's
//                              GET_TILE reply, with the FILE SIZE playing the role of the reply's size header:
//                                absent file / 0 bytes -> white tile   (this is how "no tile here" is stored)
//                                exactly 1 byte == 0x02 -> all-black tile sentinel
//                                otherwise              -> raw LZ4 block, decoded to the 8192-byte tile
//   packed ("v3", for whole-island bundles -- a flat FAT dir cannot hold z15/z16 tile counts):
//   /map/<z>.idx           : 16-byte LE records [tx:u32][ty:u32][offset:u32][size:u32], sorted by (tx,ty);
//                            binary-searched ON THE FILE (a whole-island z16 idx is ~2 MB -- never slurped)
//   /map/<z>.dat           : the tile blobs, concatenated. size 1 = 0x02 all-black sentinel, absent from the
//                            idx = white, else a raw LZ4 block -- payloads identical to the flat layout.
//   /map/labels_<z>.idx/.dat : identical to the keyboard label bundle (see the shared block above)
// microSD is primary (shares the FSPI bus with LoRa + the panel -> spiLock); internal LittleFS is the fallback.

// Hold spiLock for its lifetime ONLY when the source FS is the microSD. The LittleFS fallback lives in internal
// flash and is not on the SPI bus, so it constructs a no-op guard. Scoped tightly around raw FS I/O; NEVER held
// across lz4_decompress / blitTile / renderLabels or a panel-driver call (the driver locks spiLock internally).
struct MapFSBusLock {
    bool held;
    explicit MapFSBusLock(bool onBus) : held(onBus)
    {
        if (held)
            spiLock->lock();
    }
    ~MapFSBusLock()
    {
        if (held)
            spiLock->unlock();
    }
    MapFSBusLock(const MapFSBusLock &) = delete;
    MapFSBusLock &operator=(const MapFSBusLock &) = delete;
};

static fs::FS *s_mapFS = nullptr;
static bool s_mapFSProbed = false;
static bool s_mapPackedLayout = false; // manifest line 1 "v3 ..." -> packed <z>.idx/<z>.dat instead of t_ files
// Zooms (bit per zoom 0..31) whose /map/<z>.idx open failed while in packed layout. Without this cache, a
// mislabeled bundle (v3 manifest over flat t_ files) would pay a failed idx open -- a FULL directory scan on
// FAT -- before EVERY flat tile read. Set on first miss, cleared when the manifest is (re)parsed.
static uint32_t s_packedIdxMissing = 0;
// Same idea for /map/labels_<z>.idx: zooms probed and found absent, so the per-view label-zoom fallback
// (see readLocalLabelsForView) never repeats a failed open. Cleared when the manifest is (re)parsed.
static uint32_t s_labelsIdxMissing = 0;

// Probe once for a local map bundle: microSD primary, internal LittleFS fallback. A bundle "exists" iff
// /map/manifest is present. Caches the winning FS (or nullptr) so the probe -- and its bus I/O -- runs once.
static fs::FS *mapTileFS()
{
    if (s_mapFSProbed)
        return s_mapFS;
    s_mapFSProbed = true;
    s_mapFS = nullptr;

#ifdef HAS_SDCARD
    {
        concurrency::LockGuard g(spiLock); // SD.cardType()/exists() touch the shared FSPI bus
        if (SD.cardType() != CARD_NONE && SD.exists("/map/manifest"))
            s_mapFS = &SD;
    }
    if (s_mapFS) {
        LOG_INFO("InkHUD Map: local tiles from microSD (/map/manifest)");
        return s_mapFS;
    }
#endif
    if (FSCom.exists("/map/manifest")) {
        s_mapFS = &FSCom; // internal LittleFS -- not on the SPI bus, no lock needed for its I/O
        LOG_INFO("InkHUD Map: local tiles from internal LittleFS (/map/manifest)");
        return s_mapFS;
    }
    LOG_WARN("InkHUD Map: no local map tiles found (no /map/manifest on microSD or LittleFS)");
    return s_mapFS; // nullptr
}

// Whether the chosen source FS is the microSD (so its raw I/O must hold spiLock). Callers always resolve
// mapTileFS() first, so s_mapFS is set by the time this is read.
static bool mapFSOnBus()
{
#ifdef HAS_SDCARD
    return s_mapFS == &SD;
#else
    return false;
#endif
}

// Read up to `len` bytes at `offset` of `path` into `out` (bounded by cap). Returns bytes read (may be < len at
// EOF -> used to slurp a whole small file), 0 (open failed / empty), or -1 (bad args / seek failure). Local twin
// of fetchRangeFromKeyboard. spiLock is held for the open+seek+read+close ONLY (SD source), never beyond.
static int fetchRangeLocal(fs::FS *fsrc, const char *path, uint32_t offset, uint32_t len, uint8_t *out, uint32_t cap)
{
    if (!fsrc || len == 0 || len > cap)
        return -1;
    MapFSBusLock lock(mapFSOnBus());
    File f = fsrc->open(path, FILE_O_READ);
    if (!f)
        return 0; // absent / open failed
    if (offset && !f.seek(offset)) {
        f.close();
        return -1;
    }
    int n = (int)f.read(out, len);
    f.close();
    return n < 0 ? -1 : n;
}

// Read /map/labels_<zoom>.idx into the shared s_tileCacheBuffer (free during the label pass). Returns the record
// count, or 0 (absent / too big / corrupt). Local twin of loadLabelIndex.
static int loadLabelIndexLocal(int zoom)
{
    fs::FS *fsrc = mapTileFS();
    if (!fsrc)
        return 0;
    char path[40];
    snprintf(path, sizeof(path), "/map/labels_%d.idx", zoom);
    int n = fetchRangeLocal(fsrc, path, 0, sizeof(s_tileCacheBuffer), s_tileCacheBuffer, sizeof(s_tileCacheBuffer));
    if (n <= 0)
        return 0; // absent / empty
    if ((n % LABEL_IDX_REC) != 0) {
        // Not a whole number of records: a corrupt idx, or one larger than the scratch buffer (truncated read).
        LOG_WARN("InkHUD Map: local labels_%d.idx bad size %d -> ignored", zoom, n);
        return 0;
    }
    return n / LABEL_IDX_REC;
}

// Streamed twin of findLabelTile, for label indexes too large to slurp into the 8 KB scratch (a whole-island
// bundle can have tens of thousands of label tiles per zoom): binary-search the sorted 10-byte records directly
// on the file. spiLock is scoped around the whole probe sequence; each probe is a seek + 10-byte read.
static bool findLabelTileStreamed(fs::FS *fsrc, const char *idxPath, uint32_t nrec, uint32_t tx, uint32_t ty,
                                  uint32_t &offset, uint32_t &len)
{
    MapFSBusLock lock(mapFSOnBus());
    File idx = fsrc->open(idxPath, FILE_O_READ);
    if (!idx)
        return false;
    bool found = false;
    uint32_t lo = 0, hi = nrec; // search window is [lo, hi)
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        uint8_t r[LABEL_IDX_REC];
        if (!idx.seek(mid * LABEL_IDX_REC) || (int)idx.read(r, sizeof(r)) != (int)sizeof(r))
            break; // I/O error -> no labels for this tile
        const uint32_t rtx = r[0] | (r[1] << 8);
        const uint32_t rty = r[2] | (r[3] << 8);
        if (rtx == tx && rty == ty) {
            offset = r[4] | (r[5] << 8) | (r[6] << 16) | ((uint32_t)r[7] << 24);
            len = r[8] | (r[9] << 8);
            found = true;
            break;
        }
        if (rtx < tx || (rtx == tx && rty < ty))
            lo = mid + 1;
        else
            hi = mid;
    }
    idx.close();
    return found;
}

// Read /map/manifest to learn the available zooms + coverage. Runs once (retries, rate-limited, until it loads).
void InkHUD::MapApplet::readLocalManifestIfNeeded()
{
    if (s_kbZoomCount > 0)
        return; // already have the zoom set -> never re-read
    if (localManifestLastAttempt != 0 && (millis() - localManifestLastAttempt) < 5000)
        return; // rate-limit retries so a missing bundle doesn't re-probe the FS every render
    localManifestLastAttempt = millis();

    fs::FS *fsrc = mapTileFS();
    if (!fsrc)
        return; // no bundle present (logged by mapTileFS)

    static char manifestBuf[2048];
    int n = fetchRangeLocal(fsrc, "/map/manifest", 0, sizeof(manifestBuf) - 1, (uint8_t *)manifestBuf,
                            sizeof(manifestBuf) - 1);
    if (n <= 0) {
        LOG_WARN("InkHUD Map: local /map/manifest read failed (%d) -- will retry", n);
        return;
    }
    manifestBuf[n] = 0;

    // Line 1 = version/layout tag ("v3 0 0" = packed, else flat); remaining lines = "z minTx maxTx minTy maxTy".
    s_kbZoomCount = 0;
    const char *ver = strtok(manifestBuf, "\r\n");
    s_mapPackedLayout = ver && ver[0] == 'v' && ver[1] == '3' && (ver[2] == '\0' || ver[2] == ' ');
    s_packedIdxMissing = 0;
    s_labelsIdxMissing = 0;
    for (char *line = strtok(nullptr, "\r\n"); line; line = strtok(nullptr, "\r\n"))
        parseManifestLine(line);

    LOG_INFO("InkHUD Map: local manifest OK (%s layout), %d zoom(s):", s_mapPackedLayout ? "packed" : "flat",
             s_kbZoomCount);
    for (int i = 0; i < s_kbZoomCount; i++)
        LOG_INFO("  z%d tx[%d..%d] ty[%d..%d]", s_kbZooms[i].z, s_kbZooms[i].minTx, s_kbZooms[i].maxTx,
                 s_kbZooms[i].minTy, s_kbZooms[i].maxTy);
}

// Packed-layout fetch: binary-search /map/<z>.idx for (tx,ty) directly ON the file (a whole-island z16 idx is
// ~2 MB -- far beyond any scratch buffer), then range-read the blob out of /map/<z>.dat into s_compressedBuf.
// Returns true with the blob loaded (blobSize 1 = the 0x02 sentinel byte), false for absent-from-index / any
// I/O error (-> the caller draws white), or false with *noIdx set when the idx file itself is missing (-> the
// caller falls back to the flat per-tile path, tolerating a mislabeled manifest). spiLock discipline matches
// fetchRangeLocal: held across the raw probes + blob read only, never across the LZ4 decode.
static bool fetchBlobPacked(fs::FS *fsrc, int z, uint32_t tx, uint32_t ty, uint32_t &blobSize, bool *noIdx)
{
    char path[32];
    uint32_t blobOff = 0;
    bool found = false;
    MapFSBusLock lock(mapFSOnBus());

    snprintf(path, sizeof(path), "/map/%d.idx", z);
    File idx = fsrc->open(path, FILE_O_READ);
    if (!idx) {
        *noIdx = true;
        return false;
    }
    uint32_t lo = 0, hi = idx.size() / 16; // records are 16 bytes; search window is [lo, hi)
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        uint8_t r[16];
        if (!idx.seek(mid * 16) || (int)idx.read(r, sizeof(r)) != (int)sizeof(r))
            break; // I/O error -> treat as absent
        const uint32_t rtx = r[0] | (r[1] << 8) | (r[2] << 16) | ((uint32_t)r[3] << 24);
        const uint32_t rty = r[4] | (r[5] << 8) | (r[6] << 16) | ((uint32_t)r[7] << 24);
        if (rtx == tx && rty == ty) {
            blobOff = r[8] | (r[9] << 8) | (r[10] << 16) | ((uint32_t)r[11] << 24);
            blobSize = r[12] | (r[13] << 8) | (r[14] << 16) | ((uint32_t)r[15] << 24);
            found = true;
            break;
        }
        if (rtx < tx || (rtx == tx && rty < ty))
            lo = mid + 1;
        else
            hi = mid;
    }
    idx.close();
    if (!found || blobSize == 0 || blobSize > sizeof(s_compressedBuf)) {
        if (found && blobSize > sizeof(s_compressedBuf))
            LOG_WARN("  tile z%d (%u,%u) oversize %u -> white", z, (unsigned)tx, (unsigned)ty, (unsigned)blobSize);
        return false; // absent from the index -> white
    }

    snprintf(path, sizeof(path), "/map/%d.dat", z);
    File dat = fsrc->open(path, FILE_O_READ);
    if (!dat)
        return false;
    const bool ok = dat.seek(blobOff) && (int)dat.read(s_compressedBuf, blobSize) == (int)blobSize;
    dat.close();
    if (!ok)
        LOG_WARN("  tile z%d (%u,%u) SHORT READ at %u+%u -> white", z, (unsigned)tx, (unsigned)ty, (unsigned)blobOff,
                 (unsigned)blobSize);
    return ok;
}

// ---- PSRAM decoded-tile cache ---------------------------------------------------------------
// Every render used to re-fetch each visible tile from the bundle: a binary search ON the packed
// idx file (~17 seek+read probes on a whole-island z16 idx), a blob range-read, and an LZ4
// decode -- which is why switching to a map applet took seconds. Decoded tiles are tiny next to
// the 8MB PSRAM (8KB each), so cache them keyed by (z,tx,ty) with LRU eviction, including the
// "absent -> white" verdicts (sea tiles pay the full idx search on every miss otherwise). The
// bundle is immutable for the life of a boot, so entries never expire. Caveat: a transient FS
// read error also caches as white until reboot -- accepted, it keeps this layer trivial.
// Slots allocate lazily from PSRAM (MALLOC_CAP_SPIRAM) and the whole layer self-disables if the
// allocation ever fails, falling back to uncached reads.
#if defined(ARCH_ESP32)
#include "esp_heap_caps.h"

struct LocalTileCacheSlot {
    int8_t z = -1;     // -1 = slot empty
    uint8_t state = 0; // 1 = decoded tile in data; 2 = absent (render white)
    uint32_t tx = 0, ty = 0;
    uint32_t lastUse = 0;
    uint8_t *data = nullptr; // 8192B PSRAM, allocated on first decoded insert, then reused
};
static constexpr int LOCAL_TILE_CACHE_SLOTS = 256; // x8KB = up to 2MB of PSRAM (prefetch feeds it)
static LocalTileCacheSlot s_localTileCache[LOCAL_TILE_CACHE_SLOTS];
static uint32_t s_localTileCacheClock = 0;
static bool s_localTileCacheOk = true;

static LocalTileCacheSlot *localTileCacheFind(int z, uint32_t tx, uint32_t ty)
{
    for (int i = 0; i < LOCAL_TILE_CACHE_SLOTS; i++) {
        LocalTileCacheSlot &s = s_localTileCache[i];
        if (s.z == z && s.tx == tx && s.ty == ty)
            return &s;
    }
    return nullptr;
}

static void localTileCacheInsert(int z, uint32_t tx, uint32_t ty, const uint8_t *decoded)
{
    if (!s_localTileCacheOk)
        return;
    // Pick the LRU slot (empty slots have lastUse 0 and win immediately)
    LocalTileCacheSlot *slot = &s_localTileCache[0];
    for (int i = 1; i < LOCAL_TILE_CACHE_SLOTS; i++)
        if (s_localTileCache[i].lastUse < slot->lastUse)
            slot = &s_localTileCache[i];

    if (decoded) {
        if (!slot->data) {
            slot->data = (uint8_t *)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
            if (!slot->data) { // no PSRAM on this build/board: disable the cache layer for good
                s_localTileCacheOk = false;
                LOG_WARN("InkHUD Map: no PSRAM for tile cache; running uncached");
                return;
            }
        }
        memcpy(slot->data, decoded, 8192);
        slot->state = 1;
    } else {
        slot->state = 2; // absent verdict; keep any previously-allocated data buffer for reuse
    }
    slot->z = (int8_t)z;
    slot->tx = tx;
    slot->ty = ty;
    slot->lastUse = ++s_localTileCacheClock;
}
#endif // ARCH_ESP32

// One tile file read. Returns the decoded 8192-byte tile (in s_tileCacheBuffer) or nullptr for white / any error
// (absent, oversize, short read, bad sentinel, LZ4 failure) -> the caller leaves that tile white. Local twin of
// fetchTileFromKeyboard; the on-FS file size stands in for the keyboard's reply size header.
static const uint8_t *fetchTileLocalRaw(int z, uint32_t tx, uint32_t ty)
{
    fs::FS *fsrc = mapTileFS();
    if (!fsrc)
        return nullptr;

    if (s_mapPackedLayout && !(z >= 0 && z < 32 && (s_packedIdxMissing & (1u << z)))) {
        uint32_t blobSize = 0;
        bool noIdx = false;
        if (fetchBlobPacked(fsrc, z, tx, ty, blobSize, &noIdx)) {
            if (blobSize == 1) {
                if (s_compressedBuf[0] != 0x02) {
                    LOG_DEBUG("  tile z%d (%u,%u) BAD 1-byte sentinel -> white", z, (unsigned)tx, (unsigned)ty);
                    return nullptr;
                }
                memset(s_tileCacheBuffer, 0xFF, sizeof(s_tileCacheBuffer)); // all-black
                return s_tileCacheBuffer;
            }
            int n = lz4_decompress(s_compressedBuf, (int)blobSize, s_tileCacheBuffer, sizeof(s_tileCacheBuffer));
            if (n != (int)sizeof(s_tileCacheBuffer)) {
                LOG_WARN("  tile z%d (%u,%u) LZ4 DECODE FAIL n=%d size=%u -> white", z, (unsigned)tx, (unsigned)ty, n,
                         (unsigned)blobSize);
                return nullptr;
            }
            return s_tileCacheBuffer;
        }
        if (!noIdx)
            return nullptr; // idx exists but tile absent / bad -> white
        // idx missing at this zoom: remember (a failed FAT open scans the whole directory -- never
        // repeat it per tile) and fall through to the flat per-tile path below
        if (z >= 0 && z < 32)
            s_packedIdxMissing |= (1u << z);
    }

    char path[48];
    snprintf(path, sizeof(path), "/map/t_%d_%u_%u.lz4", z, (unsigned)tx, (unsigned)ty);

    size_t fsize = 0;
    int nread = -1;
    {
        // spiLock scopes the raw SD I/O only; released before lz4_decompress below (which touches no SPI).
        MapFSBusLock lock(mapFSOnBus());
        File f = fsrc->open(path, FILE_O_READ);
        if (!f)
            return nullptr; // absent -> white (this is how the bundle encodes "no tile here")
        fsize = f.size();
        if (fsize == 0 || fsize > sizeof(s_compressedBuf)) {
            f.close();
            if (fsize > sizeof(s_compressedBuf))
                LOG_WARN("  tile z%d (%u,%u) oversize %u -> white", z, (unsigned)tx, (unsigned)ty, (unsigned)fsize);
            return nullptr;
        }
        nread = (int)f.read(s_compressedBuf, fsize);
        f.close();
    }
    if (nread != (int)fsize) {
        LOG_WARN("  tile z%d (%u,%u) SHORT READ %d/%u -> white", z, (unsigned)tx, (unsigned)ty, nread, (unsigned)fsize);
        return nullptr;
    }
    if (fsize == 1) {
        if (s_compressedBuf[0] != 0x02) {
            LOG_DEBUG("  tile z%d (%u,%u) BAD 1-byte sentinel -> white", z, (unsigned)tx, (unsigned)ty);
            return nullptr;
        }
        memset(s_tileCacheBuffer, 0xFF, sizeof(s_tileCacheBuffer)); // all-black
        return s_tileCacheBuffer;
    }
    int n = lz4_decompress(s_compressedBuf, (int)fsize, s_tileCacheBuffer, sizeof(s_tileCacheBuffer));
    if (n != (int)sizeof(s_tileCacheBuffer)) {
        LOG_WARN("  tile z%d (%u,%u) LZ4 DECODE FAIL n=%d size=%u -> white", z, (unsigned)tx, (unsigned)ty, n,
                 (unsigned)fsize);
        return nullptr;
    }
    return s_tileCacheBuffer;
}

// Cached front-end for the raw fetch above: PSRAM hit -> no filesystem I/O at all, which makes
// re-entering a map applet (and panning back over recently-seen ground) near-instant instead of
// re-paying the idx search + blob read + LZ4 for every visible tile on every render.
const uint8_t *InkHUD::MapApplet::fetchTileLocal(int z, uint32_t tx, uint32_t ty)
{
#if defined(ARCH_ESP32)
    if (LocalTileCacheSlot *hit = localTileCacheFind(z, tx, ty)) {
        hit->lastUse = ++s_localTileCacheClock;
        return hit->state == 2 ? nullptr : hit->data;
    }
    const uint8_t *tile = fetchTileLocalRaw(z, tx, ty);
    // Cache absent verdicts only when a bundle exists at all -- "no filesystem" must stay retryable
    if (tile || mapTileFS())
        localTileCacheInsert(z, tx, ty, tile);
    return tile;
#else
    return fetchTileLocalRaw(z, tx, ty);
#endif
}

// Fetch the labels for the CURRENT view (set by drawMapTileBackgroundLocal) into s_labels. Local twin of
// readKeyboardLabelsForView -- identical view-cache / declutter logic, byte fetch swapped to the local FS.
static void readLocalLabelsForView()
{
    if (s_kbViewZoom < 0) { // no tile draw this render -> nothing to anchor labels to
        s_labelCount = 0;
        s_lblViewZoom = -2;
        return;
    }
    if (s_kbViewZoom == s_lblViewZoom && s_kbViewTx0 == s_lblViewTx0 && s_kbViewTx1 == s_lblViewTx1 &&
        s_kbViewTy0 == s_lblViewTy0 && s_kbViewTy1 == s_lblViewTy1)
        return; // labels already loaded for this exact view

    if (s_kbViewTx0 > s_kbViewTx1 || s_kbViewTy0 > s_kbViewTy1) { // empty/inverted rect (centre out of coverage)
        s_labelCount = 0;
        s_lblViewZoom = s_kbViewZoom;
        s_lblViewTx0 = s_kbViewTx0;
        s_lblViewTx1 = s_kbViewTx1;
        s_lblViewTy0 = s_kbViewTy0;
        s_lblViewTy1 = s_kbViewTy1;
        return;
    }

    s_labelCount = 0;
    int strPos = 0;
    fs::FS *fsrc = mapTileFS();
    // Pick the label zoom: the view zoom's own labels_<z> if present, else the nearest LOWER zoom that has
    // one. A z17 tile tier ships no labels_17 (the label idx tx/ty are u16, which overflows past z16), so a
    // z17 view draws the z16 labels -- records carry lat/lng, which project identically at any zoom; only
    // the tile-rect lookup needs the >> shift below. Absent zooms are bitmask-cached: each failed open is a
    // full FAT directory scan, and this probe runs on every view change.
    int lblZoom = -1;
    uint32_t idxSize = 0;
    char idxPath[40];
    if (fsrc) {
        for (int z = s_kbViewZoom; z >= 0 && lblZoom < 0; z--) {
            if (z < 32 && (s_labelsIdxMissing & (1u << z)))
                continue;
            snprintf(idxPath, sizeof(idxPath), "/map/labels_%d.idx", z);
            MapFSBusLock lock(mapFSOnBus());
            File f = fsrc->open(idxPath, FILE_O_READ);
            if (f) {
                idxSize = f.size();
                f.close();
                lblZoom = z; // idxPath still holds this zoom's path
            } else if (z < 32) {
                s_labelsIdxMissing |= (1u << z);
            }
        }
    }
    const int lblShift = (lblZoom >= 0) ? (s_kbViewZoom - lblZoom) : 0;
    const int lTx0 = s_kbViewTx0 >> lblShift, lTx1 = s_kbViewTx1 >> lblShift;
    const int lTy0 = s_kbViewTy0 >> lblShift, lTy1 = s_kbViewTy1 >> lblShift;
    // The idx size picks the search strategy: small -> slurp once into the shared scratch (fastest, one read);
    // larger (a whole-island bundle blows past the 8 KB scratch) -> streamed binary search per view tile.
    const bool streamed = idxSize > sizeof(s_tileCacheBuffer) && (idxSize % LABEL_IDX_REC) == 0;
    int idxCount = (lblZoom >= 0 && idxSize > 0 && !streamed) ? loadLabelIndexLocal(lblZoom) : 0;
    if (idxCount > 0 || streamed) {
        char datPath[40];
        snprintf(datPath, sizeof(datPath), "/map/labels_%d.dat", lblZoom);
        const int MAX_LABEL_TILES = 16;   // bound the per-view blob reads (mirrors the tile budget)
        const int MAX_LABEL_LOOKUPS = 64; // bound the per-view STREAMED index searches: each lookup (hit OR miss)
                                          // is an SD open + binary search, and a zoomed-out view's rect can span
                                          // thousands of tiles -- unbudgeted, one render could stall for seconds
                                          // under spiLock and starve the task watchdog
        int fetched = 0, lookups = 0;
        for (int ty = lTy0; ty <= lTy1 && s_labelCount < KB_MAX_LABELS && fetched < MAX_LABEL_TILES; ty++)
            for (int tx = lTx0; tx <= lTx1 && fetched < MAX_LABEL_TILES; tx++) {
                uint32_t off = 0, len = 0;
                bool has;
                if (streamed) {
                    if (++lookups > MAX_LABEL_LOOKUPS) {
                        ty = lTy1; // I/O budget exhausted: end the whole scan (labels drawn so far stand)
                        break;
                    }
#ifdef ARCH_ESP32
                    esp32Loop(); // each streamed lookup is real SD I/O: keep the task watchdog fed
#endif
                    has = findLabelTileStreamed(fsrc, idxPath, idxSize / LABEL_IDX_REC, (uint32_t)tx, (uint32_t)ty,
                                                off, len);
                } else {
                    has = findLabelTile(idxCount, (uint32_t)tx, (uint32_t)ty, off, len);
                }
                if (!has || len == 0)
                    continue; // no labels in this tile
                fetched++;
                int got = fetchRangeLocal(fsrc, datPath, off, len, s_compressedBuf, sizeof(s_compressedBuf));
                if (got > 0)
                    parseLabelBlob(s_compressedBuf, (uint32_t)got, strPos);
            }
    }
    // Insertion-sort the collected labels by rank so renderLabels' greedy declutter favours hospitals/shelters.
    for (int i = 1; i < s_labelCount; i++) {
        InkHUD::MapApplet::MapLabel key = s_labels[i];
        int j = i - 1;
        while (j >= 0 && s_labels[j].rank > key.rank) {
            s_labels[j + 1] = s_labels[j];
            j--;
        }
        s_labels[j + 1] = key;
    }
    s_lblViewZoom = s_kbViewZoom;
    s_lblViewTx0 = s_kbViewTx0;
    s_lblViewTx1 = s_kbViewTx1;
    s_lblViewTy0 = s_kbViewTy0;
    s_lblViewTy1 = s_kbViewTy1;
    LOG_DEBUG("InkHUD Map: view z%d tx%d..%d ty%d..%d -> %d labels", s_kbViewZoom, s_kbViewTx0, s_kbViewTx1,
              s_kbViewTy0, s_kbViewTy1, s_labelCount);
}

void InkHUD::MapApplet::drawLocalLabels()
{
    readLocalLabelsForView();
    renderLabels(s_labels, s_labelCount);
}

// Early manifest fetch -- mirror of primeKeyboardTiles, so a derived applet's onActivate() has the zoom set
// before the first render's zoom picker reads it.
void InkHUD::MapApplet::primeLocalTiles()
{
    readLocalManifestIfNeeded();
}

// Local-FS equivalent of drawMapTileBackground: pick the best available zoom <= chosenZoom (upsampling like the
// compiled path), then read + blit the visible tile range from the local bundle.
void InkHUD::MapApplet::drawMapTileBackgroundLocal(int zoom)
{
    s_kbViewZoom = -1; // invalidate the label view until we've resolved this render's tile zoom + range
    if (metersToPx <= 0.0f)
        return;

    readLocalManifestIfNeeded();
    if (s_kbZoomCount == 0)
        return; // no manifest yet (logged); nothing we can draw

    // Best available tile zoom <= chosenZoom, else the lowest available (mirrors the compiled-tile picker).
    int tileZoom = -1, covIdx = -1;
    for (int i = 0; i < s_kbZoomCount; i++)
        if (s_kbZooms[i].z <= zoom && (tileZoom < 0 || s_kbZooms[i].z > tileZoom)) {
            tileZoom = s_kbZooms[i].z;
            covIdx = i;
        }
    if (tileZoom < 0)
        for (int i = 0; i < s_kbZoomCount; i++)
            if (tileZoom < 0 || s_kbZooms[i].z < tileZoom) {
                tileZoom = s_kbZooms[i].z;
                covIdx = i;
            }
    if (tileZoom < 0)
        return;

    const float R = 6378137.0f;
    const float latRad = latCenter * DEG_TO_RAD;
    const float mpp = (2.0f * M_PI * R / (256.0f * (float)(1 << zoom))) * cosf(latRad);
    const float worldPxPerScreenPx = 1.0f / (metersToPx * mpp);
    const float tileWorldPx = worldPxPerScreenPx * ((float)(1 << tileZoom) / (float)(1 << zoom));

    const float sinLat = sinf(latRad);
    const float gpxX = ((lngCenter + 180.0f) / 360.0f) * (float)(1 << tileZoom) * 256.0f;
    const float gpxY = (0.5f - logf((1.0f + sinLat) / (1.0f - sinLat)) / (4.0f * M_PI)) * (float)(1 << tileZoom) * 256.0f;

    const float minWx = gpxX - width() * 0.5f * tileWorldPx;
    const float maxWx = gpxX + width() * 0.5f * tileWorldPx;
    const float minWy = gpxY - height() * 0.5f * tileWorldPx;
    const float maxWy = gpxY + height() * 0.5f * tileWorldPx;

    int txStart = (int)floorf(minWx / 256.0f);
    int txEnd = (int)floorf(maxWx / 256.0f);
    int tyStart = (int)floorf(minWy / 256.0f);
    int tyEnd = (int)floorf(maxWy / 256.0f);
    // Clamp to this zoom's manifest coverage so we don't waste reads on tiles that certainly don't exist.
    txStart = max(txStart, s_kbZooms[covIdx].minTx);
    txEnd = min(txEnd, s_kbZooms[covIdx].maxTx);
    tyStart = max(tyStart, s_kbZooms[covIdx].minTy);
    tyEnd = min(tyEnd, s_kbZooms[covIdx].maxTy);

    // Publish this render's view so drawLocalLabels() (called later in onRender) reads the same tiles' labels.
    s_kbViewZoom = tileZoom;
    s_kbViewTx0 = txStart;
    s_kbViewTx1 = txEnd;
    s_kbViewTy0 = tyStart;
    s_kbViewTy1 = tyEnd;

#if MOD_MAP_TILE_DEBUG
    LOG_DEBUG("InkHUD Map: local render tileZ=%d chosenZ=%d centre=%.6f,%.6f gpx=%.1f,%.1f tileWorldPx=%.4f "
              "tx %d..%d ty %d..%d",
              tileZoom, zoom, latCenter, lngCenter, gpxX, gpxY, tileWorldPx, txStart, txEnd, tyStart, tyEnd);
#endif

    // Cap the tile reads so a wide/zoomed-out view can't spin the render arbitrarily long (mirrors the keyboard
    // budget). SD reads are fast, but the bound also caps the spiLock hold count against concurrent LoRa RX.
    const int MAX_TILES_PER_RENDER = 16;
    int req = 0, drawn = 0;
    bool stop = false;
    for (int ty = tyStart; ty <= tyEnd && !stop; ty++)
        for (int tx = txStart; tx <= txEnd; tx++) {
            if (req >= MAX_TILES_PER_RENDER) {
                LOG_WARN("InkHUD Map: tile budget %d reached (view too wide); background truncated", MAX_TILES_PER_RENDER);
                stop = true;
                break;
            }
            req++;
#ifdef ARCH_ESP32
            esp32Loop(); // service the app watchdog once per tile (cheap: esp_task_wdt_reset)
#endif
            const uint8_t *tile = fetchTileLocal(tileZoom, (uint32_t)tx, (uint32_t)ty);
            if (!tile)
                continue; // absent / white
            drawn++;
            blitTile(tile, tx * 256.0f, ty * 256.0f, gpxX, gpxY, tileWorldPx);
        }
    LOG_DEBUG("InkHUD Map: local z%d tiles req=%d drawn=%d (chosenZoom=%d)", tileZoom, req, drawn, zoom);

    // Remember the view for the idle neighborhood prefetch (prefetchStep)
    prefetchZ = tileZoom;
    prefetchTxStart = txStart;
    prefetchTxEnd = txEnd;
    prefetchTyStart = tyStart;
    prefetchTyEnd = tyEnd;
}

// Warm the PSRAM tile cache around the last-rendered view. Scans, nearest ring first: the
// visible rect grown by a 2-tile pan margin (same z), the parent rect one zoom out (covers a
// zoom-out instantly - each parent tile spans 4), and the child rect one zoom in. Fetches ONE
// uncached tile per call (~5-20ms LZ4 decode) so the caller's idle tick stays responsive;
// returns false once everything is cached. All fetched tiles land in the shared LRU via
// fetchTileLocal, so render hits them with zero filesystem I/O.
bool InkHUD::MapApplet::prefetchStep()
{
#if defined(ARCH_ESP32)
    if (prefetchZ < 0 || !s_localTileCacheOk || !mapTileFS())
        return false;

    // Scan a rect at zoom z; fetch the first uncached tile. Returns true if one was fetched.
    auto scanRect = [this](int z, int txs, int txe, int tys, int tye) -> bool {
        if (z < 0 || z > 30)
            return false;
        const int32_t maxT = (1 << z) - 1;
        for (int ty = tys; ty <= tye; ty++) {
            if (ty < 0 || ty > maxT)
                continue;
            for (int tx = txs; tx <= txe; tx++) {
                if (tx < 0 || tx > maxT)
                    continue;
                if (localTileCacheFind(z, (uint32_t)tx, (uint32_t)ty))
                    continue; // already cached (tile or absent-verdict)
                fetchTileLocal(z, (uint32_t)tx, (uint32_t)ty);
                return true;
            }
        }
        return false;
    };

    // 1. Same zoom, visible rect + 2-tile pan margin
    if (scanRect(prefetchZ, prefetchTxStart - 2, prefetchTxEnd + 2, prefetchTyStart - 2, prefetchTyEnd + 2))
        return true;
    // 2. One zoom out: parent rect (+1 margin)
    if (scanRect(prefetchZ - 1, (prefetchTxStart >> 1) - 1, (prefetchTxEnd >> 1) + 1, (prefetchTyStart >> 1) - 1,
                 (prefetchTyEnd >> 1) + 1))
        return true;
    // 3. One zoom in: child rect of the visible area
    if (scanRect(prefetchZ + 1, prefetchTxStart << 1, (prefetchTxEnd << 1) + 1, prefetchTyStart << 1,
                 (prefetchTyEnd << 1) + 1))
        return true;
#endif // ARCH_ESP32
    return false;
}

#endif // MOD_LOCAL_MAP_TILES

void InkHUD::MapApplet::onRender(bool full)
{
#if defined(MOD_KEYBOARD_MAP_TILES)
    // Learn the streamed zoom set BEFORE the zoom picker below reads tileMetadataZoomCount(). Otherwise the
    // first render after boot sees zero zooms (the manifest is only fetched later, inside
    // drawMapTileBackground) and falls back to the hardcoded z13.
    readKeyboardManifestIfNeeded();
#elif defined(MOD_LOCAL_MAP_TILES)
    readLocalManifestIfNeeded();
#endif

    // Map center is always the node centroid - tiles are background only.
    getMapCenter(&latCenter, &lngCenter);
    if (shouldPlotNodeMarkers())
        calculateAllMarkers();
    else
        markers.clear(); // toggle off -> no markers drawn (the loop below iterates an empty list)

    // Show placeholder only if we have no position at all - no tiles, no own node
    if (!enoughMarkers() && !centerIsOurNode) {
        printAt(X(0.5), Y(0.5) - (getFont().lineHeight() / 2), "Node positions", CENTER, MIDDLE);
        printAt(X(0.5), Y(0.5) + (getFont().lineHeight() / 2), "will appear here", CENTER, MIDDLE);
        return;
    }

    // Determine the metersToPx needed to fit all nodes on screen.
    getMapSize(&widthMeters, &heightMeters);
    calculateMapScale(); // metersToPx = fit-all-nodes scale
    const float metersToPxFit = metersToPx;

    // Pick the highest zoom whose native scale fits all nodes (no downsampling, no dither noise).
    {
        const float R = 6378137.0f;
        const float latRad = latCenter * DEG_TO_RAD;

        // Collect unique zooms, sort descending (highest detail first)
        int zooms[16] = {};
        int nzooms = 0;
        for (int i = 0; i < tileMetadataZoomCount() && nzooms < 16; i++) {
            bool found = false;
            for (int j = 0; j < nzooms; j++) {
                if (zooms[j] == tileMetadataZoomAt(i)) {
                    found = true;
                    break;
                }
            }
            if (!found)
                zooms[nzooms++] = tileMetadataZoomAt(i);
        }
        for (int i = 0; i < nzooms - 1; i++) {
            for (int j = i + 1; j < nzooms; j++) {
                if (zooms[j] > zooms[i]) {
                    int t = zooms[i];
                    zooms[i] = zooms[j];
                    zooms[j] = t;
                }
            }
        }

        int chosenZoom = (nzooms > 0) ? zooms[nzooms - 1] : 13; // fallback: widest zoom
        float chosenMetersToPx = metersToPxFit;                 // fallback: fit-scale (may downsample)

        if (s_zoomLocked && s_lockedZoom >= 0) {
            // Use locked zoom at native 1:1 scale - never zoom out for new nodes
            chosenZoom = s_lockedZoom;
            float mpp = (2.0f * M_PI * R / (256.0f * (float)(1 << chosenZoom))) * cosf(latRad);
            chosenMetersToPx = 1.0f / mpp;
        } else if ((markers.empty() || metersToPxFit <= 0.0f) && nzooms > 0) {
            // No spread to fit (own node only, or single remote node at map center).
#if defined(MOD_KEYBOARD_MAP_TILES) || defined(MOD_LOCAL_MAP_TILES)
            chosenZoom = zooms[nzooms - 1]; // streamed tiles: open on the widest (lowest) zoom = whole-coverage overview
#else
            chosenZoom = zooms[0]; // highest zoom at native scale
#endif
            float mpp = (2.0f * M_PI * R / (256.0f * (float)(1 << chosenZoom))) * cosf(latRad);
            chosenMetersToPx = 1.0f / mpp;
        } else {
            for (int zi = 0; zi < nzooms; zi++) {
                float mpp = (2.0f * M_PI * R / (256.0f * (float)(1 << zooms[zi]))) * cosf(latRad);
                float nativeMetersToPx = 1.0f / mpp;
                if (nativeMetersToPx <= metersToPxFit) {
                    // This zoom at native scale shows all nodes - use it (highest detail that fits)
                    chosenZoom = zooms[zi];
                    chosenMetersToPx = nativeMetersToPx;
                    break;
                }
            }
        }

#if defined(MOD_KEYBOARD_MAP_TILES) || defined(MOD_LOCAL_MAP_TILES)
        // Never hand the tile draw a centre outside the chosen zoom's coverage: its range clamp would invert
        // (empty tile loop); on the keyboard transport the label pass then blocks on an unsynchronised ~5 KB
        // UART read -> watchdog reboot, and on the local (SD) transport it draws a blank un-re-homed map -
        // which is what a GPS trace recentring outside the bundle looked like. Drop to the highest zoom that
        // covers the centre; its wider native scale also keeps far node markers inside int16. When locked,
        // re-home the lock to that zoom too -- currentRenderZoom() (hence the NavMap pan-step and label LOD)
        // must equal the zoom actually drawn, so a jump outside coverage simply re-homes the lock to the best
        // zoom available there. (Centre outside EVERY zoom: leave chosenZoom -- the label pass skips the read.)
        if (nzooms > 0 && !kbZoomCoversCentre(chosenZoom, latCenter, lngCenter)) {
            int cov = kbHighestZoomCoveringCentre(latCenter, lngCenter);
            if (cov >= 0) {
                chosenZoom = cov;
                if (s_zoomLocked)
                    s_lockedZoom = cov;
                float mpp = (2.0f * M_PI * R / (256.0f * (float)(1 << chosenZoom))) * cosf(latRad);
                chosenMetersToPx = 1.0f / mpp;
            }
        }
#endif

        if (!s_zoomLocked)
            s_autoFitZoom = chosenZoom;
        metersToPx = chosenMetersToPx;
        s_lastRenderedZoom = chosenZoom;
        drawMapTileBackground(chosenZoom);

        char zoomLabel[8];
        snprintf(zoomLabel, sizeof(zoomLabel), "z%d", chosenZoom);
        int16_t zoomLabelW = getTextWidth(zoomLabel);
        int16_t zoomLabelH = getFont().lineHeight();
        int16_t zoomLabelX = width() - zoomLabelW - 3;
        int16_t zoomLabelY = 2;
        fillRect(zoomLabelX - 2, zoomLabelY - 1, zoomLabelW + 4, zoomLabelH + 2, WHITE);
        printAt(zoomLabelX, zoomLabelY, zoomLabel, LEFT, TOP);
    }

    // Helper: draw rounded rectangle centered at x,y
    auto fillRoundedRect = [&](int16_t cx, int16_t cy, int16_t w, int16_t h, int16_t r, uint16_t color) {
        int16_t x = cx - (w / 2);
        int16_t y = cy - (h / 2);

        // center rects
        fillRect(x + r, y, w - 2 * r, h, color);
        fillRect(x, y + r, r, h - 2 * r, color);
        fillRect(x + w - r, y + r, r, h - 2 * r, color);

        // corners
        fillCircle(x + r, y + r, r, color);
        fillCircle(x + w - r - 1, y + r, r, color);
        fillCircle(x + r, y + h - r - 1, r, color);
        fillCircle(x + w - r - 1, y + h - r - 1, r, color);
    };

    // Draw all markers first
    for (Marker m : markers) {
        // Cull in float before the int16 cast: at high zoom a node tens of km off-screen projects past 32767,
        // and the float->int16 conversion wraps (UB) to a bogus on-screen position -- a phantom marker on a nav map.
        const float fx = X(0.5) + m.eastMeters * metersToPx;
        const float fy = Y(0.5) - m.northMeters * metersToPx;
        if (fx < -100.0f || fx > width() + 100.0f || fy < -100.0f || fy > height() + 100.0f)
            continue;
        int16_t x = (int16_t)fx;
        int16_t y = (int16_t)fy;

        // What goes in the label box: the node's short name (if this applet shows names and we have one),
        // else the hop indicator -- "?" for unknown hops, "X" beyond the hop limit, otherwise the hop digit.
        char hopStr[4];
        const char *text;
        if (markerShowsName() && m.label[0]) {
            text = m.label;
        } else if (m.hopsAway == HOPS_UNKNOWN) {
            text = "?"; // position known, hop count unknown (check before the hop-limit test: 0xFF > limit)
        } else if (m.hopsAway > config.lora.hop_limit) {
            text = "X";
        } else {
            snprintf(hopStr, sizeof(hopStr), "%d", m.hopsAway);
            text = hopStr;
        }

        // Box sized to fit the text. NOTE: fillRoundedRect takes the box CENTRE (not a top-left), so both the
        // outline and the fill are drawn CONCENTRIC at the node point (x,y) -- the outline 1px larger all round --
        // and the text is centred at the same (x,y). (Using top-left semantics here gave a lopsided border and
        // pushed the text off-centre.)
        setFont(fontSmall);
        int16_t boxH = fontSmall.lineHeight() + 2;
        int16_t boxW = getTextWidth(text) + 4;
        int16_t radius = max(2, boxH / 6);
        const bool inv = markerInverted();
        fillRoundedRect(x, y, boxW + 2, boxH + 2, radius + 1, inv ? BLACK : WHITE); // outline
        fillRoundedRect(x, y, boxW, boxH, radius, inv ? WHITE : BLACK);             // fill (concentric)
        setTextColor(inv ? BLACK : WHITE);
        printAt(x, y, text, CENTER, MIDDLE); // centred on the node point = box centre
        setTextColor(BLACK);
    }

    // Text-label overlay (POI/place/region names). Base does nothing; NavMap draws its labels here, on top of
    // the tiles + node markers. Uses the current mid-render projection state (metersToPx etc.).
    drawMapLabels();

    // Dual map scale bars
    if (metersToPx <= 0.0f)
        return;
    int16_t horizPx = width() * 0.25f;
    int16_t vertPx = height() * 0.25f;
    float horizMeters = horizPx / metersToPx;
    float vertMeters = vertPx / metersToPx;

    auto formatDistance = [&](float meters, char *out, size_t len) {
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
            float feet = meters * 3.28084f;
            if (feet < 528)
                snprintf(out, len, "%.0f ft", feet);
            else {
                float miles = feet / 5280.0f;
                snprintf(out, len, miles < 10 ? "%.1f mi" : "%.0f mi", miles);
            }
        } else {
            if (meters >= 1000)
                snprintf(out, len, "%.1f km", meters / 1000.0f);
            else
                snprintf(out, len, "%.0f m", meters);
        }
    };

    // Bottom edge -- the horizontal bar sits on it, and the vertical bar anchors its bottom here too.
    int16_t horizBarY = height() - 2;

    // Horizontal scale bar (along the bottom edge). A derived applet can hide it -- NavMap does, because its
    // bottom-centre coordinate readout would overlap it.
    if (showHorizontalScaleBar()) {
        int16_t horizBarX = 1;
        drawLine(horizBarX, horizBarY, horizBarX + horizPx, horizBarY, BLACK);
        drawLine(horizBarX, horizBarY - 3, horizBarX, horizBarY + 3, BLACK);
        drawLine(horizBarX + horizPx, horizBarY - 3, horizBarX + horizPx, horizBarY + 3, BLACK);

        char horizLabel[32];
        formatDistance(horizMeters, horizLabel, sizeof(horizLabel));
        int16_t horizLabelX = horizBarX + horizPx + 4;
        printAtHalo(horizLabelX, horizBarY, horizLabel, LEFT, BOTTOM);
    }

    // Vertical scale bar
    int16_t vertBarX = 1;
    int16_t vertBarBottom = horizBarY;
    int16_t vertBarTop = vertBarBottom - vertPx;
    drawLine(vertBarX, vertBarBottom, vertBarX, vertBarTop, BLACK);
    drawLine(vertBarX - 3, vertBarBottom, vertBarX + 3, vertBarBottom, BLACK);
    drawLine(vertBarX - 3, vertBarTop, vertBarX + 3, vertBarTop, BLACK);

    char vertTopLabel[32];
    formatDistance(vertMeters, vertTopLabel, sizeof(vertTopLabel));
    int16_t topLabelY = vertBarTop - getFont().lineHeight() - 2;
    int16_t topLabelW = getTextWidth(vertTopLabel);
    int16_t topLabelH = getFont().lineHeight();
    printAtHalo(vertBarX + (topLabelW / 2) + 1, topLabelY + (topLabelH / 2), vertTopLabel, CENTER, MIDDLE);

    char vertBottomLabel[32];
    formatDistance(vertMeters, vertBottomLabel, sizeof(vertBottomLabel));
    int16_t bottomLabelY = vertBarBottom + 4;
    int16_t bottomLabelW = getTextWidth(vertBottomLabel);
    int16_t bottomLabelH = getFont().lineHeight();
    printAtHalo(vertBarX + (bottomLabelW / 2) + 1, bottomLabelY + (bottomLabelH / 2), vertBottomLabel, CENTER, MIDDLE);

    // Draw our node LAST with full white fill + outline
    if (centerIsOurNode) {
        const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
        meshtastic_PositionLite ourSelfPos;
        nodeDB->copyNodePosition(ourNode->num, ourSelfPos);
        Marker self = calculateMarker(ourSelfPos.latitude_i * 1e-7, ourSelfPos.longitude_i * 1e-7, 0);
        int16_t centerX = X(0.5) + (self.eastMeters * metersToPx);
        int16_t centerY = Y(0.5) - (self.northMeters * metersToPx);

        int16_t r = fontSmall.lineHeight() / 2; // scale marker with font

        // White fill background + halo
        fillCircle(centerX, centerY, r + 2, WHITE);
        drawCircle(centerX, centerY, r + 2, WHITE);

        // Black bullseye on top
        drawCircle(centerX, centerY, r, BLACK);
        fillCircle(centerX, centerY, max(2, r / 4), BLACK);

        // Crosshairs
        drawLine(centerX - r - 2, centerY, centerX + r + 2, centerY, BLACK);
        drawLine(centerX, centerY - r - 2, centerX, centerY + r + 2, BLACK);
    }
}

// Find the center point, in the middle of all node positions
// Calculated values are written to the *lat and *long pointer args
// - Finds the "mean lat long"
// - Calculates furthest nodes from "mean lat long"
// - Place map center directly between these furthest nodes

// Centre of the loaded tile coverage: bounding box of the lowest-zoom tiles, inverse-projected from
// Web Mercator tile coords back to lat/lng. Returns false when no tiles are compiled in.
bool InkHUD::MapApplet::getTileCenter(float *lat, float *lng)
{
#if defined(MOD_KEYBOARD_MAP_TILES) || defined(MOD_LOCAL_MAP_TILES)
    // Streamed tile source: centre on the widest (lowest-zoom) MANIFEST coverage rectangle so the GPS-less map
    // opens over the actual tiles. (The compiled MapTile.h is irrelevant in these builds.)
#if defined(MOD_KEYBOARD_MAP_TILES)
    readKeyboardManifestIfNeeded();
#else
    readLocalManifestIfNeeded();
#endif
    if (s_kbZoomCount == 0)
        return false; // manifest not loaded yet -> caller retries next render
    int li = 0;
    for (int i = 1; i < s_kbZoomCount; i++)
        if (s_kbZooms[i].z < s_kbZooms[li].z)
            li = i;
    const int z = s_kbZooms[li].z;
    const int minTx = s_kbZooms[li].minTx, maxTx = s_kbZooms[li].maxTx;
    const int minTy = s_kbZooms[li].minTy, maxTy = s_kbZooms[li].maxTy;
#else
    if (map_tile_count == 0 || tileMetadataZoomCount() == 0)
        return false;

    // Lowest available zoom covers the widest ground area.
    int z = tileMetadataZoomAt(0);
    for (int i = 1; i < tileMetadataZoomCount(); i++) {
        const int zi = tileMetadataZoomAt(i);
        if (zi < z)
            z = zi;
    }

    // Bounding box of the tiles at that zoom (tile x/y are integer tile indices).
    int minTx = 0x7fffffff, maxTx = -1, minTy = 0x7fffffff, maxTy = -1;
    for (int i = 0; i < map_tile_count; i++) {
        if (tileZoomAt(i) != z)
            continue;
        const int tx = tileTxAt(i);
        const int ty = tileTyAt(i);
        if (tx < minTx)
            minTx = tx;
        if (tx > maxTx)
            maxTx = tx;
        if (ty < minTy)
            minTy = ty;
        if (ty > maxTy)
            maxTy = ty;
    }
    if (maxTx < 0)
        return false;
#endif

    // Centre of the covered rectangle (a tile spans [t, t+1) in tile units), inverse Web Mercator.
    const double n = (double)(1 << z);
    const double cx = (minTx + maxTx + 1) / 2.0;
    const double cy = (minTy + maxTy + 1) / 2.0;
    *lng = (float)(cx / n * 360.0 - 180.0);
    const double m = M_PI * (1.0 - 2.0 * cy / n);
    *lat = (float)(atan(sinh(m)) * 180.0 / M_PI);
    return true;
}

void InkHUD::MapApplet::getMapCenter(float *lat, float *lng)
{
    // If we have a valid position for our own node, use that as the anchor
    const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    meshtastic_PositionLite ourSelfPos;
    if (ourNode && nodeDB->hasValidPosition(ourNode) && nodeDB->copyNodePosition(ourNode->num, ourSelfPos)) {
        *lat = ourSelfPos.latitude_i * 1e-7;
        *lng = ourSelfPos.longitude_i * 1e-7;
        centerIsOurNode = true;
    } else {
        centerIsOurNode = false;
        // Find mean lat long coords
        // ============================
        // - assigning X, Y and Z values to position on Earth's surface in 3D space, relative to center of planet
        // - averages the x, y and z coords
        // - uses tan to find angles for lat / long degrees
        //   - longitude: triangle formed by x and y (on plane of the equator)
        //   - latitude: triangle formed by z (north south),
        //     and the line along plane of equator which stretches from earth's axis to where point xyz intersects planet's
        //     surface

        // Working totals, averaged after nodeDB processed
        uint32_t positionCount = 0;
        float xAvg = 0;
        float yAvg = 0;
        float zAvg = 0;

        // For each node in db
        for (uint32_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
            meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);

            // Skip if no position
            if (!nodeDB->hasValidPosition(node))
                continue;

            // Skip if derived applet doesn't want to show this node on the map
            if (!shouldDrawNode(node))
                continue;

            meshtastic_PositionLite pos;
            if (!nodeDB->copyNodePosition(node->num, pos))
                continue;

            // Latitude and Longitude of node, in radians
            float latRad = pos.latitude_i * (1e-7) * DEG_TO_RAD;
            float lngRad = pos.longitude_i * (1e-7) * DEG_TO_RAD;

            // Convert to cartesian points, with center of earth at 0, 0, 0
            // Exact distance from center is irrelevant, as we're only interested in the vector
            float x = cos(latRad) * cos(lngRad);
            float y = cos(latRad) * sin(lngRad);
            float z = sin(latRad);

            // To find mean values shortly
            xAvg += x;
            yAvg += y;
            zAvg += z;
            positionCount++;
        }

        // All NodeDB processed, find mean values
        if (positionCount == 0)
            return;
        xAvg /= positionCount;
        yAvg /= positionCount;
        zAvg /= positionCount;

        // Longitude from cartesian coords
        // (Angle from 3D coords describing a point of globe's surface)
        /*
                          UK
                       /-------\
        (Top View)   /-         -\
                   /-      (You)  -\
                 /-           .     -\
               /-             . X     -\
         Asia -             ...         - USA
               \-           Y         -/
                 \-                 -/
                   \-             -/
                     \-         -/
                       \- -----/
                       Pacific

        */

        *lng = atan2(yAvg, xAvg) * RAD_TO_DEG;

        // Latitude from cartesian coords
        // (Angle from 3D coords describing a point on the globe's surface)
        // As latitude increases, distance from the Earth's north-south axis out to our surface point decreases.
        // Means we need to first find the hypotenuse which becomes base of our triangle in the second step
        /*
                           UK                                         North
                        /-------\                 (Front View)      /-------\
         (Top View)   /-         -\                               /-         -\
                    /-       (You) -\                           /-(You)        -\
                  /-         /.      -\                       /-   .             -\
                /-    √X²+Y²/ . X      -\                   /-   Z .               -\
        Asia   -           /...          - USA             -       .....             -
                \-           Y         -/                   \-     √X²+Y²          -/
                  \-                 -/                       \-                 -/
                    \-             -/                           \-             -/
                      \-         -/                               \-         -/
                        \- -----/                                   \- -----/
                         Pacific                                      South
        */

        float hypotenuse = sqrt((xAvg * xAvg) + (yAvg * yAvg)); // Distance from globe's north-south axis to surface intersect
        *lat = atan2(zAvg, hypotenuse) * RAD_TO_DEG;
    }

    // Use either our node position, or the mean fallback as the center
    latCenter = *lat;
    lngCenter = *lng;

    // When zoom is locked, keep center exactly on own node / zero-hop centroid.
    // Skip bounding-box shift so new distant nodes don't move the zoomed view.
    if (s_zoomLocked) {
        // Own node has no position - re-center on zero-hop centroid instead.
        if (!centerIsOurNode) {
            uint32_t count = 0;
            float xAvg = 0, yAvg = 0, zAvg = 0;
            for (uint32_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
                meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
                if (!nodeDB->hasValidPosition(node) || !shouldDrawNode(node))
                    continue;
                if (!node->has_hops_away || node->hops_away != 0)
                    continue;
                meshtastic_PositionLite pos;
                if (!nodeDB->copyNodePosition(node->num, pos))
                    continue;
                float latRad2 = pos.latitude_i * 1e-7 * DEG_TO_RAD;
                float lngRad2 = pos.longitude_i * 1e-7 * DEG_TO_RAD;
                xAvg += cosf(latRad2) * cosf(lngRad2);
                yAvg += cosf(latRad2) * sinf(lngRad2);
                zAvg += sinf(latRad2);
                count++;
            }
            if (count > 0) {
                xAvg /= count;
                yAvg /= count;
                zAvg /= count;
                *lng = atan2f(yAvg, xAvg) * RAD_TO_DEG;
                *lat = atan2f(zAvg, sqrtf(xAvg * xAvg + yAvg * yAvg)) * RAD_TO_DEG;
                latCenter = *lat;
                lngCenter = *lng;
            }
        }
        return; // Do not shift center based on bounding box
    }

    // Find furthest nodes from our center, shift center to midpoint of bounding box
    float northernmost = latCenter;
    float southernmost = latCenter;
    float easternmost = lngCenter;
    float westernmost = lngCenter;

    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);

        if (!nodeDB->hasValidPosition(node))
            continue;
        if (!shouldDrawNode(node))
            continue;

        meshtastic_PositionLite pos;
        if (!nodeDB->copyNodePosition(node->num, pos))
            continue;

        float latNode = pos.latitude_i * 1e-7;
        float lngNode = pos.longitude_i * 1e-7;

        northernmost = max(northernmost, latNode);
        southernmost = min(southernmost, latNode);

        float degEastward = fmod(((lngNode - lngCenter) + 360), 360);      // Degrees east from center to node
        float degWestward = abs(fmod(((lngNode - lngCenter) - 360), 360)); // Degrees west from center to node
        if (degEastward < degWestward)
            easternmost = max(easternmost, lngCenter + degEastward);
        else
            westernmost = min(westernmost, lngCenter - degWestward);
    }

    // Todo: check for issues with map spans >180 deg. MQTT only..
    latCenter = (northernmost + southernmost) / 2;
    lngCenter = (westernmost + easternmost) / 2;

    // In case our new center is west of -180, or east of +180, for some reason
    lngCenter = fmod(lngCenter, 180);
}

// Size of map in meters
// Grown to fit the nodes furthest from map center
// Overridable if derived applet wants a custom map size (fixed size?)
void InkHUD::MapApplet::getMapSize(uint32_t *widthMeters, uint32_t *heightMeters)
{
    // Reset the value
    *widthMeters = 0;
    *heightMeters = 0;

    // Find the greatest distance horizontally and vertically from map center
    for (Marker m : markers) {
        *widthMeters = max(*widthMeters, (uint32_t)abs(m.eastMeters) * 2);
        *heightMeters = max(*heightMeters, (uint32_t)abs(m.northMeters) * 2);
    }

    // Add padding
    *widthMeters *= 1.1;
    *heightMeters *= 1.1;
}

// Convert and store info we need for drawing a marker
// Lat / long to "meters relative to map center", for position on screen
// Info about hopsAway, for marker size
InkHUD::MapApplet::Marker InkHUD::MapApplet::calculateMarker(float lat, float lng, uint8_t hopsAway)
{
    assert(lat != 0 || lng != 0); // Not null island. Applets should check this before calling.

    // Bearing and distance from map center to node
    float distanceFromCenter = GeoCoord::latLongToMeter(latCenter, lngCenter, lat, lng);
    float bearingFromCenter = GeoCoord::bearing(latCenter, lngCenter, lat, lng); // in radians

    // Split into meters north and meters east components (signed)
    // - signedness of cos / sin automatically sets negative if south or west
    float northMeters = cos(bearingFromCenter) * distanceFromCenter;
    float eastMeters = sin(bearingFromCenter) * distanceFromCenter;

    Marker m;
    m.eastMeters = eastMeters;
    m.northMeters = northMeters;
    m.hopsAway = hopsAway;
    return m;
}
// Draw a marker on the map for a node, with a shortname label, and backing box
void InkHUD::MapApplet::drawLabeledMarker(meshtastic_NodeInfoLite *node)
{
    // Find x and y position based on node's position in nodeDB
    assert(nodeDB->hasValidPosition(node));
    meshtastic_PositionLite pos;
    const bool hasPos = nodeDB->copyNodePosition(node->num, pos);
    assert(hasPos);
    Marker m = calculateMarker(pos.latitude_i * 1e-7, pos.longitude_i * 1e-7, node->hops_away);

    // Convert to pixel coords
    int16_t markerX = X(0.5) + (m.eastMeters * metersToPx);
    int16_t markerY = Y(0.5) - (m.northMeters * metersToPx);

    constexpr uint16_t paddingH = 2;
    constexpr uint16_t paddingW = 4;
    uint16_t paddingInnerW = 2;                      // Zero'd out if no text
    uint16_t markerSizeMax = fontSmall.lineHeight(); // Scale cross with font
    uint16_t markerSizeMin = max(5, fontSmall.lineHeight() / 3);

    int16_t textX;
    int16_t textY;
    uint16_t textW;
    uint16_t textH;
    int16_t labelX;
    int16_t labelY;
    uint16_t labelW;
    uint16_t labelH;
    uint8_t markerSize;

    bool tooManyHops = node->hops_away > config.lora.hop_limit;

    // Parse any non-ascii chars in the short name,
    // and use last 4 instead if unknown / can't render
    std::string shortName = parseShortName(node);

    // We will draw a left or right hand variant, to place text towards screen center
    // Hopefully avoid text spilling off screen
    // Most values are the same, regardless of left-right handedness

    // Pick emblem style
    if (tooManyHops)
        markerSize = getTextWidth("!");
    else
        markerSize = map(node->hops_away, 0, config.lora.hop_limit, markerSizeMax, markerSizeMin);

    // Common dimensions (left or right variant)
    textW = getTextWidth(shortName);
    if (textW == 0)
        paddingInnerW = 0; // If no text, no padding for text
    textH = fontSmall.lineHeight();
    labelH = paddingH + max((int16_t)(textH), (int16_t)markerSize) + paddingH;
    labelY = markerY - (labelH / 2);
    textY = markerY;
    labelW = paddingW + markerSize + paddingInnerW + textW + paddingW; // Width is same whether right or left hand variant

    // Left-side variant
    if (markerX < width() / 2) {
        labelX = markerX - (markerSize / 2) - paddingW;
        textX = labelX + paddingW + markerSize + paddingInnerW;
    }

    // Right-side variant
    else {
        labelX = markerX - (markerSize / 2) - paddingInnerW - textW - paddingW;
        textX = labelX + paddingW;
    }

    // Prevent overlap with scale bars and their labels
    // Define a "safe zone" in the bottom-left where the scale bars and text are drawn
    constexpr int16_t safeZoneHeight = 28; // adjust based on your label font height
    constexpr int16_t safeZoneWidth = 60;  // adjust based on horizontal label width zone
    bool overlapsScale = (labelY + labelH > height() - safeZoneHeight) && (labelX < safeZoneWidth);

    // If it overlaps, shift label upward slightly above the safe zone
    if (overlapsScale) {
        labelY = height() - safeZoneHeight - labelH - 2;
        textY = labelY + (labelH / 2);
    }

    // Backing box
    fillRect(labelX, labelY, labelW, labelH, WHITE);
    drawRect(labelX, labelY, labelW, labelH, BLACK);

    // Short name
    printAt(textX, textY, shortName, LEFT, MIDDLE);

    // If the label is for our own node,
    // fade it by overdrawing partially with white
    if (node == nodeDB->getMeshNode(nodeDB->getNodeNum()))
        hatchRegion(labelX, labelY, labelW, labelH, 2, WHITE);

    // Draw the marker emblem
    // - after the fading, because hatching (own node) can align with cross and make it look weird
    if (tooManyHops)
        printAt(markerX, markerY, "!", CENTER, MIDDLE);
    else
        drawCross(markerX, markerY, markerSize);
}

// Check if we actually have enough nodes which would be shown on the map
bool InkHUD::MapApplet::enoughMarkers()
{
    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (nodeDB->hasValidPosition(node) && shouldDrawNode(node))
            return true;
    }
    return false;
}

// Calculate how far north and east of map center each node is
// Derived applets can control which nodes to calculate (and later, draw) by overriding MapApplet::shouldDrawNode
void InkHUD::MapApplet::calculateAllMarkers()
{
    // Clear old markers
    markers.clear();

    // For each node in db
    for (uint32_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);

        // Skip if no position
        if (!nodeDB->hasValidPosition(node))
            continue;

        // Skip if derived applet doesn't want to show this node on the map
        if (!shouldDrawNode(node))
            continue;

        // Skip if our own node
        // - special handling in render()
        if (node->num == nodeDB->getNodeNum())
            continue;

        // Skip nodes with unknown hop count - partial info, not useful to plot -- unless the derived applet
        // opts to plot every positioned node (NavMap), in which case they get a "?" marker.
        if (requireHopCount() && !node->has_hops_away)
            continue;

        meshtastic_PositionLite pos;
        if (!nodeDB->copyNodePosition(node->num, pos))
            continue;

        uint8_t hops = node->has_hops_away ? node->hops_away : HOPS_UNKNOWN;
        Marker mk = calculateMarker(pos.latitude_i * 1e-7, pos.longitude_i * 1e-7, hops);
        if (nodeInfoLiteHasUser(node) && node->short_name[0]) {
            strncpy(mk.label, node->short_name, 4); // NodeInfoLite has short_name inline; <=4 chars, field zero-init'd
            mk.label[4] = 0;
        }
        markers.push_back(mk);
    }
}

void InkHUD::MapApplet::calculateMapScale()
{
    if (widthMeters == 0 || heightMeters == 0) {
        metersToPx = 0;
        return;
    }
    float mapAspectRatio = (float)widthMeters / heightMeters;
    float appletAspectRatio = (float)width() / height();
    if (mapAspectRatio > appletAspectRatio)
        metersToPx = (float)width() / widthMeters;
    else
        metersToPx = (float)height() / heightMeters;
}

// Draw an x, centered on a specific point
// Most markers will draw with this method
void InkHUD::MapApplet::drawCross(int16_t x, int16_t y, uint8_t size)
{
    int16_t x0 = x - (size / 2);
    int16_t y0 = y - (size / 2);
    int16_t x1 = x0 + size - 1;
    int16_t y1 = y0 + size - 1;
    drawLine(x0, y0, x1, y1, BLACK);
    drawLine(x0, y1, x1, y0, BLACK);
}

// See header. White outline first (8 offsets), then black text on top -- keeps labels readable over map tiles.
void InkHUD::MapApplet::printAtHalo(int16_t x, int16_t y, const char *text, HorizontalAlignment ha, VerticalAlignment va)
{
    setTextColor(WHITE);
    for (int8_t dx = -1; dx <= 1; dx++)
        for (int8_t dy = -1; dy <= 1; dy++)
            if (dx || dy)
                printAt(x + dx, y + dy, text, ha, va);
    setTextColor(BLACK);
    printAt(x, y, text, ha, va);
}

// Emergency facilities render as an icon only -- their names use characters outside the baked font corpus and a
// glyph is more glanceable on a tiny e-ink screen. Places / POIs / road names render their text.
bool InkHUD::MapApplet::categoryHasText(uint8_t category)
{
    switch (category) {
    case LABEL_HOSPITAL:
    case LABEL_SHELTER:
    case LABEL_POLICE:
    case LABEL_FIRE:
        return false;
    default:
        return true; // LABEL_PLACE, LABEL_POI, LABEL_ROAD
    }
}

// Draw a label's on-map anchor centred on (cx,cy): a small dot for text categories, or an 11x11 category glyph
// (over a white backing so it reads on any tile) for the icon-only emergency categories.
void InkHUD::MapApplet::drawLabelAnchor(int16_t cx, int16_t cy, uint8_t category)
{
    if (categoryHasText(category)) {
        fillRect(cx - 2, cy - 2, 5, 5, WHITE); // dot halo, so the anchor shows on any background
        fillRect(cx - 1, cy - 1, 3, 3, BLACK); // anchor dot at the exact point
        return;
    }
    fillRect(cx - 5, cy - 5, 11, 11, WHITE); // white backing for the glyph
    switch (category) {
    case LABEL_HOSPITAL: // bold medical cross
        fillRect(cx - 4, cy - 1, 9, 3, BLACK);
        fillRect(cx - 1, cy - 4, 3, 9, BLACK);
        break;
    case LABEL_SHELTER: // house: triangle roof over a square body
        fillTriangle(cx, cy - 5, cx - 5, cy, cx + 5, cy, BLACK);
        fillRect(cx - 3, cy, 7, 5, BLACK);
        break;
    case LABEL_POLICE: // shield: square shoulders tapering to a point
        fillRect(cx - 4, cy - 4, 9, 5, BLACK);
        fillTriangle(cx - 4, cy + 1, cx + 4, cy + 1, cx, cy + 5, BLACK);
        break;
    case LABEL_FIRE: // solid upward triangle
        fillTriangle(cx, cy - 5, cx - 5, cy + 5, cx + 5, cy + 5, BLACK);
        break;
    default:
        fillRect(cx - 1, cy - 1, 3, 3, BLACK);
        break;
    }
}

// Render a set of map labels: LOD-filter by minZoom, gate by category toggle, project each to screen (same as
// node markers), greedy-declutter (place a label only if its box is clear of ones already placed -- the array is
// expected in importance order), and draw the category icon and/or the haloed name. Called via drawMapLabels().
void InkHUD::MapApplet::renderLabels(const MapLabel *labels, int count)
{
    if (metersToPx <= 0.0f)
        return;
    const int zoom = currentRenderZoom();
    setFont(fontSmall);
    const int16_t th = fontSmall.lineHeight();

    constexpr int MAX_PLACED = 24; // more than the screen can usefully hold; O(N*placed) is trivial
    struct Box {
        int16_t x, y, w, h;
    } placed[MAX_PLACED];
    int nPlaced = 0;

    for (int i = 0; i < count && nPlaced < MAX_PLACED; i++) {
        const MapLabel &L = labels[i];
        if (zoom >= 0 && L.minZoom > zoom)
            continue; // level-of-detail: not shown at this zoom yet
        if (L.lat == 0.0f && L.lng == 0.0f)
            continue;
        if (!categoryEnabled(L.category))
            continue; // hidden behind its user toggle (e.g. showShelters off)

        Marker m = calculateMarker(L.lat, L.lng, 0); // reuse the node-marker projection
        int16_t px = X(0.5) + (int16_t)(m.eastMeters * metersToPx);
        int16_t py = Y(0.5) - (int16_t)(m.northMeters * metersToPx);

        const bool hasText = categoryHasText(L.category) && L.text && L.text[0];
        int16_t bx, by, bw, bh; // declutter box: text box for text labels, 11x11 glyph footprint for icons
        if (hasText) {
            bw = getTextWidth(L.text);
            bh = th;
            bx = px - bw / 2;
            by = py - 3 - th; // text sits just above the anchor dot (BOTTOM-aligned at py-3)
        } else {
            bw = 11;
            bh = 11;
            bx = px - 5;
            by = py - 5; // icon centred on the point
        }
        if (bx + bw < 0 || bx > width() || by + bh < 0 || by > height())
            continue; // fully off-screen

        bool clash = false; // greedy declutter against already-placed boxes
        for (int j = 0; j < nPlaced; j++) {
            const Box &b = placed[j];
            if (bx < b.x + b.w && bx + bw > b.x && by < b.y + b.h && by + bh > b.y) {
                clash = true;
                break;
            }
        }
        if (clash)
            continue;
        placed[nPlaced++] = {bx, by, bw, bh};

        drawLabelAnchor(px, py, L.category);
        if (hasText)
            printAtHalo(px, py - 3, L.text, CENTER, BOTTOM);
    }
}

#endif
