#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./NavMapApplet.h"
#include "NodeDB.h"
#include "gps/RTC.h" // getValidTime -- timestamp for "Set Pos Here"
#include "modules/TextMessageModule.h" // textMessageModule -- source for the auto-jump-on-message feature

#if !MESHTASTIC_EXCLUDE_GPS && HAS_GPS
#include "GPS.h"      // gps->enable() -- forced-acquisition entry point
#include "PowerFSM.h" // EVENT_INPUT re-stamps the screen_on_secs dwell while a locate holds the device awake
#include "sleep.h"    // tdeckMaxLastWakeActivityMs -- the quick-sleep idle watchdog's activity stamp
#endif

#include <cmath> // cosf (Mercator-corrected latitude pan step)
#include <cstdio>
#include <cstdlib>

using namespace NicheGraphics;

InkHUD::NavMapApplet::NavMapApplet() : concurrency::OSThread("NavMapGPS")
{
    OSThread::disable(); // the tick only runs while a GPS locate is in flight
#if defined(MOD_INPUT_MENU)
    Controllable::registerControllable(this, Controllable::Types::NavMap);
#endif //defined(MOD_INPUT_MENU)
}

#if !MESHTASTIC_EXCLUDE_GPS && HAS_GPS
// --- Private (map-only) GPS session -----------------------------------------------------------
// Locate and trace share one session: beginGpsSession snapshots localPosition and raises
// localPositionBroadcastHold (PositionModule sends NOTHING while it is up), endGpsSession
// restores the snapshot before releasing the hold - so a fix acquired here is never visible to
// the mesh. If gps_mode is DISABLED, the session also powers the module up just for itself
// (RAM-only mode flip + XL9555 rail) and puts it back to sleep at the end.
// NOTE: with gps_mode ENABLED in config the GPS keeps fixing (and broadcasting) on its own
// schedule anyway - the privacy machinery only means the SESSION adds nothing new.

void InkHUD::NavMapApplet::beginGpsSession()
{
    if (!sessionActive) {
        sessionActive = true;
        posSnapshot = localPosition;
        localPositionBroadcastHold = true;
        if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_DISABLED) {
            sessionTempGps = true;
            config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED; // RAM only - never saved
#if defined(T_DECK_MAX)
            tdeckmaxApplyGpsRail(); // power the XL9555 GPS rail before waking the driver
#endif
        }
    }
#if defined(T_DECK_MAX)
    // Session-scoped stay-awake: acquisition needs the CPU up to pump NMEA and trace needs
    // live renders. The idle watchdog honours this hold (same mechanism as the USB/Debug
    // holds) - replaces the old per-tick EVENT_INPUT storm that could crash the device.
    tdeckmaxGpsSessionHold = true;
#endif
    gps->enable(); // forced-search entry; harmless if already searching
    OSThread::setIntervalFromNow(500);
    OSThread::enabled = true;
}

void InkHUD::NavMapApplet::endGpsSession()
{
    if (locating || tracing)
        return; // the other consumer still runs; keep the session
    if (!sessionActive)
        return;
    sessionActive = false;
#if defined(T_DECK_MAX)
    tdeckmaxGpsSessionHold = false; // release the stay-awake; normal quick-sleep resumes
#endif
    // Restore the pre-session position FIRST (both localPosition and our own NodeDB cache
    // entry), then release the broadcast hold: nothing acquired in between can ever be sent.
    // Never restore an EMPTY snapshot though: if the node had no position when the session
    // began, writing zeros back would wipe our own NodeDB slot for nothing - the freshly
    // acquired fix simply becomes the node's position, as if GPS had been enabled normally.
    if (posSnapshot.latitude_i || posSnapshot.longitude_i)
        nodeDB->updatePosition(nodeDB->getNodeNum(), posSnapshot, RX_SRC_LOCAL);
    localPositionBroadcastHold = false;
    if (sessionTempGps) {
        // Mirror the disabled-boot path: soft-sleep the module over UART while its rail is
        // still up, then cut the rail. gps_mode returns to DISABLED - the ENABLED value only
        // ever existed in RAM, so a reboot mid-session also lands back on the saved config.
        sessionTempGps = false;
        config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_DISABLED;
        if (gps)
            gps->disable();
#if defined(T_DECK_MAX)
        tdeckmaxGpsRailOff("NavMap session finished; config leaves GPS disabled");
#endif
    }
}

// One-shot locate: force a fresh acquisition and recenter on it when it lands. The OSThread
// tick babysits the search because the NMEA reader only runs while the CPU is up -- without a
// hold, the T-Deck Max quick-sleep watchdog light-sleeps the device ~3s after the menu closes
// and the search would never complete. localPosition.time moves ONLY on a real fix, so
// comparing against a baseline cleanly separates "fresh fix" from "stale fix re-published".
bool InkHUD::NavMapApplet::startGpsLocate()
{
    if (!gps || config.position.fixed_position)
        return false; // no module probed this boot, or the position is pinned by config
    locating = true;
    locateNoFix = false;
    locateStartMs = millis();
    locateLastRenderMs = millis();
    locateBaselineFixTime = localPosition.time;
    beginGpsSession();
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
    return true;
}

// Continuous follow mode: keep the GPS hot and recenter the map on every fresh fix (renders
// throttled to TRACE_RENDER_MIN_MS for e-ink wear). Session privacy identical to locate.
bool InkHUD::NavMapApplet::toggleGpsTrace()
{
    if (tracing) {
        tracing = false;
        endGpsSession();
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return true;
    }
    if (!gps || config.position.fixed_position)
        return false;
    tracing = true;
    traceFixTime = localPosition.time; // only fixes newer than "now" recenter
    beginGpsSession();
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
    return true;
}

int32_t InkHUD::NavMapApplet::runOnce()
{
    if (!locating && !tracing) {
        endGpsSession(); // safety net: never leave a session (hold/rail) behind
#ifdef MOD_LOCAL_MAP_TILES
        // Idle neighborhood tile prefetch (one tile per tick): warms the PSRAM cache around the
        // last-rendered view so pan/zoom hit memory instead of LittleFS+LZ4. Only while this
        // applet is on screen and the device is awake - it rides existing awake time, never
        // extends it.
        if (isForeground()
#if defined(T_DECK_MAX)
            && inkhudScreenAwake
#endif
            && prefetchStep())
            return 150;
#endif // MOD_LOCAL_MAP_TILES
        return OSThread::disable();
    }

    // Fresh fix landed?
    const bool freshFix =
        localPosition.time != locateBaselineFixTime && (localPosition.latitude_i || localPosition.longitude_i);

    if (locating && freshFix) {
        // One-shot satisfied: jump the centre there (range-checked + Mercator-clamped)
        locating = false;
        gotoCenter(localPosition.latitude_i * 1e-7f, localPosition.longitude_i * 1e-7f);
        traceFixTime = localPosition.time; // trace (if also on) has consumed this fix too
        endGpsSession();                   // no-op while tracing keeps the session
        if (!tracing)
            return OSThread::disable();
    } else if (locating && (uint32_t)(millis() - locateStartMs) > LOCATE_TIMEOUT_MS) {
        locating = false;
        locateNoFix = true; // shown by onRender until the next locate attempt
        endGpsSession();
        requestUpdate();
        if (!tracing)
            return OSThread::disable();
    }

    // Trace: recenter on every fix newer than the last one we consumed. The centre tracks every
    // fix; the RENDER is throttled to TRACE_RENDER_MIN_MS for e-ink wear (gotoCenter render arg).
    if (tracing && localPosition.time != traceFixTime && (localPosition.latitude_i || localPosition.longitude_i)) {
        traceFixTime = localPosition.time;
        const bool renderNow = (uint32_t)(millis() - locateLastRenderMs) >= TRACE_RENDER_MIN_MS;
        if (renderNow)
            locateLastRenderMs = millis();
        gotoCenter(localPosition.latitude_i * 1e-7f, localPosition.longitude_i * 1e-7f, renderNow);
    }

    // Keep acquisition hot while tracing: the driver duty-cycles the module down after each fix
    // window and parks for gps_update_interval (default 120s), capping "follow" at one recentre
    // per two minutes. Re-arm the search periodically so trace means tens of seconds at worst.
    if (tracing && gps && (uint32_t)(millis() - traceEnableKickMs) > 30000) {
        traceEnableKickMs = millis();
        gps->enable(); // same forced-search entry as beginGpsSession; harmless mid-acquisition
    }

    // NOTE deliberately NO powerFSM.trigger(EVENT_INPUT) here: the old per-tick trigger forced
    // an ON<->sleep oscillation (each cycle = two full-panel refreshes, one of them a
    // synchronous render inside the FSM transition) that could grind the panel and starve the
    // task watchdog - the probable "trace crashed the device" mechanism. The session hold in
    // TDeckMaxIdleSleepThread (tdeckmaxGpsSessionHold, set by beginGpsSession) now keeps the
    // device cleanly awake for the whole session instead.

    // Locate-in-progress elapsed-time readout: fast refresh at a slow cadence, so a 30s-3min
    // search shows progress without grinding the panel.
    if (locating && (uint32_t)(millis() - locateLastRenderMs) >= 15000) {
        locateLastRenderMs = millis();
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
    }
    return 2000;
}

// Publish the CURRENT map centre as the node's official position - the deliberate opposite of
// the private locate/trace. The mesh learns it through the normal PositionModule schedule
// (smart-broadcast sends early if it moved >~100m from the last sent position). During an
// active private session the snapshot is updated instead of localPosition being left to the
// session-end restore, so the chosen position survives the session.
bool InkHUD::NavMapApplet::setPositionToMapCenter()
{
    if (config.position.fixed_position)
        return false; // pinned via config; don't fight the admin setting

    float lat = 0, lng = 0;
    getCenter(lat, lng);
    if (lat == 0 && lng == 0)
        return false;

    meshtastic_Position p = meshtastic_Position_init_default;
    p.latitude_i = (int32_t)(lat * 1e7);
    p.longitude_i = (int32_t)(lng * 1e7);
    p.has_latitude_i = true;
    p.has_longitude_i = true;
    p.location_source = meshtastic_Position_LocSource_LOC_MANUAL;
    p.time = getValidTime(RTCQualityDevice, false); // seconds epoch; 0 is acceptable (no RTC)

    if (sessionActive)
        posSnapshot = p; // applied (and broadcastable) when the session ends
    else
        nodeDB->updatePosition(nodeDB->getNodeNum(), p, RX_SRC_LOCAL);
    LOG_INFO("NavMap: map centre published as localPosition (lat=%d lon=%d)%s", p.latitude_i, p.longitude_i,
             sessionActive ? " [deferred until session end]" : "");
    return true;
}
#else  // !MESHTASTIC_EXCLUDE_GPS && HAS_GPS
bool InkHUD::NavMapApplet::startGpsLocate()
{
    return false; // no GPS support in this build
}
bool InkHUD::NavMapApplet::toggleGpsTrace()
{
    return false;
}
bool InkHUD::NavMapApplet::setPositionToMapCenter()
{
    return false;
}
int32_t InkHUD::NavMapApplet::runOnce()
{
    return OSThread::disable();
}
void InkHUD::NavMapApplet::beginGpsSession() {}
void InkHUD::NavMapApplet::endGpsSession() {}
#endif // !MESHTASTIC_EXCLUDE_GPS && HAS_GPS

#if defined(MOD_INPUT_MENU)
InkHUD::NavMapApplet::~NavMapApplet()
{
    Controllable::unregisterControllable(this);
}
#endif //defined(MOD_INPUT_MENU)

void InkHUD::NavMapApplet::onActivate()
{
    mode = Mode::Idle;
#ifdef MOD_KEYBOARD_MAP_TILES
    // Grab the keyboard's zoom set before the first render: deferring it to render time means the zoom picker
    // runs with no zooms and falls back to a hardcoded one. Wakes the sleeping keyboard if needed, and the reply
    // refreshes its 30s keep-alive so the first render's tile fetches go straight through.
    primeKeyboardTiles();
#elif defined(MOD_LOCAL_MAP_TILES)
    // Same reason: read the local bundle's manifest before the first render so the zoom picker sees real zooms.
    primeLocalTiles();
#endif
#if defined(MOD_INPUT_MENU)
    textMessageObserver.observe(textMessageModule); // for auto-jump-on-message (gated in the callback)
#endif
    updateSubscription();
}

void InkHUD::NavMapApplet::onDeactivate()
{
#if defined(MOD_INPUT_MENU)
    textMessageObserver.unobserve(textMessageModule);
#endif
    setInputsSubscribed(NAV_UP | NAV_DOWN | NAV_LEFT | NAV_RIGHT, false);
    mode = Mode::Idle;
    // Cancel an in-flight GPS locate/trace: holding the device awake for a map that left the
    // screen would be a silent battery drain. Ending the private session restores the
    // pre-session position and releases the broadcast hold; a temporarily-powered GPS is put
    // back to sleep, a config-enabled one returns to its normal duty cycle on its own.
    locating = false;
    tracing = false;
    endGpsSession();
    OSThread::disable();
}

// User toggles (persisted in settings), read live each render so the menu takes effect immediately.
bool InkHUD::NavMapApplet::shouldPlotNodeMarkers()
{
    return settings->optionalFeatures.showNodeMarkers;
}
bool InkHUD::NavMapApplet::requireHopCount()
{
    return false; // plot every node with a valid position, even without a known hop count
}

// Map-label overlay: labels are fetched from the keyboard's /map/labels.lbl (Phase 1) and rendered as crisp
// font text. renderLabels() does the LOD + declutter; the fetch/parse/cache lives in MapApplet.
void InkHUD::NavMapApplet::drawMapLabels()
{
    if (!settings->optionalFeatures.showMapLabels)
        return; // master switch off -> no overlay at all
#if defined(MOD_KEYBOARD_MAP_TILES)
    drawKeyboardLabels();
#elif defined(MOD_LOCAL_MAP_TILES)
    drawLocalLabels();
#endif
}

// Gate each label category behind its own user toggle (read live so the menu takes effect immediately). The
// master showMapLabels switch is already checked in drawMapLabels(); this is the per-category filter.
bool InkHUD::NavMapApplet::categoryEnabled(uint8_t category)
{
    const auto &f = settings->optionalFeatures;
    switch (category) {
    case LABEL_HOSPITAL:
        return f.showHospitals;
    case LABEL_SHELTER:
        return f.showShelters;
    case LABEL_POLICE:
    case LABEL_FIRE:
        return f.showEmergencyServices;
    case LABEL_PLACE:
    case LABEL_POI:
    case LABEL_ROAD:
    default:
        return f.showPlaceLabels;
    }
}

#if defined(MOD_INPUT_MENU)
// A text message arrived. If the user enabled auto-jump, recenter on the sender's last-known
// position - in the BACKGROUND too (user decision 2026-08-09: the old foreground-only gate made
// the feature unobservable, because chat autoshow replaced the map in the same render pass the
// jump fired in). Background recenter is state-only (render=false): two floats change in RAM
// and the map shows the new center whenever it next renders - no e-ink refresh, no power cost.
// Foreground recenter rides the render pass the message already triggered. Deliberately never
// brings NavMap to the foreground itself.
int InkHUD::NavMapApplet::onReceiveTextMessage(const meshtastic_MeshPacket *p)
{
    if (!settings->optionalFeatures.autoJumpToMsgSender)
        return 0;
    NodeNum from = getFrom(p);
    if (from == nodeDB->getNodeNum())
        return 0; // our own outgoing message
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(from);
    meshtastic_PositionLite pos;
    if (node && nodeDB->hasValidPosition(node) && nodeDB->copyNodePosition(from, pos))
        gotoCenter(pos.latitude_i * 1e-7f, pos.longitude_i * 1e-7f, isForeground()); // background: silent recenter
    return 0; // keep notifying other observers
}
#endif //defined(MOD_INPUT_MENU)

// Up/down are always captured (up enters Navigate from Idle). Left/right are captured only while in a
// mode, so Idle left/right keep their default applet/tile-switching behaviour on the joystick path.
void InkHUD::NavMapApplet::updateSubscription()
{
    setInputsSubscribed(NAV_UP | NAV_DOWN, true);
    setInputsSubscribed(NAV_LEFT | NAV_RIGHT, mode != Mode::Idle);
}

void InkHUD::NavMapApplet::setMode(Mode m)
{
    mode = m;
    updateSubscription();
    requestUpdate(Drivers::EInk::UpdateTypes::FAST); // navigation is interactive: fast refresh, deghost when idle
}

const char *InkHUD::NavMapApplet::modeLabel() const
{
    switch (mode) {
    case Mode::Navigate:
        return "NAV";
    case Mode::Scale:
        return "SCALE";
    case Mode::Idle:
    default:
        return "up=nav";
    }
}

// Default map centre used when no position is known. This MUST be a coordinate that has tile coverage at
// EVERY zoom you ship. Do not use the tile-coverage centroid: with sparse high-zoom data (e.g. z16 city
// detail scattered inside a wide z11 extent) the centroid lands in a rural gap, so the map opens blank the
// moment you zoom in. Override with -D INKHUD_MAP_DEFAULT_LAT / -D INKHUD_MAP_DEFAULT_LNG.
#ifndef INKHUD_MAP_DEFAULT_LAT
#define INKHUD_MAP_DEFAULT_LAT 25.03467187539701f // Taipei -- verified to have valid z11 AND z16 tiles
#endif
#ifndef INKHUD_MAP_DEFAULT_LNG
#define INKHUD_MAP_DEFAULT_LNG 121.52176143433692f
#endif

// Seed the initial centre: the explicit default (known-good coverage), else the tile-coverage centroid,
// else our own node's position, else the first peer we know a position for.
void InkHUD::NavMapApplet::seedCenterIfNeeded()
{
    if (centerSeeded)
        return;

#if defined(INKHUD_MAP_DEFAULT_LAT) && defined(INKHUD_MAP_DEFAULT_LNG)
    // Explicit default: opens on a spot with tiles at every zoom, so zooming in never lands on empty data.
    manualLat = INKHUD_MAP_DEFAULT_LAT;
    manualLng = INKHUD_MAP_DEFAULT_LNG;
    centerSeeded = true;
#else
    // Fall back to the centre of the loaded map tiles, so the applet still opens on the map with no GPS.
    // Keyboard tiles load asynchronously, so if the manifest isn't ready leave the centre unseeded and RETRY
    // next render -- otherwise we'd latch a (0,0) centre off the map before the coverage is known.
    if (getTileCenter(&manualLat, &manualLng)) {
        centerSeeded = true;
        return;
    }

    meshtastic_PositionLite pos;
    const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (ourNode && nodeDB->hasValidPosition(ourNode) && nodeDB->copyNodePosition(ourNode->num, pos)) {
        manualLat = pos.latitude_i * 1e-7f;
        manualLng = pos.longitude_i * 1e-7f;
        centerSeeded = true;
        return;
    }
    for (uint32_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (node && nodeDB->hasValidPosition(node) && nodeDB->copyNodePosition(node->num, pos)) {
            manualLat = pos.latitude_i * 1e-7f;
            manualLng = pos.longitude_i * 1e-7f;
            centerSeeded = true;
            return;
        }
    }
    // Nothing to seed from yet (no tiles loaded, no position). Leave centerSeeded=false to retry next render.
#endif // defined(INKHUD_MAP_DEFAULT_LAT) && defined(INKHUD_MAP_DEFAULT_LNG)
}

void InkHUD::NavMapApplet::getMapCenter(float *lat, float *lng)
{
    seedCenterIfNeeded();
    *lat = manualLat;
    *lng = manualLng;
}

void InkHUD::NavMapApplet::getMapSize(uint32_t *widthMeters, uint32_t *heightMeters)
{
    *widthMeters = defaultSpanMeters;
    *heightMeters = defaultSpanMeters;
}

bool InkHUD::NavMapApplet::enoughMarkers()
{
    return true;
}

void InkHUD::NavMapApplet::onRender(bool full)
{
    // Reuse the base map: tile background (blank on tile-less builds), node markers, scale bars.
    MapApplet::onRender(full);

    // Crosshair at the applet centre (the current pan target) + a small mode indicator. White-haloed so they
    // stay legible over the map tiles.
    printAtHalo(X(0.5), Y(0.5), "+", CENTER, MIDDLE);
    printAtHalo(X(0.02), Y(0.02), modeLabel(), LEFT, TOP);

    // Current centre coordinate (read it here; edit it via the InputMenu "Go to coord" command).
    char coord[28];
    snprintf(coord, sizeof(coord), "%.4f, %.4f", manualLat, manualLng);
    printAtHalo(X(0.5), Y(0.98), coord, CENTER, BOTTOM);

#if !MESHTASTIC_EXCLUDE_GPS && HAS_GPS
    // GPS locate/trace status, top-right (the mode indicator owns the top-left corner).
    if (locating) {
        char gpsline[16];
        snprintf(gpsline, sizeof(gpsline), "GPS %us", (unsigned)((millis() - locateStartMs) / 1000));
        printAtHalo(X(0.98), Y(0.02), gpsline, RIGHT, TOP);
    } else if (tracing) {
        // Follow mode: show fix age so a stalled trace (indoors, antenna shadow) is obvious
        char gpsline[16];
        if (traceFixTime && traceFixTime == localPosition.time)
            snprintf(gpsline, sizeof(gpsline), "Trace ok");
        else
            snprintf(gpsline, sizeof(gpsline), "Trace...");
        printAtHalo(X(0.98), Y(0.02), gpsline, RIGHT, TOP);
    } else if (locateNoFix) {
        printAtHalo(X(0.98), Y(0.02), "GPS: no fix", RIGHT, TOP);
    }
#endif

#ifdef MOD_LOCAL_MAP_TILES
    // Every repaint may have moved the view: (re)start the idle prefetch tick. It self-disables
    // once the neighborhood is fully cached; locate/trace sessions own the thread when active.
    if (!locating && !tracing) {
        OSThread::setIntervalFromNow(250);
        OSThread::enabled = true;
    }
#endif // MOD_LOCAL_MAP_TILES
}

#if defined(MOD_INPUT_MENU)
void InkHUD::NavMapApplet::getCenter(float &lat, float &lng)
{
    seedCenterIfNeeded();
    lat = manualLat;
    lng = manualLng;
}

bool InkHUD::NavMapApplet::gotoCenter(float lat, float lng, bool render)
{
    if (!(lat >= -90.0f && lat <= 90.0f && lng >= -180.0f && lng <= 180.0f))
        return false; // out of range (or NaN) -> ignore
    // Clamp to the web-mercator operational limit the pan handlers enforce (panNorth/panSouth ±85). Beyond it the
    // Mercator projection blows up (cosf(90°) is a tiny negative -> negative metersToPx -> blank map) and the
    // per-press pan step collapses to ~0, soft-locking the user on an empty map until the next go-to-coord.
    if (lat > 85.0f)
        lat = 85.0f;
    else if (lat < -85.0f)
        lat = -85.0f;
    manualLat = lat;
    manualLng = lng;
    centerSeeded = true; // pin against auto-seeding
    if (render)
        requestUpdate(Drivers::EInk::UpdateTypes::FAST); // navigation is interactive: fast refresh, deghost when idle
    return true;
}

namespace
{
struct NamedPlace {
    const char *en;
    const char *cjk;
    float lat;
    float lng;
};

// Jump targets for typing a place name in the "go to" menu. Coordinates are the city/county seat (or the landmark
// centre). Stored in flash (static const) -> zero RAM. Ordered largest-city-first so a short ambiguous English
// prefix (e.g. "tai") resolves to the biggest match.
const NamedPlace kNamedPlaces[] = {
    {"Taipei", "台北", 25.0330f, 121.5654f},        {"New Taipei", "新北", 25.0117f, 121.4648f},
    {"Taoyuan", "桃園", 24.9936f, 121.3010f},       {"Taichung", "台中", 24.1477f, 120.6736f},
    {"Tainan", "台南", 22.9908f, 120.2133f},        {"Kaohsiung", "高雄", 22.6273f, 120.3014f},
    {"Keelung", "基隆", 25.1283f, 121.7419f},       {"Hsinchu", "新竹", 24.8138f, 120.9675f},
    {"Chiayi", "嘉義", 23.4801f, 120.4491f},        {"Hsinchu County", "新竹縣", 24.8272f, 121.0113f},
    {"Miaoli", "苗栗", 24.5700f, 120.8214f},        {"Changhua", "彰化", 24.0685f, 120.5440f},
    {"Nantou", "南投", 23.9157f, 120.6869f},        {"Yunlin", "雲林", 23.7092f, 120.5459f},
    {"Chiayi County", "嘉義縣", 23.4590f, 120.3200f}, {"Pingtung", "屏東", 22.6761f, 120.4942f},
    {"Yilan", "宜蘭", 24.7021f, 121.7377f},         {"Hualien", "花蓮", 23.9769f, 121.6044f},
    {"Taitung", "台東", 22.7972f, 121.1713f},       {"Penghu", "澎湖", 23.5654f, 119.5793f},
    {"Kinmen", "金門", 24.4489f, 118.3767f},        {"Matsu", "馬祖", 26.1590f, 119.9497f},
    // Landmarks
    {"Yushan", "玉山", 23.4700f, 120.9575f},        {"Sun Moon Lake", "日月潭", 23.8500f, 120.9150f},
    {"Alishan", "阿里山", 23.5100f, 120.8000f},     {"Taroko", "太魯閣", 24.1580f, 121.4900f},
    {"Kenting", "墾丁", 21.9480f, 120.7970f},
};

// True if the first `len` bytes of `input` are a prefix of `cand` (case-insensitive for ASCII; CJK compares as
// raw UTF-8 bytes, which is fine when whole characters are typed).
bool placePrefixMatch(const char *input, size_t len, const char *cand)
{
    for (size_t i = 0; i < len; i++) {
        char a = input[i], b = cand[i];
        if (!b)
            return false; // candidate is shorter than the typed text -> not a prefix
        if (a >= 'A' && a <= 'Z')
            a += 32;
        if (b >= 'A' && b <= 'Z')
            b += 32;
        if (a != b)
            return false;
    }
    return true;
}
} // namespace

// Jump to a named place typed in the "go to" menu (the commit handler falls back here when the text isn't
// "lat,lng"). Matches the trimmed input as a case-insensitive prefix of either the English or CJK name; the first
// entry in kNamedPlaces wins. Returns false (no jump) when nothing matches.
bool InkHUD::NavMapApplet::gotoPlace(const char *name)
{
    if (!name)
        return false;
    while (*name == ' ' || *name == '\t' || *name == '\r' || *name == '\n')
        name++;
    size_t n = 0;
    while (name[n])
        n++;
    while (n > 0 && (name[n - 1] == ' ' || name[n - 1] == '\t' || name[n - 1] == '\r' || name[n - 1] == '\n'))
        n--;
    if (n == 0)
        return false;
    for (const NamedPlace &p : kNamedPlaces)
        if (placePrefixMatch(name, n, p.en) || placePrefixMatch(name, n, p.cjk))
            return gotoCenter(p.lat, p.lng);
    return false;
}
#endif // defined(MOD_INPUT_MENU)

// A LONGITUDE pan increment: at web-mercator zoom z one 256 px tile spans 360/2^z degrees, so ~1/4 tile
// (64 px) per press feels responsive and always overlaps the previous view.
//
// The step MUST come from the zoom actually being rendered, not from zoomEstimate. With a sparse zoom set
// (e.g. the keyboard serves only z11 and z16) zoomIn() jumps s_lockedZoom straight from 11 to 16 while
// zoomEstimate merely ++'s -- so a "quarter tile at z14" is a WHOLE 256 px tile at z16 and the map jumps a
// full screen per press instead of panning smoothly.
float InkHUD::NavMapApplet::panStepDegrees() const
{
    int z = currentRenderZoom();
    if (z < 0)
        z = zoomEstimate; // nothing rendered yet
    return (360.0f / (float)(1 << z)) * 0.25f;
}

// The matching LATITUDE increment. In Mercator dy/dlat = sec(lat) * dx/dlng, so an equal step in degrees
// moves 1/cos(lat) further vertically (at 25 deg N that is ~10% more). Scale it so an up/down press covers
// the same number of screen pixels as a left/right press.
float InkHUD::NavMapApplet::panStepLatDegrees() const
{
    return panStepDegrees() * cosf(manualLat * DEG_TO_RAD);
}

void InkHUD::NavMapApplet::panNorth()
{
    seedCenterIfNeeded();
    manualLat += panStepLatDegrees();
    if (manualLat > 85.0f)
        manualLat = 85.0f; // web-mercator latitude limit
    requestUpdate(Drivers::EInk::UpdateTypes::FAST); // navigation is interactive: fast refresh, deghost when idle
}

void InkHUD::NavMapApplet::panSouth()
{
    seedCenterIfNeeded();
    manualLat -= panStepLatDegrees();
    if (manualLat < -85.0f)
        manualLat = -85.0f;
    requestUpdate(Drivers::EInk::UpdateTypes::FAST); // navigation is interactive: fast refresh, deghost when idle
}

void InkHUD::NavMapApplet::panEast()
{
    seedCenterIfNeeded();
    manualLng += panStepDegrees();
    if (manualLng > 180.0f)
        manualLng -= 360.0f; // wrap
    requestUpdate(Drivers::EInk::UpdateTypes::FAST); // navigation is interactive: fast refresh, deghost when idle
}

void InkHUD::NavMapApplet::panWest()
{
    seedCenterIfNeeded();
    manualLng -= panStepDegrees();
    if (manualLng < -180.0f)
        manualLng += 360.0f; // wrap
    requestUpdate(Drivers::EInk::UpdateTypes::FAST); // navigation is interactive: fast refresh, deghost when idle
}

void InkHUD::NavMapApplet::zoomInStep()
{
    zoomIn(); // base: locks the (shared static) zoom and switches to its native scale
    if (zoomEstimate < ZOOM_MAX_NO_TILES)
        zoomEstimate++;
    requestUpdate(Drivers::EInk::UpdateTypes::FAST); // navigation is interactive: fast refresh, deghost when idle
}

void InkHUD::NavMapApplet::zoomOutStep()
{
    zoomOut();
    if (zoomEstimate > ZOOM_MIN)
        zoomEstimate--;
    requestUpdate(Drivers::EInk::UpdateTypes::FAST); // navigation is interactive: fast refresh, deghost when idle
}

// --- Mode-aware key actions (shared by both input paths) ---

bool InkHUD::NavMapApplet::pressUp()
{
    switch (mode) {
    case Mode::Idle:
        setMode(Mode::Navigate); // "press up" to start navigating
        return true;
    case Mode::Navigate:
        panNorth();
        return true;
    case Mode::Scale:
        zoomInStep();
        return true;
    }
    return true;
}

bool InkHUD::NavMapApplet::pressDown()
{
    switch (mode) {
    case Mode::Idle:
        return false; // nothing while idle
    case Mode::Navigate:
        panSouth();
        return true;
    case Mode::Scale:
        zoomOutStep();
        return true;
    }
    return false;
}

bool InkHUD::NavMapApplet::pressLeft()
{
    switch (mode) {
    case Mode::Idle:
        return false; // let the router switch applet/tile
    case Mode::Navigate:
        panWest();
        return true;
    case Mode::Scale:
        return true; // consume (no-op) so it doesn't switch applets
    }
    return false;
}

bool InkHUD::NavMapApplet::pressRight()
{
    switch (mode) {
    case Mode::Idle:
        return false; // let the router switch applet/tile
    case Mode::Navigate:
        panEast();
        return true;
    case Mode::Scale:
        return true; // consume (no-op)
    }
    return false;
}

bool InkHUD::NavMapApplet::pressEnter()
{
    switch (mode) {
    case Mode::Idle:
        return false; // let the router advance to the next applet (previous behaviour)
    case Mode::Navigate:
        setMode(Mode::Scale);
        return true;
    case Mode::Scale:
        return true; // already scaling
    }
    return false;
}

bool InkHUD::NavMapApplet::pressBack()
{
    switch (mode) {
    case Mode::Idle:
        return false; // nothing while idle
    case Mode::Navigate:
        setMode(Mode::Idle); // exit navigation mode
        return true;
    case Mode::Scale:
        setMode(Mode::Navigate); // exit scale mode, back to navigation
        return true;
    }
    return false;
}

// --- joystick / Events path ---
void InkHUD::NavMapApplet::onNavUp()
{
    pressUp();
}
void InkHUD::NavMapApplet::onNavDown()
{
    pressDown();
}
void InkHUD::NavMapApplet::onNavLeft()
{
    pressLeft();
}
void InkHUD::NavMapApplet::onNavRight()
{
    pressRight();
}

#if defined(MOD_INPUT_MENU)
// --- InputMenu / Controllable path (T-keyboard / 12-key) ---
bool InkHUD::NavMapApplet::handleUp()
{
    return pressUp();
}
bool InkHUD::NavMapApplet::handleDown()
{
    return pressDown();
}
bool InkHUD::NavMapApplet::handleLeft()
{
    return pressLeft();
}
bool InkHUD::NavMapApplet::handleRight()
{
    return pressRight();
}
bool InkHUD::NavMapApplet::handleEnter()
{
    return pressEnter();
}
bool InkHUD::NavMapApplet::handleBack()
{
    return pressBack();
}
#endif //defined(MOD_INPUT_MENU)

#endif
