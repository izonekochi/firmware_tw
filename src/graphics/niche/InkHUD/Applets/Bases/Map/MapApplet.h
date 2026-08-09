#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Base class for Applets which show nodes on a map

Plots position of for a selection of nodes, with north facing up.
Size of cross represents hops away.
Our own node is identified with a faded label.

The base applet doesn't handle any events; this is left to the derived applets.

*/

#pragma once

#include "configuration.h"
#include <list>

#include "graphics/niche/InkHUD/Applet.h"

#include "GPSStatus.h"
#include "MeshModule.h"
#include "Observer.h"
#include "gps/GeoCoord.h"

namespace NicheGraphics::InkHUD
{

class MapApplet : public Applet
{
  public:
    MapApplet();
    void onRender(bool full) override;

    MapApplet *asMapApplet() override { return this; } // Identify as MapApplet without RTTI

    // Zoom lock - shared across all MapApplet instances (static)
    static constexpr int ZOOM_MAX_NO_TILES = 16;

    void zoomIn();
    void zoomOut();
    void resetZoom();

    // GPS locate: force a fresh GPS fix and recenter the map on it when it lands. Base applets don't
    // support it (returns false); NavMap overrides with the full search-babysitting implementation.
    virtual bool startGpsLocate() { return false; }
    virtual bool toggleGpsTrace() { return false; }          // NavMap: continuous private follow mode
    virtual bool isGpsTracing() { return false; }            // NavMap: is follow mode running?
    virtual bool setPositionToMapCenter() { return false; }  // NavMap: publish map centre as localPosition
    bool isZoomLocked() const { return s_zoomLocked; }
    bool canZoomIn() const;

#ifdef MOD_LOCAL_MAP_TILES
    // Neighborhood tile prefetch: warm the PSRAM tile cache around the last-rendered view
    // (visible rect + pan margin at the same zoom, the parent rect one zoom out, the child rect
    // one zoom in). One uncached tile fetched per call; returns false once the whole
    // neighborhood is cached - callers drive it from an idle tick so pan/zoom become instant.
    bool prefetchStep();

  protected:
    // Last-rendered view rect (recorded by the local render path; -1 z = nothing rendered yet)
    int prefetchZ = -1;
    int prefetchTxStart = 0, prefetchTxEnd = 0, prefetchTyStart = 0, prefetchTyEnd = 0;

  public:
#endif // MOD_LOCAL_MAP_TILES
    bool canZoomOut() const;

    // A node whose position is known but whose hop count is not: plotted as "?" instead of a hop digit.
    static constexpr uint8_t HOPS_UNKNOWN = 0xFF;

    // Category of a map label -> selects its icon and which user toggle gates it. Persisted as the label
    // record's category byte; keep the numeric values stable (they are written by the gen_labels tool).
    enum LabelCategory : uint8_t {
        LABEL_PLACE = 0,    // city / district / village name -> anchor dot + text
        LABEL_HOSPITAL = 1, // hospital / clinic / pharmacy   -> cross icon (no text)
        LABEL_SHELTER = 2,  // air-raid / evacuation shelter   -> house icon (no text)
        LABEL_POLICE = 3,   // police station                  -> icon (no text)
        LABEL_FIRE = 4,     // fire station                    -> icon (no text)
        LABEL_ROAD = 5,     // road name (Phase 2c)            -> text
        LABEL_POI = 6,      // generic named POI               -> anchor dot + text
        LABEL_CATEGORY_COUNT
    };

    // A map text label (POI / place / region name / emergency facility). Overlay labels are supplied by the
    // derived applet and rendered as real font text (and/or a category icon) over the tiles, replacing the
    // illegible baked-in raster labels.
    struct MapLabel {
        float lat;
        float lng;
        uint8_t minZoom;      // shown only at this render zoom and higher (level-of-detail)
        uint8_t rank;         // importance for decluttering; the array is placed in this order (lower = first)
        uint8_t category;     // LabelCategory: selects icon + gating toggle
        const char *text;     // UTF-8 (may be CJK); may be empty for icon-only categories
    };

  protected:
    virtual bool shouldDrawNode(meshtastic_NodeInfoLite *node) { return true; } // Allow derived applets to filter the nodes

    // Whether to plot node markers at all. A derived applet can hide them behind a user toggle.
    virtual bool shouldPlotNodeMarkers() { return true; }
    // Whether a node must have a known hop count to be plotted. Default true (partial info is usually noise);
    // a derived applet (NavMap) can return false to plot every node with a valid position.
    virtual bool requireHopCount() { return true; }

    // Draw text with a 1px white outline so it stays legible over busy map tiles: paint the string white at the
    // eight surrounding offsets, then the black text on top. Same signature as printAt(); leaves the colour black.
    void printAtHalo(int16_t x, int16_t y, const char *text, HorizontalAlignment ha = LEFT, VerticalAlignment va = TOP);

    // Whether node markers show the node's short name (<=4 chars) instead of the hop count. Default false;
    // NavMap overrides to true. The label is always populated in calculateAllMarkers so the switch is cheap.
    virtual bool markerShowsName() { return false; }

    // Whether the horizontal scale bar (drawn along the bottom edge) is shown. NavMap hides it because its
    // bottom-centre coordinate readout would overlap it; the vertical bar on the left edge still shows.
    virtual bool showHorizontalScaleBar() { return true; }

    // Marker box style. Default (false): black box, white halo, white text -- reads well on a light map. NavMap
    // returns true for the inverse: white box, black border, black text -- so markers stand out on dark tiles.
    virtual bool markerInverted() { return false; }

    // Whether a label of the given LabelCategory should be drawn. Base: always. NavMap overrides to gate each
    // category behind its user toggle (showHospitals / showShelters / showPlaceLabels / ...). Read live per render.
    virtual bool categoryEnabled(uint8_t category) { return true; }

    // Text-label overlay. The base draws nothing; a derived applet (NavMap) overrides drawMapLabels() to supply
    // its labels and call renderLabels(). MUST be invoked from inside onRender() -- it uses the mid-render
    // projection state (metersToPx / latCenter / lngCenter). renderLabels() does LOD (minZoom) + greedy
    // decluttering + a category icon and/or haloed text, reusing the node-marker lat/lng->screen projection.
    virtual void drawMapLabels() {}
    void renderLabels(const MapLabel *labels, int count);
    // Draw the category's icon centred on (cx,cy) with a white halo, or an anchor dot for text categories.
    void drawLabelAnchor(int16_t cx, int16_t cy, uint8_t category);
    static bool categoryHasText(uint8_t category); // PLACE/POI/ROAD show text; emergency categories are icon-only
#ifdef MOD_KEYBOARD_MAP_TILES
    void drawKeyboardLabels(); // fetch the keyboard's /map/labels.lbl (cached) + renderLabels(); call from drawMapLabels()
#endif
#ifdef MOD_LOCAL_MAP_TILES
    void drawLocalLabels(); // read the local bundle's per-view labels + renderLabels(); call from drawMapLabels()
#endif

    virtual void getMapCenter(float *lat, float *lng);
    virtual void getMapSize(uint32_t *widthMeters, uint32_t *heightMeters);

    // Centre (lat/lng) of the compiled-in map-tile coverage, if any. Lets a derived applet open on the
    // map even without a GPS/fixed position. Returns false when no tiles are present.
    bool getTileCenter(float *lat, float *lng);

    // The zoom actually being DRAWN (locked value, else the last auto-fit); -1 before the first render.
    // Derived applets must size pan steps from this, not from a separate estimate: with a sparse zoom set
    // zoomIn() can jump several levels at once (e.g. z11 -> z16), so an estimate drifts out of sync.
    int currentRenderZoom() const { return (s_zoomLocked && s_lockedZoom >= 0) ? s_lockedZoom : s_lastRenderedZoom; }

#ifdef MOD_KEYBOARD_MAP_TILES
    // Fetch the keyboard's zoom manifest EARLY -- call from a derived applet's onActivate(), so the zoom set is
    // known BEFORE the first render's zoom picker reads it (otherwise it sees no zooms and falls back to a
    // hardcoded zoom with no tiles). Wakes the keyboard first if needed, and its reply refreshes the keyboard's
    // 30s keep-alive, so the first render's tile fetches skip the wake handshake.
    void primeKeyboardTiles();
#endif
#ifdef MOD_LOCAL_MAP_TILES
    // Read the local bundle's /map/manifest EARLY (mirror of primeKeyboardTiles) -- call from a derived applet's
    // onActivate() so the zoom set is known before the first render's zoom picker reads it.
    void primeLocalTiles();
#endif

    virtual bool enoughMarkers();                          // Anything to draw?
    void drawLabeledMarker(meshtastic_NodeInfoLite *node); // Highlight a specific marker

  private:
    int onGpsStatusUpdate(const meshtastic::Status *status);
    CallbackObserver<MapApplet, const meshtastic::Status *> gpsStatusObserver =
        CallbackObserver<MapApplet, const meshtastic::Status *>(this, &MapApplet::onGpsStatusUpdate);

    static bool s_zoomLocked;
    static int s_lockedZoom;
    static int s_lastRenderedZoom;
    static int s_autoFitZoom; // Zoom chosen by auto-fit (updated whenever not locked)
    // Position and size of a marker to be drawn
    struct Marker {
        float eastMeters = 0;  // Meters east of map center. Negative if west.
        float northMeters = 0; // Meters north of map center. Negative if south.
        uint8_t hopsAway = 0;  // Determines marker size
        char label[5] = {0};   // Node short name (<=4 chars); empty -> the hop indicator is drawn instead
    };

    Marker calculateMarker(float lat, float lng, uint8_t hopsAway);
    void calculateAllMarkers();
    void calculateMapScale();                           // Conversion factor for meters to pixels
    void drawMapTileBackground(int zoom);               // Draw georeferenced tile at zoom
    // Blit one decoded 8192-byte (256x256 1-bit column-major) tile whose world-pixel origin is
    // (tileMinWx, tileMinWy), given the current projection. Shared by the compiled-in and keyboard sources.
    void blitTile(const uint8_t *tile, float tileMinWx, float tileMinWy, float gpxX, float gpxY, float tileWorldPx);
#ifdef MOD_KEYBOARD_MAP_TILES
    // Tile source = the companion keyboard's LittleFS over Serial2 (GET_TILE 0x0D), replacing MapTile.h. The
    // available zoom set (s_kbZooms in the .cpp) is FILE-SCOPE so the tileMetadata* zoom helpers can read it,
    // which routes the whole zoom picker (onRender / zoomIn / zoomOut / canZoom*) onto the keyboard's real zooms
    // instead of the compiled MapTile.h metadata. Only per-instance transfer state lives here.
    void drawMapTileBackgroundFromKeyboard(int zoom);
    const uint8_t *fetchTileFromKeyboard(int z, uint32_t tx, uint32_t ty); // one GET_TILE round-trip -> 8192 buf / nullptr
    void readKeyboardManifestIfNeeded();                                   // learn available zooms + coverage (once)
    uint32_t kbManifestLastAttempt = 0; // millis() of the last read attempt; rate-limits retries until zooms load
    bool kbFetchTimedOut = false;       // set by fetchTileFromKeyboard when the size header never arrives (vs white)
    // Tiled label overlay: /map/labels_<z>.idx + .dat, streamed per-view via READ_RANGE. The loader + fetch are
    // file-scope statics in the .cpp (they need no instance state); drawKeyboardLabels() drives them.
#endif
#ifdef MOD_LOCAL_MAP_TILES
    // Tile source = a local microSD / LittleFS map bundle (same format as the keyboard's, see MapApplet.cpp),
    // replacing MapTile.h. Twins of the keyboard transport; the shared zoom set (s_kbZooms) + label machinery in
    // the .cpp are reused, so only per-instance transfer state lives here.
    void drawMapTileBackgroundLocal(int zoom);
    const uint8_t *fetchTileLocal(int z, uint32_t tx, uint32_t ty); // one tile file read -> 8192 buf / nullptr
    void readLocalManifestIfNeeded();                               // learn available zooms + coverage (once)
    uint32_t localManifestLastAttempt = 0; // millis() of the last read attempt; rate-limits retries until zooms load
#endif
    void drawCross(int16_t x, int16_t y, uint8_t size); // Draw the X used for most markers

    float metersToPx = 0;         // Conversion factor for meters to pixels
    float latCenter = 0;          // Map center: latitude
    float lngCenter = 0;          // Map center: longitude
    bool centerIsOurNode = false; // True if map is centered on our own position (GPS or phone)

    std::list<Marker> markers;
    uint32_t widthMeters = 0;  // Map width: meters
    uint32_t heightMeters = 0; // Map height: meters
};

} // namespace NicheGraphics::InkHUD

#endif