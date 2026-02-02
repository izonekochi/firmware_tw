#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "configuration.h"

#include "graphics/niche/Drivers/Backlight/LatchingBacklight.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/Persistence.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include "graphics/niche/InkHUD/Applets/Bases/Controllable/Controllable.h"

#include "Channels.h"
#include "concurrency/OSThread.h"

namespace NicheGraphics::InkHUD {

class Applet;

class InputMenuApplet : public SystemApplet, public concurrency::OSThread {
  public:
    InputMenuApplet();
    void onForeground() override;
    void onBackground() override;
    void onButtonShortPress() override;
    void onButtonLongPress() override;
    void onRender(bool full) override;

    void show(Tile *t, Tile *neighborTile = nullptr); // Open the simple input applet, onto a user tile

    using Key = std::tuple<std::string, int16_t>;
    using Keyboard = std::tuple<std::string, std::vector<std::vector<Key>>>;
    using SendTarget = std::tuple<std::string, uint32_t>;

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
#if defined(MOD_UART_T_KEYBOARD)
    bool keyboardCIM = false;
    uint8_t currentKBBL = 255;
#endif //defined(MOD_UART_T_KEYBOARD)

    Drivers::LatchingBacklight *backlight = nullptr; // Convenient access to the backlight singleton
    
    int32_t runOnce() override;

    Controllable* getActiveControllable();

#if defined(MOD_UART_KEYBOARD_12KEY)
    void handleMenuVKey(const uint8_t code);
    void handleBackgroundVKey(const uint8_t code);
#elif defined(MOD_UART_T_KEYBOARD)
    void handleMenuTKey(const uint8_t modCode, const uint8_t keyCode);
    void handleBackgroundTKey(const uint8_t modCode, const uint8_t keyCode);
#endif //defined(MOD_UART_T_KEYBOARD)

    void handleKeyboardPress();

    void sendText(NodeNum dest, ChannelIndex channel, const std::string& message); // Send a text message to mesh

    Applet *borrowedTileOwner = nullptr;
    Applet *neighborTileOwner = nullptr;
};

} // namespace NicheGraphics::InkHUD

#endif