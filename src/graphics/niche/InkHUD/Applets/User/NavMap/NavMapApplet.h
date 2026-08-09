#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

    A manually-navigable map, for devices without GPS (e.g. Heltec Mesh Pocket / KMP2).

    Unlike PositionsApplet / FavoritesMapApplet, the view is NOT auto-centered on our own node
    or the node centroid. The user drives it with a small mode machine so that, when idle, the
    normal applet-switching keys still work:

        Idle      : left / right / enter behave as usual (switch applet / tile).
                    press UP to enter Navigate mode.
        Navigate  : arrow keys pan the map (N/S/W/E).
                    ENTER  -> Scale mode.
                    BACK   -> back to Idle.
        Scale     : UP zoom in, DOWN zoom out.
                    BACK   -> back to Navigate.

    It reuses MapApplet's tile background, node-marker and scale-bar rendering; only the centre
    and the (existing, shared) zoom lock are driven manually.

    Input routing:
      - MOD_INPUT_MENU builds (T-keyboard / 12-key, e.g. KMP2): up/down/enter/back reach a foreground
        Controllable via InputMenuApplet. Left/right reach it via the forwarding added to
        InputMenuApplet::handleBackgroundTKey, which falls back to tile/applet switching when the
        applet returns false (i.e. while Idle). So while Idle, left/right/enter still switch applets.
      - Joystick / rocker builds: the same actions arrive via onNav*. We only subscribe NAV_LEFT/RIGHT
        while in a mode, so Idle left/right keep their default applet-switching behaviour.

*/

#pragma once

#include "configuration.h"

#include "concurrency/OSThread.h" // GPS-locate babysitter tick (hold-awake + fix polling + timeout)
#include "graphics/niche/InkHUD/Applets/Bases/Map/MapApplet.h"

#if defined(MOD_INPUT_MENU)
#include "graphics/niche/InkHUD/Applets/Bases/Controllable/Controllable.h"
#endif //defined(MOD_INPUT_MENU)

namespace NicheGraphics::InkHUD
{

#if defined(MOD_INPUT_MENU)
class NavMapApplet : public MapApplet, public concurrency::OSThread, virtual public Controllable
#else //!defined(MOD_INPUT_MENU)
class NavMapApplet : public MapApplet, public concurrency::OSThread
#endif //defined(MOD_INPUT_MENU)
{
  public:
    NavMapApplet();

    // Menu "GPS Locate": force a fresh GPS acquisition, keep the device awake while it searches,
    // and recenter the map on the fix when it lands. False when no GPS module / fixed_position.
    bool startGpsLocate() override;
#if defined(MOD_INPUT_MENU)
    ~NavMapApplet() override;
#endif //defined(MOD_INPUT_MENU)

    void onRender(bool full) override;

    // Directional input - joystick / Events path
    void onNavUp() override;
    void onNavDown() override;
    void onNavLeft() override;
    void onNavRight() override;

#if defined(MOD_INPUT_MENU)
    // Directional input - InputMenu / Controllable path (T-keyboard / 12-key). Returning true marks
    // the key consumed so the router does not fall through to tile/applet switching.
    bool handleUp() override;
    bool handleDown() override;
    bool handleLeft() override;
    bool handleRight() override;
    bool handleEnter() override;
    bool handleBack() override;

    // Coordinate readout / jump, for the InputMenu "Go to coordinate" feature.
    void getCenter(float &lat, float &lng); // current centre (seeds first, so it isn't 0,0 before any pan)
    // Jump to lat/lng; returns false (ignored) if out of range. render=false updates the centre
    // without requesting a refresh (trace mode tracks every fix but throttles e-ink wear).
    bool gotoCenter(float lat, float lng, bool render = true);
    bool gotoPlace(const char *name);       // jump to a built-in named place (Taipei/台北, …); false if no match
#endif //defined(MOD_INPUT_MENU)

  protected:
    void onActivate() override;
    void onDeactivate() override;

    int32_t runOnce() override; // GPS-locate babysitter (enabled only while a locate is running)

    // Node-marker behaviour, both driven by user toggles in settings->optionalFeatures:
    bool shouldPlotNodeMarkers() override; // showNodeMarkers -- hide/show all peer markers
    bool requireHopCount() override;       // false -- plot every node with a valid position, hop count or not
    bool markerShowsName() override { return true; }        // label markers with the node short name, not the hop count
    bool showHorizontalScaleBar() override { return false; } // hide it -- the bottom coord readout would overlap
    bool markerInverted() override { return true; }          // white box + black border + black text (stands out on dark tiles)
    void drawMapLabels() override;                           // label overlay (keyboard-served, per-category toggled)
    bool categoryEnabled(uint8_t category) override;         // gate each LabelCategory behind its user toggle

#if defined(MOD_INPUT_MENU)
    // Recenter on a message's sender when a new text message arrives (gated on autoJumpToMsgSender;
    // works in background too - state-only there, no render request).
    // Gated on MOD_INPUT_MENU because it uses gotoCenter(), which is only available in those builds -- and NavMap
    // is only ever added to a tile under MOD_INPUT_MENU (see the variant's nicheGraphics.h).
    int onReceiveTextMessage(const meshtastic_MeshPacket *p);
    CallbackObserver<NavMapApplet, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<NavMapApplet, const meshtastic_MeshPacket *>(this, &NavMapApplet::onReceiveTextMessage);
#endif //defined(MOD_INPUT_MENU)

    // Render even when neither our node nor any peer has a position, so the user can still pan/zoom.
    bool enoughMarkers() override;

    // The seam that forces the map centre. MapApplet::onRender passes &latCenter/&lngCenter here, so
    // whatever we write to *lat/*lng lands directly in the base's (private) centre.
    void getMapCenter(float *lat, float *lng) override;

    // Fixed default span so the (unlocked) view always has a valid scale, even with no nodes/GPS.
    void getMapSize(uint32_t *widthMeters, uint32_t *heightMeters) override;

    // Private-GPS session controls (menu; see the session block in the private section below)
    bool toggleGpsTrace() override;
    bool isGpsTracing() override { return tracing; }
    bool setPositionToMapCenter() override;

  private:
    enum class Mode : uint8_t { Idle, Navigate, Scale };

    // Mode-aware key actions, shared by the Controllable (handle*) and joystick (onNav*) paths.
    // Return true if the key was consumed (Idle returns false for left/right/enter so the router
    // performs the usual applet/tile switch).
    bool pressUp();
    bool pressDown();
    bool pressLeft();
    bool pressRight();
    bool pressEnter();
    bool pressBack();

    void setMode(Mode m);
    void updateSubscription();
    const char *modeLabel() const;

    void seedCenterIfNeeded();
    float panStepDegrees() const;    // longitude step, from the RENDERED zoom (a quarter tile)
    float panStepLatDegrees() const; // latitude step, Mercator-corrected so it covers the same screen pixels
    void panNorth();
    void panSouth();
    void panEast();
    void panWest();
    void zoomInStep();
    void zoomOutStep();

    Mode mode = Mode::Idle;

    float manualLat = 0.0f; // current map centre (manually panned)
    float manualLng = 0.0f;
    bool centerSeeded = false;

    // Local estimate of the active zoom, kept in step with the base zoomIn()/zoomOut() so the pan
    // step can scale with zoom. Approximate; used only for pan-step sizing.
    int zoomEstimate = 13;

    uint32_t defaultSpanMeters = 5000; // ~5 km initial view when unlocked

    static constexpr int ZOOM_MIN = 2;

    // --- Private GPS session (startGpsLocate / toggleGpsTrace / runOnce / onRender) ---
    // Locate and trace are MAP-ONLY: beginGpsSession snapshots localPosition and raises
    // localPositionBroadcastHold, endGpsSession restores the snapshot before clearing it, so a
    // fix acquired here is never visible to the mesh (see NodeDB.h). "Set Pos Here" is the
    // explicit opposite: it publishes the map centre as the official position.
    bool locating = false;
    bool locateNoFix = false;    // last locate timed out: onRender shows "GPS: no fix"
    bool tracing = false;        // continuous follow mode (menu "GPS Trace"): recenter on every fix
    bool sessionActive = false;  // private-GPS session running (locate and/or trace)
    bool sessionTempGps = false; // session powered up a config-DISABLED GPS just for itself
    meshtastic_Position posSnapshot = meshtastic_Position_init_default; // restored at session end
    void beginGpsSession(); // snapshot + broadcast hold + (if needed) temp GPS power + gps->enable()
    void endGpsSession();   // no-op while locate or trace still runs; else restore + release
    uint32_t locateStartMs = 0;
    uint32_t locateBaselineFixTime = 0; // localPosition.time moves ONLY on a real fix
    uint32_t traceFixTime = 0;          // last fix already consumed by the trace recenter
    uint32_t traceEnableKickMs = 0;     // last gps->enable() re-arm (defeats the 120s duty-cycle park)
    uint32_t locateLastRenderMs = 0;
    static constexpr uint32_t LOCATE_TIMEOUT_MS = 180 * 1000; // a cold GPS start can need minutes
    static constexpr uint32_t TRACE_RENDER_MIN_MS = 5000;     // e-ink wear: recenter renders at most this often
};

} // namespace NicheGraphics::InkHUD

#endif
