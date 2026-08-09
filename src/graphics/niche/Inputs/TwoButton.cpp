#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./TwoButton.h"

#include "NodeDB.h" // For the helper function TwoButton::getUserButtonPin
#include "PowerFSM.h"
#include "sleep.h"

using namespace NicheGraphics::Inputs;

TwoButton::TwoButton() : concurrency::OSThread("TwoButton")
{
    // Don't start polling buttons for release immediately
    // Assume they are in a "released" state at boot
    OSThread::disable();

#ifdef ARCH_ESP32
    // Register callbacks for before and after lightsleep
    lsObserver.observe(&notifyLightSleep);
    lsEndObserver.observe(&notifyLightSleepEnd);
#endif

    // Explicitly initialize these, just to keep cppcheck quiet..
    buttons[0] = Button();
    buttons[1] = Button();
}

// Get access to (or create) the singleton instance of this class
// Accessible inside the ISRs, even though we maybe shouldn't
TwoButton *TwoButton::getInstance()
{
    // Instantiate the class the first time this method is called
    static TwoButton *const singletonInstance = new TwoButton;

    return singletonInstance;
}

// Begin receiving button input
// We probably need to do this after sleep, as well as at boot
void TwoButton::start()
{
    if (buttons[0].pin != 0xFF)
        attachInterrupt(buttons[0].pin, TwoButton::isrPrimary, CHANGE); // both edges: press registers, release latches

    if (buttons[1].pin != 0xFF)
        attachInterrupt(buttons[1].pin, TwoButton::isrSecondary, CHANGE); // both edges: press registers, release latches
}

// Stop receiving button input, and run custom sleep code
// Called before device sleeps. This might be power-off, or just ESP32 light sleep
// Some devices will want to attach interrupts here, for the user button to wake from sleep
void TwoButton::stop()
{
    if (buttons[0].pin != 0xFF)
        detachInterrupt(buttons[0].pin);

    if (buttons[1].pin != 0xFF)
        detachInterrupt(buttons[1].pin);
}

// Attempt to resolve a GPIO pin for the user button, honoring userPrefs.jsonc and device settings
// This helper method isn't used by the TwoButton class itself, it could be moved elsewhere.
// Intention is to pass this value to TwoButton::setWiring in the setupNicheGraphics method.
uint8_t TwoButton::getUserButtonPin()
{
    uint8_t pin = 0xFF; // Unset

    // Use default pin for variant, if no better source
#ifdef BUTTON_PIN
    pin = BUTTON_PIN;
#endif

    // From userPrefs.jsonc, if set
#ifdef USERPREFS_BUTTON_PIN
    pin = USERPREFS_BUTTON_PIN;
#endif

    // From user's override in device settings, if set
    if (config.device.button_gpio)
        pin = config.device.button_gpio;

    return pin;
}

// Configures the wiring and logic of either button
// Called when outlining your NicheGraphics implementation, in variant/nicheGraphics.cpp
void TwoButton::setWiring(uint8_t whichButton, uint8_t pin, bool internalPullup)
{
    // Prevent the same GPIO being assigned to multiple buttons
    // Allows an edge case when the user remaps hardware buttons using device settings, due to a broken user button
    for (uint8_t i = 0; i < whichButton; i++) {
        if (buttons[i].pin == pin) {
            LOG_WARN("Attempted reuse of GPIO %d. Ignoring assignment whichButton=%d", pin, whichButton);
            return;
        }
    }

    assert(whichButton < 2);
    buttons[whichButton].pin = pin;
    buttons[whichButton].activeLogic = LOW; // Unimplemented

    pinMode(buttons[whichButton].pin, internalPullup ? INPUT_PULLUP : INPUT);
}

void TwoButton::setTiming(uint8_t whichButton, uint32_t debounceMs, uint32_t longpressMs)
{
    assert(whichButton < 2);
    buttons[whichButton].debounceLength = debounceMs;
    buttons[whichButton].longpressLength = longpressMs;
}

// Set what should happen when a button becomes pressed
// Use this to implement a "while held" behavior
void TwoButton::setHandlerDown(uint8_t whichButton, Callback onDown)
{
    assert(whichButton < 2);
    buttons[whichButton].onDown = onDown;
}

// Set what should happen when a "short press" event has occurred
void TwoButton::setHandlerShortPress(uint8_t whichButton, Callback onShortPress)
{
    assert(whichButton < 2);
    buttons[whichButton].onShortPress = onShortPress;
}

// Set what should happen when a "long press" event has fired
// Note: this will occur while the button is still held
void TwoButton::setHandlerLongPress(uint8_t whichButton, Callback onLongPress)
{
    assert(whichButton < 2);
    buttons[whichButton].onLongPress = onLongPress;
}

// Handle the start of a press to the primary button
// Wakes our button thread
#if defined(T_DECK_MAX) && defined(MESHTASTIC_INCLUDE_INKHUD)
extern bool inkhudScreenAwake; // InkHUD sleep-UX flag (declared in InkHUD.h); plain read, ISR-safe
#endif

void TwoButton::isrPrimary()
{
    static volatile bool isrRunning = false;

    if (!isrRunning) {
        isrRunning = true;
        TwoButton *b = TwoButton::getInstance();
        if (digitalRead(b->buttons[0].pin) == b->buttons[0].activeLogic) {
            // Press edge. Ignore edges within debounceLength of the last release edge: that's
            // contact bounce, and a bounce registered here becomes a phantom press whose
            // EVENT_PRESS re-wakes the FSM immediately after a deliberate sleep press.
            if (b->buttons[0].state == State::REST &&
                (uint32_t)(millis() - b->buttons[0].lastReleaseMs) >= b->buttons[0].debounceLength) {
#if defined(T_DECK_MAX) && defined(MESHTASTIC_INCLUDE_INKHUD)
                // First registration of this press: capture the sleep state BEFORE any wake
                // processing (PowerFSM's LS-wake EVENT_PRESS runs later, in thread context)
                b->buttons[0].pressWhileAsleep = !inkhudScreenAwake;
#endif
                b->buttons[0].releaseSeen = false;
                b->buttons[0].state = State::IRQ;
                b->buttons[0].irqAtMillis = millis();
                b->startThread();
            }
        } else {
            // Release edge: stamp it (unconditionally - bounce re-stamps extend the guard above
            // through the whole bounce train), and latch it, so a poll thread stalled behind a
            // blocking e-ink refresh still learns the press ENDED (else clicks merge into a
            // phantom hold)
            b->buttons[0].lastReleaseMs = millis();
            if (b->buttons[0].state != State::REST)
                b->buttons[0].releaseSeen = true;
        }
        isrRunning = false;
    }
}

// Handle the start of a press to the secondary button
// Wakes our button thread
void TwoButton::isrSecondary()
{
    static volatile bool isrRunning = false;

    if (!isrRunning) {
        isrRunning = true;
        TwoButton *b = TwoButton::getInstance();
        if (digitalRead(b->buttons[1].pin) == b->buttons[1].activeLogic) {
            // see isrPrimary: bounce guard rejects press edges within debounceLength of a release
            if (b->buttons[1].state == State::REST &&
                (uint32_t)(millis() - b->buttons[1].lastReleaseMs) >= b->buttons[1].debounceLength) {
                b->buttons[1].releaseSeen = false;
                b->buttons[1].state = State::IRQ;
                b->buttons[1].irqAtMillis = millis();
                b->startThread();
            }
        } else {
            b->buttons[1].lastReleaseMs = millis();
            if (b->buttons[1].state != State::REST)
                b->buttons[1].releaseSeen = true; // see isrPrimary: stall-proof release latch
        }
        isrRunning = false;
    }
}

// Concise method to start our button thread
// Follows an ISR, listening for button release
void TwoButton::startThread()
{
    if (!OSThread::enabled) {
        OSThread::setInterval(10);
        OSThread::enabled = true;
    }
}

// Concise method to stop our button thread
// Called when we no longer need to poll for button release
void TwoButton::stopThread()
{
    if (OSThread::enabled) {
        OSThread::disable();
    }

    // Reset both buttons manually
    // Just in case an IRQ fires during the process of resetting the system
    // Can occur with super rapid presses?
    buttons[0].state = REST;
    buttons[1].state = REST;
}

// Our button thread
// Started by an IRQ, on either button
// Polls for button releases
// Stops when both buttons released
int32_t TwoButton::runOnce()
{
    constexpr uint8_t BUTTON_COUNT = sizeof(buttons) / sizeof(Button);

    // Allow either button to request that our thread should continue polling
    bool awaitingRelease = false;

    // Check both primary and secondary buttons
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        switch (buttons[i].state) {
        // No action: button has not been pressed
        case REST:
            break;

        // New press detected by interrupt
        case IRQ:
            powerFSM.trigger(EVENT_PRESS);             // Tell PowerFSM that press occurred (resets sleep timer)
            buttons[i].onDown();                       // Run callback: press has begun (possible hold behavior)
            buttons[i].state = State::POLLING_UNFIRED; // Mark that button-down has been handled
            awaitingRelease = true;                    // Mark that polling-for-release should continue
            break;

        // An existing press continues
        // Not held long enough to register as longpress
        case POLLING_UNFIRED: {
            uint32_t length = millis() - buttons[i].irqAtMillis;

            // Released? Trust the ISR-latched release edge FIRST: poll ticks can stall for
            // seconds behind a blocking e-ink refresh, and the pin may already be DOWN again
            // with the NEXT click by the time we run.
            if (buttons[i].releaseSeen || digitalRead(buttons[i].pin) != buttons[i].activeLogic) {
                bool sawEdge = buttons[i].releaseSeen;
                buttons[i].releaseSeen = false;
                buttons[i].onUp();              // Run callback: press has ended (possible release of a hold)
                buttons[i].state = State::REST; // Mark that the button has reset
                // Short press: for an ISR-latched release the measured length may span the
                // stall, but a physical release DID occur - it cannot have been a 2s hold.
                if (length > buttons[i].debounceLength && (sawEdge || length < buttons[i].longpressLength))
                    buttons[i].onShortPress(); // Run callback: short press
                // If a NEW press is already in progress (its edge was ignored while we were
                // mid-press), register it now so it isn't lost. Bounce guard as in the ISR: a
                // LOW read within debounceLength of the release edge is bounce, not a new press.
                if (digitalRead(buttons[i].pin) == buttons[i].activeLogic &&
                    (uint32_t)(millis() - buttons[i].lastReleaseMs) >= buttons[i].debounceLength) {
                    buttons[i].state = State::IRQ;
                    buttons[i].irqAtMillis = millis();
                    awaitingRelease = true;
                }
            }

            // If button not yet released
            else {
                awaitingRelease = true; // Mark that polling-for-release should continue
                if (length >= buttons[i].longpressLength) {
                    // Run callback: long press (once)
                    // Then continue waiting for release, to rearm
                    buttons[i].state = State::POLLING_FIRED;
                    buttons[i].onLongPress();
                }
            }
            break;
        }

        // Button still held, but duration long enough that longpress event already fired
        // Just waiting for release
        case POLLING_FIRED:
            // Release detected (ISR-latched edge, or live pin level)
            if (buttons[i].releaseSeen || digitalRead(buttons[i].pin) != buttons[i].activeLogic) {
                buttons[i].releaseSeen = false;
                buttons[i].state = State::REST;
                buttons[i].onUp(); // Callback: release of hold (in this case: *after* longpress has fired)
            }
            // Not yet released, keep polling
            else
                awaitingRelease = true;
            break;
        }
    }

    // If both buttons are now released
    // we don't need to waste cpu resources polling
    // IRQ will restart this thread when we next need it
    if (!awaitingRelease)
        stopThread();

    // Run this method again, or don't..
    // Use whatever behavior was previously set by stopThread() or startThread()
    return OSThread::interval;
}

#ifdef ARCH_ESP32

// Detach our class' interrupts before lightsleep
// Allows sleep.cpp to configure its own interrupts, which wake the device on user-button press
int TwoButton::beforeLightSleep(void *unused)
{
    stop();
    return 0; // Indicates success
}

// Reconfigure our interrupts
// Our class' interrupts were disconnected during sleep, to allow the user button to wake the device from sleep
int TwoButton::afterLightSleep(esp_sleep_wakeup_cause_t cause)
{
    start();

    // Manually trigger the button-down ISR
    // - during light sleep, our ISR is disabled
    // - if light sleep ends by button press, pretend our own ISR caught it
    // - need to manually confirm by reading pin ourselves, to avoid occasional false positives
    //   (false positive only when using internal pullup resistors?)
    if (cause == ESP_SLEEP_WAKEUP_GPIO && digitalRead(buttons[0].pin) == buttons[0].activeLogic)
        isrPrimary();

    return 0; // Indicates success
}

#endif

#endif
