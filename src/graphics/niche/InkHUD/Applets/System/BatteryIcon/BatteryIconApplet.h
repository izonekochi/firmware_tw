#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

This applet floats top-right, giving a graphical representation of battery remaining
It should be optional, enabled by the on-screen menu

On T-Deck Max it owns the whole top-right indicator cluster, drawn right-aligned in a widened
tile: [lock][bell][eye][mast][GPS][moon][battery]. The battery keeps the corner (gated by
optionalFeatures.batteryIcon, now drawn asleep AND awake); the PADLOCK appears while the
keyboard won't respond - screen asleep (shared inkhudScreenAwake flag, kept in sync by
Events::onScreenPower) or the alt+L lock; the crescent MOON marks silent mode (tdeckmaxSilentMode:
no e-ink refresh at all while asleep); the GPS reticle appears while config.position.gps_mode
is ENABLED.
Unoccupied tile width is left untouched (transparent), and Applet::drawHeader asks
occupiedWidth() so header text dithers out exactly at the cluster's true left edge. For all
this the applet is always foreground on that target.

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/SystemApplet.h"

#include "PowerStatus.h"

namespace NicheGraphics::InkHUD
{

class BatteryIconApplet : public SystemApplet
{
  public:
    BatteryIconApplet();

    void onRender(bool full) override;
    int onPowerStatusUpdate(const meshtastic::Status *status); // Called when new info about battery is available

#if defined(T_DECK_MAX)
    // Width (px, measured from the tile's RIGHT edge) the indicator cluster actually occupies
    // this render: battery + moon + GPS + antenna + debug-eye, whichever are visible. Used by
    // Applet::drawHeader to dither header text out at the true edge, and by preRender to size
    // the tile itself.
    uint16_t occupiedWidth();

    // Fit the tile to the occupied span before the renderer clears it (cleared area is white,
    // not transparent - a fixed-width tile stamped a blank block over full-screen applets).
    void preRender() override;
#endif

  private:
    void drawBatteryGlyph(int16_t left, uint16_t w); // outline + charge slice, at x-offset `left`
#if defined(T_DECK_MAX)
    static bool gpsIndicatorOn();                                                    // config says GPS powered
    bool keyboardLocked();                                                           // IME's alt+L lock engaged?
    uint16_t batteryGlyphWidth() { return (uint16_t)(((height() - 2) * 9) / 5) + 1; } // matches WindowManager sizing
    uint16_t indicatorSlotWidth() { return height(); }                                // square slots for moon / GPS
#endif
    // Get informed when new information about the battery is available (via onPowerStatusUpdate method)
    CallbackObserver<BatteryIconApplet, const meshtastic::Status *> powerStatusObserver =
        CallbackObserver<BatteryIconApplet, const meshtastic::Status *>(this, &BatteryIconApplet::onPowerStatusUpdate);

    uint8_t socRounded = 0; // Battery state of charge, rounded to nearest 10%
};

} // namespace NicheGraphics::InkHUD

#endif