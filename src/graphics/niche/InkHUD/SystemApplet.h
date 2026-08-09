#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

An applet with nonstandard behavior, which will require special handling

For features like the menu, and the battery icon.

*/

#pragma once

#include "configuration.h"

#include "./Applet.h"

namespace NicheGraphics::InkHUD
{

class SystemApplet : public Applet
{
  public:
    // System applets have the right to:

    bool handleInput = false;    // - respond to input from the user button
    bool handleFreeText = false; // - respond to free text input
    bool lockRendering = false;  // - prevent other applets from being rendered during an update
    bool lockRequests = false;   // - prevent other applets from triggering display updates
    bool alwaysRender = false;   // - render every time the screen is updated

    virtual void onReboot() { onShutdown(); } // - handle reboot specially
    virtual void onApplyingChanges() {}

    // Called by the renderer just before this applet's tile is cleared for a partial render.
    // Last chance to adjust the tile geometry: cleared tile area is WHITE, not transparent, so
    // an overlay whose content width varies (BatteryIcon's indicator cluster) resizes its tile
    // here to exactly the span it will paint - anything wider would stamp a blank white block
    // over the applet beneath (visible on full-screen applets like NavMap).
    virtual void preRender() {}

    // Other system applets may take precedence over our own system applet though
    // The order an applet is passed to WindowManager::addSystemApplet determines this hierarchy (added earlier = higher rank)

  private:
    // System applets are always running (active), but may not be visible (foreground)

    void onActivate() override {}
    void onDeactivate() override {}
};

}; // namespace NicheGraphics::InkHUD

#endif
