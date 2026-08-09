#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "configuration.h"

#include "graphics/niche/Drivers/Backlight/LatchingBacklight.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/Persistence.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include "graphics/niche/InkHUD/Applets/Bases/Controllable/Controllable.h"

#include "Channels.h"
#include "concurrency/OSThread.h"

#if defined(MOD_I2C_TCA8418_KEYBOARD)
// TCA8418 I2C BBQ10-style keyboard backend (T-Deck Max). The keyboard object is created in the
// board's lateInitVariant() and consumed by InputMenuApplet::runOnce(). Forward-declared here (the
// full header is included in the .cpp) so the board variant can publish the instance without
// pulling the driver into every InkHUD translation unit. Poll-only: KB_IRQ_PIN has no ISR.
class TCA8418KeyboardBase;
extern TCA8418KeyboardBase *inkhudI2CKeyboard;
#endif // defined(MOD_I2C_TCA8418_KEYBOARD)

// Both physical BBQ10-layout keyboards -- the custom UART T-keyboard and the T-Deck Max TCA8418 --
// deliver positional 2-byte (modCode, keyCode) events and share the same mirrored on-screen boards
// and handleMenuTKey/handleBackgroundTKey handlers. MOD_TKEY_MODEL gates that shared model; the
// backend-specific bits (UART transport, KB backlight protocol) stay under their own MOD_ flags.
#if defined(MOD_UART_T_KEYBOARD) || defined(MOD_I2C_TCA8418_KEYBOARD)
#define MOD_TKEY_MODEL
#endif

namespace NicheGraphics::InkHUD {

class Applet;

class InputMenuApplet : public SystemApplet, public concurrency::OSThread {
  public:
    InputMenuApplet();
    void onForeground() override;
    void onBackground() override;
    void onButtonShortPress() override;
    void onButtonLongPress() override;
    // Directional navigation (joystick / rocker / keyboard arrows). InputMenuApplet is the
    // handleInput consumer while foreground, so Events dispatches these to us automatically.
    void onNavUp() override;
    void onNavDown() override;
    void onNavLeft() override;
    void onNavRight() override;
    void onExitShort() override;
    void onExitLong() override;
    // Direct touch on the IME: tap board cells to type (incl. the full bopomofo board), the tab
    // bar to switch boards, the candidate bar to pick, the target list to select. Geometry
    // mirrors onRender. Only reachable on touch-capable builds (Events dispatches touch points
    // to system applets holding handleInput).
    bool onTouchPoint(uint16_t x, uint16_t y, bool longPress) override;
    void onRender(bool full) override;

    void show(Tile *t, Tile *neighborTile = nullptr); // Open the simple input applet, onto a user tile
    bool isKeyboardLocked() const { return touchLocked; } // corner lock indicator (BatteryIconApplet)

    using Key = std::tuple<std::string, int16_t>;
    using Keyboard = std::tuple<std::string, std::vector<std::vector<Key>>>;
    using SendTarget = std::tuple<std::string, uint32_t>;

    // The Controllable applet on the focused tile (nullptr if none). Public so board code
    // (e.g. the T-Deck Max bezel keys) can drive the same handleUp/handleDown scroll path the
    // keyboard's background navigation uses.
    Controllable *getActiveControllable();

  protected:
    std::vector<Keyboard> keyboards;
    std::vector<SendTarget> sendTargets;
    std::string currentInput, currentCIM;
    std::vector<int16_t> currentCIMKeys;
    std::vector<std::string> currentCIMResults;
    int16_t selMode = 0, selKB = -1, selRow = -1, selCol = -1, selResult = -1, selTarget = -1;

    uint32_t autoHideMillis = 0;
    uint32_t comboStartMillis = 0;
    uint8_t comboKeyCode = 0;
    uint8_t comboPressCount = 0;
    bool touchLocked = false;
#if defined(MOD_TKEY_MODEL)
    bool keyboardCIM = false;
#endif //defined(MOD_TKEY_MODEL)
#if defined(MOD_UART_T_KEYBOARD)
    uint8_t currentKBBL = 255; // UART keyboard backlight level (sent over the wire; N/A to TCA8418)
#endif //defined(MOD_UART_T_KEYBOARD)

    Drivers::LatchingBacklight *backlight = nullptr; // Convenient access to the backlight singleton
    
    int32_t runOnce() override;

    // Shared directional-navigation core, used by the button FSM, the onNav*/onExit* handlers,
    // and the UART keyboards. Extracted from onButtonShortPress/onButtonLongPress so the
    // single-button behavior is preserved exactly.
    int16_t &currentCursor();    // active selMode's cursor variable (selKB/selRow/selCol/selResult/selTarget)
    int16_t currentLevelCount(); // number of items in the active selMode's list
    void cursorStep(int delta);  // move active cursor by +/-1 (button cycle / NavDown/NavUp)
    void levelActivate();        // descend a level or commit (long-press when cursor != -1 / NavRight)
    void levelBack();            // step up a level / exit / clear CIM (long-press when cursor == -1 / NavLeft / ExitShort)
    void noteUserActivity();     // re-arm the auto-hide timeout on any user input

#if defined(MOD_UART_KEYBOARD_12KEY)
    void handleMenuVKey(const uint8_t code);
    void handleBackgroundVKey(const uint8_t code);
#elif defined(MOD_TKEY_MODEL)
    void handleMenuTKey(const uint8_t modCode, const uint8_t keyCode);
    void handleBackgroundTKey(const uint8_t modCode, const uint8_t keyCode);
    // Fn-layer funcCode dispatch shared by the physical bAlt keys and the on-screen Fn board; funcCode==14
    // commits currentInput to the neighbour applet (ThreadedMessage broadcast / NavMap goto).
    void dispatchTKeyFunc(int16_t funcCode, Applet *ctrlPtr0, Controllable::Types ctrlType, bool bBorrowed);
    void commitInputToNeighbor(Applet *ctrlPtr0, Controllable::Types ctrlType, bool bBorrowed);
#endif //defined(MOD_TKEY_MODEL)

    void handleKeyboardPress();

    // Shared helpers extracted from the duplicated keyboard-input paths.
    void utf8PopLast(std::string &s);   // remove the final UTF-8 codepoint from s (CIM-aware backspace)
    void populateChannelTargets();      // (re)fill sendTargets from the enabled channels
    void populateFavoriteTargets();     // (re)fill sendTargets from the favorite nodes
    void tilePrevOrApplet();            // "left": focus the previous tile (multi-tile) or previous applet (single tile)
    void tileNextOrApplet();            // "right": focus the next tile (multi-tile) or next applet (single tile)
#if defined(MOD_CJK_ENABLED)
    // Walk bopomofoTable for currentCIMKeys + tone (0..4); on a non-empty result, set selMode=3 and
    // selResult=selResultOnFound. Shared by the on-screen keyboard and the physical-typing paths.
    void lookupBopomofoCandidates(int16_t toneKey, int16_t selResultOnFound);
#endif

    void sendText(NodeNum dest, ChannelIndex channel, const std::string& message); // Send a text message to mesh

    Applet *borrowedTileOwner = nullptr;
    Applet *neighborTileOwner = nullptr;
};

} // namespace NicheGraphics::InkHUD

#endif