#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "configuration.h"

#include "graphics/niche/Drivers/Backlight/LatchingBacklight.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/Persistence.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include "graphics/niche/Utils/CannedMessageStore.h"

#include "./MenuItem.h"
#include "./MenuPage.h"

#include "Channels.h"
#include "concurrency/OSThread.h"

namespace NicheGraphics::InkHUD
{

class Applet;

class MenuApplet : public SystemApplet, public concurrency::OSThread
{
  public:
    MenuApplet();
    void onForeground() override;
    void onBackground() override;
    void onButtonShortPress() override;
    void onButtonLongPress() override;
    void onExitShort() override;
    void onNavUp() override;
    void onNavDown() override;
    void onNavLeft() override;
    void onNavRight() override;
    void onFreeText(char c) override;
    void onFreeTextDone() override;
    void onFreeTextCancel() override;
    bool onTouchPoint(uint16_t x, uint16_t y, bool longPress) override;
    void onRender(bool full) override;

    void show(Tile *t); // Open the menu, onto a user tile
    void setStartPage(MenuPage page);

#if defined(T_DECK_MAX)
    // Keyboard fast-toggle (alt+A, InputMenuApplet background chord): same flip + rail drive +
    // persistence as the Hardware-page "Ext Antenna" item. Returns the NEW state (true = external).
    bool quickToggleAntenna();
    uint8_t quickCycleFrontlight(); // alt+B / Fn "FL": Off -> Low -> Med -> High -> Off; returns new level
    uint8_t quickCycleVibra();      // alt+V / Fn "Vib": All -> DMs only -> Off; returns new mode
    bool quickToggleSilent();       // alt+S / "Silent Mode": no asleep e-ink refresh; returns new state
#endif

    // Re-show the selection highlight after a nav event from a PHYSICAL key (keyboard QWEASD /
    // bezel keys). On touch-first layouts onNavUp/onNavDown hide the highlight (swipes should
    // scroll without a cursor), but key-driven navigation must keep it visible, or W/S presses
    // move the cursor invisibly and E/D execute an unseen selection. Call immediately after
    // onNavUp()/onNavDown(); it lands before the deferred render, so the hide-then-show pair
    // collapses into a single visible-cursor repaint.
    void showCursorHighlight() { hideTouchSelectionHighlight = false; }

  protected:
    Drivers::LatchingBacklight *backlight = nullptr; // Convenient access to the backlight singleton

    int32_t runOnce() override;

    void execute(MenuItem item);  // Perform the MenuAction associated with a MenuItem, if any
    void showPage(MenuPage page); // Load and display a MenuPage

    // Raw cursor -> option-table index on picker pages laid out [Back, header(s), option...].
    // The cursor indexes the full items vector, where section headers OCCUPY SLOTS (navigation
    // merely skips over them), so a bare "cursor - 1" is off by one per header above the
    // options. Counts the selectable items before the cursor instead, minus one for "Back".
    // Returns 0xFF when the cursor sits on Back itself (callers bounds-check against the table).
    uint8_t pickerOptionIndex() const;

    void populateSendPage();           // Dynamically create MenuItems including canned messages
    void populateRecipientPage();      // Dynamically create a page of possible destinations for a canned message
    void populateAppletPage();         // Dynamically create MenuItems for toggling loaded applets
    void populateAutoshowPage();       // Dynamically create MenuItems for selecting which applets can autoshow
    void populateRecentsPage();        // Create menu items: a choice of values for settings.recentlyActiveSeconds
    void populateDisplayTimeoutPage(); // Create menu items for config.display.screen_on_secs

    void drawInputField(uint16_t left, uint16_t top, uint16_t width, uint16_t height,
                        const std::string &text); // Draw input field for free text
    uint16_t getSystemInfoPanelHeight();
    void drawSystemInfoPanel(int16_t left, int16_t top, uint16_t width,
                             uint16_t *height = nullptr);                   // Info panel at top of root menu
    void sendText(NodeNum dest, ChannelIndex channel, const char *message); // Send a text message to mesh
    void freeCannedMessageResources();                                      // Clear MenuApplet's canned message processing data

    MenuPage startPageOverride = MenuPage::ROOT;
    MenuPage currentPage = MenuPage::ROOT;
    MenuPage previousPage = MenuPage::EXIT;
    uint8_t cursor = 0;                       // Which menu item is currently highlighted
    bool cursorShown = false;                 // Is *any* item highlighted? (Root menu: no initial selection)
    bool hideTouchSelectionHighlight = false; // Touch scrolling keeps cursor for paging math, but can hide highlight
    bool freeTextMode = false;
    uint16_t systemInfoPanelHeight = 0; // Need to know before we render
    uint16_t menuTextLimit = 200;

    std::vector<MenuItem> items;               // MenuItems for the current page. Filled by ShowPage
    std::vector<std::string> nodeConfigLabels; // Persistent labels for Node Config pages
    uint8_t selectedChannelIndex = 0;          // Currently selected LoRa channel (Node Config → Radio → Channel)
    bool channelPositionEnabled = false;
    bool gpsEnabled = false;

    // Recents menu checkbox state (derived from settings.recentlyActiveSeconds)
    static constexpr uint8_t RECENTS_COUNT = 6;
    bool recentsSelected[RECENTS_COUNT] = {};
    static constexpr uint8_t DISPLAY_TIMEOUT_COUNT = 7;
    bool displayTimeoutSelected[DISPLAY_TIMEOUT_COUNT] = {};

    // Data for selecting and sending canned messages via the menu
    // Placed into a sub-class for organization only
    class CannedMessages
    {
      public:
        // Share NicheGraphics component
        // Handles loading, getting, setting
        CannedMessageStore *store;

        // One canned message
        // Links the menu item to the true message text
        struct MessageItem {
            std::string label;   // Shown in menu. Prefixed, and UTF-8 chars parsed
            std::string rawText; // The message which will be sent, if this item is selected
        } *selectedMessageItem;

        // One possible destination for a canned message
        // Links the menu item to the intended recipient
        // May represent either broadcast or DM
        struct RecipientItem {
            std::string label; // Shown in menu
            NodeNum dest = NODENUM_BROADCAST;
            uint8_t channelIndex = 0;
        } *selectedRecipientItem;

        // These lists are generated when the menu page is populated
        // Cleared onBackground (when MenuApplet closes)
        std::vector<MessageItem> messageItems;
        std::vector<RecipientItem> recipientItems;

        MessageItem freeTextItem;
    } cm;

    Applet *borrowedTileOwner = nullptr; // Which applet we have temporarily replaced while displaying menu

    bool invertedColors = false;  // Helper to display current state of config.display.displaymode in InkHUD options
    bool keepBacklightOn = false; // Helper to display current backlight latch state in InkHUD options
    bool mapTracing = false;      // Helper: checkbox state for the NavMap "GPS Trace" follow-mode toggle
    NodeNum traceTarget = 0;      // Staged traceroute target (DM/Heard menu -> TRACEROUTE_VIA picker)
    bool openInputAfterClose = false; // MENU_OPEN_INPUT: open the IME once our tile is restored (onBackground)
    Applet *showAppletAfterClose = nullptr; // HEARD_FILTER_PACKETS etc.: swap this user applet onto the
                                            // focused tile once the borrowed owner is restored (onBackground)

#if defined(T_DECK_MAX)
    bool tdmExtAntenna = false; // Helper: checkbox state for the LoRa antenna toggle
    bool tdmQuickSleep = false; // Helper: checkbox state for the quick sleep toggle
    bool tdmCpuFast = false;    // Helper: checkbox state for the CPU 240MHz toggle
    bool tdmRxSniffEco = false; // Helper: checkbox state for the RX sniff-eco toggle
#endif
};

} // namespace NicheGraphics::InkHUD

#endif
