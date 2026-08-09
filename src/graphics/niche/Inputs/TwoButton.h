#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

/*

Re-usable NicheGraphics input source

Short and Long press for up to two buttons
Interrupt driven

*/

#pragma once

#include "configuration.h"

#include "assert.h"
#include "functional"

#ifdef ARCH_ESP32
#include "esp_sleep.h" // For light-sleep handling
#endif

#include "Observer.h"

namespace NicheGraphics::Inputs
{

class TwoButton : protected concurrency::OSThread
{
  public:
    typedef std::function<void()> Callback;

    static uint8_t getUserButtonPin(); // Resolve the GPIO, considering the various possible source of definition

    static TwoButton *getInstance(); // Create or get the singleton instance
    void start();                    // Start handling button input
    void stop();                     // Stop handling button input (disconnect ISRs for sleep)
    void setWiring(uint8_t whichButton, uint8_t pin, bool internalPullup = false);
    void setTiming(uint8_t whichButton, uint32_t debounceMs, uint32_t longpressMs);
    void setHandlerDown(uint8_t whichButton, Callback onDown);
    // Was the current/most-recent press the one that woke the screen? Captured at the ISR
    // (before ANY wake processing runs), so it is immune to wake-path latency - unlike any
    // time-window heuristic. Only meaningful on builds that maintain inkhudScreenAwake.
    bool pressBeganAsleep(uint8_t whichButton) { return buttons[whichButton].pressWhileAsleep; }
    void setHandlerShortPress(uint8_t whichButton, Callback onShortPress);
    void setHandlerLongPress(uint8_t whichButton, Callback onLongPress);

    // Disconnect and reconnect interrupts for light sleep
#ifdef ARCH_ESP32
    int beforeLightSleep(void *unused);
    int afterLightSleep(esp_sleep_wakeup_cause_t cause);
#endif

  private:
    // Internal state of a specific button
    enum State {
        REST,            // Up, no activity
        IRQ,             // Down detected, not yet handled
        POLLING_UNFIRED, // Down handled, polling for release
        POLLING_FIRED,   // Longpress fired, button still held
    };

    // Contains info about a specific button
    // (Array of this struct below)
    class Button
    {
      public:
        // Per-button config
        uint8_t pin = 0xFF;                 // 0xFF: unset
        bool activeLogic = LOW;             // Active LOW by default. Currently unimplemented.
        uint32_t debounceLength = 50;       // Minimum length for shortpress, in ms
        uint32_t longpressLength = 500;     // How long after button down to fire longpress, in ms
        volatile State state = State::REST; // Internal state
        volatile uint32_t irqAtMillis;      // millis() when button went down
        volatile bool pressWhileAsleep = false; // captured in the ISR: was the screen asleep when this press began?
        volatile bool releaseSeen = false;      // ISR saw a physical release edge for the CURRENT press. Poll ticks can
                                                // stall for seconds behind blocking e-ink refreshes; without this, rapid
                                                // clicks merged into one phantom >=2s "hold" (longpress = shutdown!) and
                                                // short releases vanished. A press with releaseSeen can never be a hold.
        volatile uint32_t lastReleaseMs = 0;    // ISR stamp of the most recent release edge, INCLUDING bounce re-stamps.
                                                // A "press" edge within debounceLength of this is contact bounce, not a
                                                // new press: the poll sets state=REST before running onShortPress, so a
                                                // single bounce glitch registered a phantom press whose EVENT_PRESS
                                                // re-woke the FSM right after a deliberate sleep press reached DARK.

        // Per-button event callbacks
        static void noop(){};
        std::function<void()> onDown = noop;
        std::function<void()> onUp = noop;
        std::function<void()> onShortPress = noop;
        std::function<void()> onLongPress = noop;
    };

#ifdef ARCH_ESP32
    // Get notified when lightsleep begins and ends
    CallbackObserver<TwoButton, void *> lsObserver = CallbackObserver<TwoButton, void *>(this, &TwoButton::beforeLightSleep);
    CallbackObserver<TwoButton, esp_sleep_wakeup_cause_t> lsEndObserver =
        CallbackObserver<TwoButton, esp_sleep_wakeup_cause_t>(this, &TwoButton::afterLightSleep);
#endif

    int32_t runOnce() override; // Timer method. Polls for button release

    void startThread(); // Start polling for release
    void stopThread();  // Stop polling for release

    static void isrPrimary();   // Detect start of press
    static void isrSecondary(); // Detect start of press (optional aux button)

    TwoButton(); // Constructor made private: force use of Button::instance()

    // Info about both buttons
    Button buttons[2];
};

}; // namespace NicheGraphics::Inputs

#endif