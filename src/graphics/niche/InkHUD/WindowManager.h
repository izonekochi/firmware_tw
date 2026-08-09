#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Responsible for managing which applets are shown, and their sizes / positions

*/

#pragma once

#include "configuration.h"

#include "./Applets/System/Notification/Notification.h" // The notification object, not the applet
#include "./InkHUD.h"
#include "./Persistence.h"
#include "./Tile.h"

namespace NicheGraphics::InkHUD
{

class WindowManager
{
  public:
    WindowManager();
    void addApplet(const char *name, Applet *a, bool defaultActive, bool defaultAutoshow, uint8_t onTile);
    void begin();

    // - call these to make stuff change

    void nextTile();
    void prevTile();
    bool selectTileAt(uint16_t x, uint16_t y);
    Applet *getActiveApplet();
    void openMenu();
#if defined(MOD_INPUT_MENU)
    void openInputMenu(); // IME driving the focused applet (transient split on 1-tile layouts)
#endif
    void openAlignStick();
    void openAppSwitcher();
    void openKeyboard();
    void closeKeyboard();
    void nextApplet();
    void prevApplet();
#if defined(MOD_INPUT_MENU)
    Tile* getFocusedTile();
#endif //defined(MOD_INPUT_MENU)
    bool showApplet(uint8_t appletIndex);
    void rotate();
    void toggleBatteryIcon();

    // - call these to manifest changes already made to the relevant Persistence::Settings values

    void changeLayout();           // Change tile layout or count
    void changeActivatedApplets(); // Change which applets are activated
    void restoreFromMenuSplit();   // Merge the transient InputMenu 1->2 split back to the real tile count
    bool isMenuSplitActive() const { return menuSplitActive; }

    // - called during the rendering operation

    void autoshow();                     // Show a different applet, to display new info
    std::vector<Tile *> getEmptyTiles(); // Any user tiles without a valid applet

  private:
    // Steps for configuring (or reconfiguring) the window manager
    // - all steps required at startup
    // - various combinations of steps required for on-the-fly reconfiguration (by user, via menu)

    void addSystemApplet(const char *name, SystemApplet *applet, Tile *tile);
    void createSystemApplets(); // Instantiate the system applets
    void placeSystemTiles();    // Assign manual positions to (most) system applets

    void createUserApplets(); // Activate user's selected applets
    void createUserTiles();   // Instantiate enough tiles for user's selected layout
    void assignUserAppletsToTiles();
    void placeUserTiles(); // Automatically place tiles, according to user's layout
    void refocusTile();    // Ensure focused tile has a valid applet

    void findOrphanApplets(); // Find any applets left-behind when layout changes

    std::vector<Tile *> userTiles; // Tiles which can host user applets
    bool keyboardOpen = false;

    // Transient 1->2 tile split while the InputMenu is open over a controllable applet (e.g. NavMap), so the
    // working applet stays visible beside the menu instead of being masked. Merged back when the menu closes.
    // Runtime-only, mirroring keyboardOpen: settings->userTiles.count is bumped to 2 for the duration and
    // restored on close, so the user's real layout preference is never persisted as 2.
    bool menuSplitActive = false;
    uint8_t savedUserTileCount = 1;

    // For convenience
    InkHUD *inkhud = nullptr;
    Persistence::Settings *settings = nullptr;
};

} // namespace NicheGraphics::InkHUD

#endif
