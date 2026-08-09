#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./BatteryIconApplet.h"

#if defined(T_DECK_MAX)
#include "main.h" // config (GPS indicator reads config.position.gps_mode)
#if defined(MOD_INPUT_MENU)
#include "graphics/niche/InkHUD/Applets/System/InputMenu/InputMenuApplet.h" // keyboard-lock indicator
#endif
#endif

using namespace NicheGraphics;

InkHUD::BatteryIconApplet::BatteryIconApplet()
{
    alwaysRender = true; // render every time the screen is updated

#if defined(T_DECK_MAX)
    // T-Deck Max sleep-UX: always foreground, because this applet also stamps the "asleep" moon
    // into its tile, regardless of whether the battery-icon feature is enabled. The battery glyph
    // itself is still gated by optionalFeatures.batteryIcon, in onRender.
    bringToForeground();
#else
    // Show at boot, if user has previously enabled the feature
    if (settings->optionalFeatures.batteryIcon)
        bringToForeground();
#endif

    // Register to our have BatteryIconApplet::onPowerStatusUpdate method called when new power info is available
    // This happens whether or not the battery icon feature is enabled
    powerStatusObserver.observe(&powerStatus->onNewStatus);
}

// We handle power status' even when the feature is disabled,
// so that we have up to date data ready if the feature is enabled later.
// Otherwise could be 30s before new status update, with weird battery value displayed
int InkHUD::BatteryIconApplet::onPowerStatusUpdate(const meshtastic::Status *status)
{
    // System applets are always active
    assert(isActive());

    // This method should only receive power statuses
    // If we get a different type of status, something has gone weird elsewhere
    assert(status->getStatusType() == STATUS_TYPE_POWER);

    const meshtastic::PowerStatus *pwrStatus = (const meshtastic::PowerStatus *)status;

    // Get the new state of charge %, and round to the nearest 10%
    uint8_t newSocRounded = ((pwrStatus->getBatteryChargePercent() + 5) / 10) * 10;

    // If rounded value has changed, trigger a display update
    // It's okay to requestUpdate before we store the new value, as the update won't run until next loop()
    // Don't trigger an update if the feature is disabled
    if (this->socRounded != newSocRounded && settings->optionalFeatures.batteryIcon)
        requestUpdate();

    // Store the new value
    this->socRounded = newSocRounded;

    return 0; // Tell Observable to continue informing other observers
}

void InkHUD::BatteryIconApplet::onRender(bool full)
{
#if defined(T_DECK_MAX)
    // Top-right indicator cluster, right-aligned: [lock][bell][eye][mast][GPS][moon][battery].
    // Battery keeps the exact corner and is drawn asleep AND awake. The PADLOCK means "keyboard
    // not responding": screen asleep (e-ink retains that frame for the whole sleep, so the lock
    // is what the user sees) or the alt+L lock. The MOON means silent mode (no e-ink refresh at
    // all while asleep). The GPS reticle appears while the GPS is powered. Only the occupied
    // span is cleared white - the rest of the tile stays transparent so header text beneath
    // keeps the width.
    const bool showBattery = settings->optionalFeatures.batteryIcon;
    const bool showMoon = tdeckmaxSilentMode;              // silent mode: no e-ink refresh at all while asleep
    const bool showGps = gpsIndicatorOn();
    const bool showAntenna = tdeckmaxGetAntennaExternal(); // shown only for the non-default external mux
    const bool showDebugHold = tdeckmaxDebugHold;          // session stay-awake hold (menu / alt+H)
    const bool showVibra = (tdeckmaxVibraMode != 0);       // non-default buzz policy (DMs only / off)
    const bool showLock = !inkhudScreenAwake || keyboardLocked(); // keyboard not responding: asleep, or alt+L lock

    const uint16_t occupied = occupiedWidth();
    if (occupied == 0)
        return;
    fillRect(width() - occupied, 0, occupied, height(), WHITE);

    int16_t xRight = width(); // glyphs are placed right-to-left from the tile edge
    if (showBattery) {
        const uint16_t bw = batteryGlyphWidth();
        xRight -= bw;
        drawBatteryGlyph(xRight, bw);
    }
    const uint16_t slotW = indicatorSlotWidth();
    const int16_t cy = height() / 2;
    if (showMoon) {
        xRight -= slotW;
        // Crescent moon: a filled black disc with a shifted white disc overdrawn to carve the crescent
        const int16_t r = (height() - 2) / 2;
        if (r >= 2) {
            const int16_t cx = xRight + slotW / 2;
            fillCircle(cx, cy, r, BLACK);                       // full disc
            fillCircle(cx + (r * 3) / 5, cy - r / 4, r, WHITE); // carve crescent (opens upper-right)
        }
    }
    if (showGps) {
        xRight -= slotW;
        // GPS reticle: circle + centre dot + four compass ticks crossing the ring
        const int16_t r = (height() - 2) / 2 - 1;
        if (r >= 2) {
            const int16_t cx = xRight + slotW / 2;
            drawCircle(cx, cy, r, BLACK);
            fillCircle(cx, cy, (r >= 5) ? 2 : 1, BLACK);
            drawLine(cx, cy - r - 1, cx, cy - r + 1, BLACK);
            drawLine(cx, cy + r - 1, cx, cy + r + 1, BLACK);
            drawLine(cx - r - 1, cy, cx - r + 1, cy, BLACK);
            drawLine(cx + r - 1, cy, cx + r + 1, cy, BLACK);
        }
    }
    if (showAntenna) {
        xRight -= slotW;
        // External-antenna mast: vertical pole with V arms from the tip, doubled 1px apart
        // for weight at this size
        const int16_t r = (height() - 2) / 2;
        if (r >= 2) {
            const int16_t cx = xRight + slotW / 2;
            const int16_t tipY = cy - r;                       // mast tip
            const int16_t armY = cy - r / 3;                   // where the V arms end
            const int16_t armX = (r * 2) / 3;                  // half-spread of the V
            drawLine(cx, tipY, cx, cy + r, BLACK);             // mast
            drawLine(cx - armX, armY, cx, tipY, BLACK);        // left arm
            drawLine(cx + armX, armY, cx, tipY, BLACK);        // right arm
            drawLine(cx - armX + 1, armY, cx + 1, tipY, BLACK); // thicken arms
            drawLine(cx + armX - 1, armY, cx - 1, tipY, BLACK);
        }
    }
    if (showDebugHold) {
        xRight -= slotW;
        // Debug Hold: an open eye (device deliberately never sleeps) - eye outline as two
        // mirrored chevrons + a filled pupil
        const int16_t r = (height() - 2) / 2;
        if (r >= 2) {
            const int16_t cx = xRight + slotW / 2;
            const int16_t lidH = (r * 2) / 3;                 // eye half-height
            drawLine(cx - r, cy, cx, cy - lidH, BLACK);       // upper lid
            drawLine(cx, cy - lidH, cx + r, cy, BLACK);
            drawLine(cx - r, cy, cx, cy + lidH, BLACK);       // lower lid
            drawLine(cx, cy + lidH, cx + r, cy, BLACK);
            fillCircle(cx, cy, (r >= 5) ? 2 : 1, BLACK);      // pupil
        }
    }
    if (showVibra) {
        xRight -= slotW;
        // Vibration policy bell (hollow outline): plain bell = DMs only; slashed bell = fully muted
        const int16_t r = (height() - 2) / 2;
        if (r >= 3) {
            const int16_t cx = xRight + slotW / 2;
            const int16_t domeW = r - 1;                                           // half-width of the bell mouth
            drawTriangle(cx - domeW, cy + r - 3, cx + domeW, cy + r - 3, cx, cy - r + 1, BLACK); // dome outline
            drawLine(cx - domeW - 1, cy + r - 2, cx + domeW + 1, cy + r - 2, BLACK);             // mouth rim
            fillCircle(cx, cy + r - 1, 1, BLACK);                                                // clapper
            if (tdeckmaxVibraMode == 2) {
                drawLine(cx - r, cy + r, cx + r, cy - r, BLACK); // mute slash, doubled
                drawLine(cx - r + 1, cy + r, cx + r + 1, cy - r, BLACK);
            }
        }
    }
    if (showLock) {
        xRight -= slotW;
        // Padlock: filled body with a white keyhole dot, shackle ring peeking above
        const int16_t r = (height() - 2) / 2;
        if (r >= 3) {
            const int16_t cx = xRight + slotW / 2;
            drawCircle(cx, cy - 2, r - 2, BLACK);                          // shackle (lower half hidden)
            fillRect(cx - (r - 1), cy - 1, (r - 1) * 2 + 1, r + 1, BLACK); // body
            fillCircle(cx, cy + r / 2 - 1, 1, WHITE);                      // keyhole
        }
    }
#else
    // Clear the region beneath the tile, including the border
    // Most applets are drawing onto an empty frame buffer and don't need to do this
    // We do need to do this with the battery though, as it is an "overlay"
    fillRect(0, 0, width(), height(), WHITE);
    drawBatteryGlyph(0, width());
#endif
}

// Battery outline + charge slice, drawn into [left, left+w) at the tile's full height.
// Split out so the T-Deck Max indicator cluster can place it beside the moon / GPS glyphs.
void InkHUD::BatteryIconApplet::drawBatteryGlyph(int16_t left, uint16_t w)
{
    // =====================
    // Draw battery outline
    // =====================

    // Positive terminal "bump"
    constexpr uint16_t bumpW = 2;
    const int16_t bumpL = left + 1;
    const uint16_t bumpH = (height() - 2) / 2;
    const int16_t bumpT = (1 + ((height() - 2) / 2)) - (bumpH / 2);
    fillRect(bumpL, bumpT, bumpW, bumpH, BLACK);

    // Main body of battery
    const int16_t bodyL = left + 1 + bumpW;
    const int16_t &bodyT = 1;
    const int16_t &bodyH = height() - 2;   // Handle top/bottom padding
    const int16_t bodyW = (w - 1) - bumpW; // Handle 1px left pad
    drawRect(bodyL, bodyT, bodyW, bodyH, BLACK);

    // Erase join between bump and body
    drawLine(bodyL, bumpT, bodyL, bumpT + bumpH - 1, WHITE);

    // ===================
    // Draw battery level
    // ===================

    constexpr int16_t slicePad = 2;
    int16_t sliceL = bodyL + slicePad;
    const int16_t sliceT = bodyT + slicePad;
    const uint16_t sliceH = bodyH - (slicePad * 2);
#if defined(MOD_INKHUD_TUNES)
    uint16_t sliceW0 = bodyW - (slicePad * 2);

    uint16_t sliceW = (sliceW0 * socRounded) / 100; // Apply percentage

    hatchRegion(sliceL + sliceW0 - sliceW, sliceT, sliceW, sliceH, 2, BLACK);
    drawRect(sliceL + sliceW0 - sliceW, sliceT, sliceW, sliceH, BLACK);
#else //!defined(MOD_INKHUD_TUNES)
    uint16_t sliceW = bodyW - (slicePad * 2);

    sliceW = (sliceW * socRounded) / 100;          // Apply percentage
    sliceL += ((bodyW - (slicePad * 2)) - sliceW); // Shift slice to the battery's negative terminal, correcting drain direction

    hatchRegion(sliceL, sliceT, sliceW, sliceH, 2, BLACK);
    drawRect(sliceL, sliceT, sliceW, sliceH, BLACK);
#endif //defined(MOD_INKHUD_TUNES)
}

#if defined(T_DECK_MAX)
bool InkHUD::BatteryIconApplet::gpsIndicatorOn()
{
#if !MESHTASTIC_EXCLUDE_GPS && HAS_GPS
    return config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED;
#else
    return false;
#endif
}

// IME alt+L keyboard lock engaged? Shown so a locked keyboard under Debug Hold (device held
// awake, keys silently dropped) is visible instead of feeling like a dead keyboard.
bool InkHUD::BatteryIconApplet::keyboardLocked()
{
#if defined(MOD_INPUT_MENU)
    auto *ime = (InputMenuApplet *)inkhud->getSystemApplet("InputMenu");
    return ime && ime->isKeyboardLocked();
#else
    return false;
#endif
}

uint16_t InkHUD::BatteryIconApplet::occupiedWidth()
{
    // Height-based only (slot width = tile height), so the result is stable while preRender
    // resizes the tile's WIDTH around it.
    uint16_t w = 0;
    if (settings->optionalFeatures.batteryIcon)
        w += batteryGlyphWidth();
    if (tdeckmaxSilentMode) // moon = silent mode
        w += indicatorSlotWidth();
    if (gpsIndicatorOn())
        w += indicatorSlotWidth();
    if (tdeckmaxGetAntennaExternal())
        w += indicatorSlotWidth();
    if (tdeckmaxDebugHold)
        w += indicatorSlotWidth();
    if (tdeckmaxVibraMode != 0)
        w += indicatorSlotWidth();
    if (!inkhudScreenAwake || keyboardLocked()) // lock = keyboard not responding (asleep or alt+L)
        w += indicatorSlotWidth();
    return w;
}

// Runs just before the renderer clears our tile for a partial render: pin the tile's right
// edge to the screen edge and shrink its width to exactly the occupied cluster span. Cleared
// tile area is WHITE (not transparent), so any excess width would stamp a blank block over the
// applet beneath - most visible on full-screen no-header applets like NavMap. Growth is safe
// (the wider clear becomes the cluster's own background); shrink is safe too because the
// renderer only reaches this on updates where the underlying user applet either re-renders or
// keeps its previous frame outside our (now smaller) tile.
void InkHUD::BatteryIconApplet::preRender()
{
    Tile *t = getTile();
    if (!t)
        return;
    const uint16_t screenW = inkhud->width();
    uint16_t occ = occupiedWidth();
    if (occ < 1)
        occ = 1; // keep a degenerate but valid region
    if (occ > screenW)
        occ = screenW;
    t->setRegion(screenW - occ, t->getTop(), occ, t->getHeight());
}
#endif

#endif
