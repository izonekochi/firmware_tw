#include "configuration.h"

#ifdef T_DECK_MAX

#include "AudioBoard.h"
#include "ExtensionIOXL9555.hpp"
#include "HynTouch.h"
#include "NodeDB.h" // for the global `config` (GPS rail policy)
#include "PowerFSM.h" // sleep-UX: EVENT_INPUT to re-stamp the ON timer on bezel/touch activity
#include "HWCDC.h"    // HWCDC::isPlugged(): USB-HOST-present gate for the serial-log hold-awake
#include "PowerStatus.h" // powerStatus->getHasUSB(): VBUS presence for the USB enumeration grace
#include "concurrency/OSThread.h"
#include "input/InputBroker.h"
#include "mesh/RadioLibInterface.h" // receiver reset (AGC) after the antenna mux switch
#include "input/TouchScreenImpl1.h"
#include "sleep.h"
#include <Wire.h>

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/Persistence.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include "graphics/niche/Inputs/TwoButton.h" // pressBeganAsleep: ISR-captured waking-press identity
#endif

#if defined(MOD_I2C_TCA8418_KEYBOARD) || defined(MOD_INPUT_MENU)
// Standalone TCA8418 QWERTY -> InkHUD InputMenuApplet (InputBroker is excluded in InkHUD
// builds). InputMenuApplet.h publishes the extern instance + IRQ latch consumed here, and
// getActiveControllable() used by the bezel keys' up/down scroll routing.
#include "graphics/niche/InkHUD/Applets/System/InputMenu/InputMenuApplet.h"
#endif
#if defined(MOD_I2C_TCA8418_KEYBOARD)
#include "input/TDeckMaxTKeyboard.h"
#endif

extern ExtensionIOXL9555 io;

// Power-button long-press shutdown. Declared in main.h, which must NOT be included here: it drags
// in graphics/Screen.h -> GpioLogic.h, whose `class GpioPin` collides with the audio-driver
// library's `typedef int16_t GpioPin` (API_SPI.h) and breaks the BaseUI t-deck-max build (the
// InkHUD build masks the clash via MESHTASTIC_EXCLUDE_SCREEN). Declare the one symbol we use.
extern uint32_t shutdownAtMsec;

DriverPins PinsAudioBoardES8311;
AudioBoard board(AudioDriverES8311, PinsAudioBoardES8311);

// --- Stage 6: persisted hardware toggle runtime state -----------------------------------
// Touchscreen is menu-toggleable and defaults OFF on the InkHUD product; on the BaseUI
// diagnostics build there is no menu to flip it, so keep it usable there. The 3 bezel keys
// come in via the separate hyn_touch_set_key_callback path (BaseUI only -- the InkHUD build
// leaves them unwired) and are never gated here.
#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
static bool s_touchEnabled = false; // InkHUD: default OFF (user decision), toggled via system menu
#else
static bool s_touchEnabled = true; // BaseUI diagnostics: touch always usable
#endif
// Current e-ink frontlight PWM level (GPIO41). Authority lives here so the light-sleep
// observer can force it to 0 and restore it on wake.
static uint8_t s_frontlightLevel = 0;
// Quick sleep (idle -> immediate light sleep) enable. Menu-toggleable, persisted in
// TDeckMaxPrefs; default ON. Read by TDeckMaxIdleSleepThread below.
static bool s_quickSleepEnabled = true;

static bool readTouch(int16_t *x, int16_t *y)
{
    int16_t xArray[1] = {0};
    int16_t yArray[1] = {0};

    // ALWAYS perform the controller read, even when the touchscreen is disabled: it keeps the
    // CST3xx report pipeline drained, and on the BaseUI build it also dispatches the 3 bezel key
    // events (hyn_touch_get_point() -> handle_key_report() -> touchKeyCallback). The InkHUD build
    // registers no key callback (bezel unwired -- mistouch-prone), so key reports are dropped
    // there. We never hold T_RST / power the CST3xx down either.
    const uint8_t count = hyn_touch_get_point(xArray, yArray, 1);

    // Coordinate / gesture gate. When touch is disabled we simply drop the on-screen point so
    // no touchTap/gesture reaches InkHUD; the bezel keys were already handled by the read above.
    if (count == 0 || !s_touchEnabled) {
        return false;
    }

    *x = xArray[0];
    *y = yArray[0];
    return true;
}

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
// InkHUD build: the touchscreen AND the 3 capacitive bezel keys are DELIBERATELY DISABLED.
// The bezel pads sit exactly where fingers wrap the case and were constantly mistouched; their
// functions moved to the keyboard: R / T / Y = the old left / middle / right SHORT press
// (up / select / down), alt+R / alt+T / alt+Y = the old LONG press (layout toggle / open menu /
// focus toggle) -- see the MOD_I2C_TCA8418_KEYBOARD bezel-replacement block in
// InputMenuApplet::handleBackgroundTKey. The CST328 itself is held in RESET by lateInitVariant()
// (keyboard-only product decision; the chip has no power rail, and active scan cost 2-3mA in the
// power sweep), which also removes the former TouchInkHUDBridge and touch polling entirely.
#else  // BaseUI build: keep injecting InputBroker events (the default keyboard/nav path).
static input_broker_event touchKeyToEvent(uint8_t keyId)
{
    // Vendor firmware reports the three bezel keys left-to-right as heart, circle, paper airplane.
    switch (keyId) {
    case 0:
        return INPUT_BROKER_USER_PRESS;
    case 1:
        return INPUT_BROKER_SELECT;
    case 2:
        return INPUT_BROKER_SEND_PING;
    default:
        return INPUT_BROKER_NONE;
    }
}

static void touchKeyCallback(uint8_t keyId, bool pressed, void *userData)
{
    (void)userData;
    if (!pressed || !inputBroker) {
        return;
    }

    InputEvent event = {
        .source = "touchkeys",
        .inputEvent = touchKeyToEvent(keyId),
        .kbchar = 0,
        .touchX = 0,
        .touchY = 0,
    };
    if (event.inputEvent != INPUT_BROKER_NONE) {
        inputBroker->injectInputEvent(&event);
        LOG_DEBUG("T-Deck Max touch key %u pressed", keyId);
    }
}
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS

// NOTE: KB_IRQ_PIN (GPIO15, TCA8418 INT) must NEVER have an attachInterrupt() handler.
// doLightSleep's gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL) rewrites the pin's interrupt
// TYPE register (and gpio_wakeup_disable does not restore it), so after the first armed nap
// a FALLING handler silently becomes LEVEL-LOW - on a line the TCA8418 holds LOW until a
// thread drains the FIFO. The ISR can't release it -> CPU1 storms in gpio_isr_loop -> IWDT
// panic; and the rst:0xc CPU-only reset preserves both the GPIO config and the chip's
// asserted INT, boot-looping the device (seen on hardware 2026-08-04). Input relies on the
// InputMenuApplet poll alone; the INT line is used only as a light-sleep wake source.

#ifdef ARCH_ESP32
struct TDeckMaxTouchLightSleepObserver {
    int onLightSleep(void *)
    {
        hyn_touch_before_light_sleep();
        return 0;
    }

    int onLightSleepEnd(esp_sleep_wakeup_cause_t)
    {
        hyn_touch_after_light_sleep();
        return 0;
    }

    CallbackObserver<TDeckMaxTouchLightSleepObserver, void *> sleepObserver{this, &TDeckMaxTouchLightSleepObserver::onLightSleep};
    CallbackObserver<TDeckMaxTouchLightSleepObserver, esp_sleep_wakeup_cause_t> wakeObserver{
        this, &TDeckMaxTouchLightSleepObserver::onLightSleepEnd};
} static touchLightSleepObserver;
#endif

#if defined(ARCH_ESP32) && defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
// Force the e-ink frontlight fully OFF while in light sleep (it would otherwise keep drawing
// current for nothing), then restore the user's chosen level on wake. InkHUD product build
// only: on BaseUI the frontlight is owned by Screen.cpp's display on/off path, so we must not
// fight it there.
struct TDeckMaxFrontlightLightSleepObserver {
    int onLightSleep(void *)
    {
        analogWrite(PIN_EINK_BL, 0);
        return 0;
    }

    int onLightSleepEnd(esp_sleep_wakeup_cause_t)
    {
        if (s_frontlightLevel > 0)
            analogWrite(PIN_EINK_BL, s_frontlightLevel);
        return 0;
    }

    CallbackObserver<TDeckMaxFrontlightLightSleepObserver, void *> sleepObserver{
        this, &TDeckMaxFrontlightLightSleepObserver::onLightSleep};
    CallbackObserver<TDeckMaxFrontlightLightSleepObserver, esp_sleep_wakeup_cause_t> wakeObserver{
        this, &TDeckMaxFrontlightLightSleepObserver::onLightSleepEnd};
} static frontlightLightSleepObserver;
#endif

void tDeckMaxSetAudioAmp(bool enable)
{
    io.digitalWrite(EXPANDS_AUDIO_SEL, LOW);
    io.digitalWrite(EXPANDS_AMP_EN, enable ? HIGH : LOW);
}

// --- Stage 6: persisted hardware toggle hooks -------------------------------------------
// Called from the InkHUD system menu (MenuApplet.cpp) on load (apply persisted value at boot)
// and on each user toggle. Also defined for the BaseUI build (harmless; simply unused there).

// LoRa antenna mux on XL9555 P04. Vendor pinmap: HIGH = internal, LOW = external.
static bool s_antennaExternal = false; // mirrors the last mux write (earlyInitVariant boots HIGH = internal)
void tdeckmaxSetAntennaExternal(bool external)
{
    io.pinMode(EXPANDS_LORA_SEL, OUTPUT);
    io.digitalWrite(EXPANDS_LORA_SEL, external ? LOW : HIGH);
    s_antennaExternal = external;
    LOG_INFO("T-Deck Max LoRa antenna -> %s (XL9555 P04 %s)", external ? "external" : "internal", external ? "LOW" : "HIGH");
    // Force a receiver reset after the mux switch: the SX1262's AGC settled against the OLD
    // antenna's signal level and would otherwise keep that gain state (a stuck-desensed RX after
    // switching to the better antenna, or vice versa). reconfigure() = standby -> reprogram all
    // modem parameters -> restart receive, which restarts the AGC from scratch (same call the
    // admin path uses for live LoRa config changes). Null at boot: MenuApplet's ctor applies the
    // persisted pref before radio init, and the fresh radio init programs everything anyway.
    if (RadioLibInterface::instance)
        RadioLibInterface::instance->reconfigure();
}

// Read-side for the InkHUD corner indicator (BatteryIconApplet): which antenna is selected NOW.
bool tdeckmaxGetAntennaExternal()
{
    return s_antennaExternal;
}

// E-ink frontlight PWM level (GPIO41). 0 = off.
void tdeckmaxSetFrontlight(uint8_t level)
{
    s_frontlightLevel = level;
    analogWrite(PIN_EINK_BL, s_frontlightLevel);
    LOG_INFO("T-Deck Max frontlight -> %u", (unsigned)s_frontlightLevel);
}

// Runtime enable for the coordinate/gesture touch path (see readTouch). Bezel keys unaffected.
void tdeckmaxSetTouchEnabled(bool enabled)
{
    s_touchEnabled = enabled;
    LOG_INFO("T-Deck Max touchscreen %s", enabled ? "ENABLED" : "disabled");
}

// InkHUD TouchEnabledProvider (see nicheGraphics.h): reflects the persisted Hardware-menu toggle.
bool tdeckmaxIsTouchEnabled()
{
    return s_touchEnabled;
}

// Quick sleep on/off (idle watchdog below). Off = stock behavior (full screen_on_secs dwell).
void tdeckmaxSetQuickSleep(bool enabled)
{
    s_quickSleepEnabled = enabled;
    LOG_INFO("T-Deck Max quick sleep %s", enabled ? "ENABLED" : "disabled");
}

// CPU frequency experiment toggle (menu "CPU 240MHz"): race-to-sleep A/B test. 80MHz is the
// boot default (main.cpp setCPUFast(false)); 240MHz roughly doubles CPU power but shortens the
// CPU-bound part of each packet nap. main.cpp's setCPUFast(false) runs AFTER MenuApplet's ctor
// applies the persisted pref, so the idle watchdog below re-asserts the choice once at startup.
// Debug Hold (variant.h): session-only stay-awake + fast Info refresh, toggled from the menu
bool tdeckmaxDebugHold = false;
// Vibration buzz policy (variant.h): 0 all / 1 DMs only / 2 off
uint8_t tdeckmaxVibraMode = 0;
// Silent mode (variant.h): while asleep, drop ALL e-ink refreshes; messages are only recorded
bool tdeckmaxSilentMode = false;
// NavMap GPS session hold (variant.h): stay-awake while locate/trace runs
bool tdeckmaxGpsSessionHold = false;

static bool s_cpuFast = false;
static bool s_cpuFastReasserted = false;

// Boot-time pref restore WITHOUT switching the clock mid-setup. MenuApplet's ctor used to call
// tdeckmaxSetCpuFast(true) directly, flipping to 240MHz in the middle of setup() - right before
// the first synchronous e-ink FULL render - which is the prime suspect for the field boot-loop
// (interrupt WDT during "Updating display", first-ever 240MHz boots after the prefs v5 reset).
// Now boot runs at the safe default clock; the idle watchdog's existing one-shot re-assert
// (s_cpuFastReasserted below) applies the choice a few seconds later, after setup() completes -
// the same deferred path that already existed to survive main.cpp's setCPUFast(false) clobber.
void tdeckmaxSetCpuFastBootPref(bool fast)
{
    s_cpuFast = fast;
    if (fast)
        LOG_INFO("T-Deck Max CPU 240MHz (deferred until after setup)");
}

void tdeckmaxSetCpuFast(bool fast)
{
    s_cpuFast = fast;
    setCpuFrequencyMhz(fast ? 240 : 80);
    LOG_INFO("T-Deck Max CPU %uMHz", (unsigned)getCpuFrequencyMhz());
}

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
// --- Phone-style power button (BOOT / GPIO0) ------------------------------------------------
// Wired in nicheGraphics.h: short press = wake / sleep toggle, 2s hold = graceful shutdown.
// The InkHUD "user button" role (select/advance + menu) lives on the middle bezel key instead.
//
// Waking needs no code here: the TwoButton driver fires EVENT_PRESS on every button-down (and
// PowerFSM's wake handler covers a press that ended a true light sleep), which is what lifts
// LS/DARK back to stateON. The short-press callback lands on RELEASE, after that wake has
// already flipped inkhudScreenAwake — so a fresh-wake grace window is what stops the waking
// press from instantly putting the device back to sleep.

static uint32_t s_lastScreenWakeMs = 0; // asleep->awake edge stamp (USB enum grace + waking-press detection)

// Stamp the asleep->awake edge (observes notifyScreenPower, same source InkHUD's Events uses)
class TDeckMaxScreenWakeStamp
{
  public:
    int onScreenPower(bool awake)
    {
        static bool wasAwake = true;
        if (awake && !wasAwake) {
            s_lastScreenWakeMs = millis();
            // Interactive-nap window: a BOOT wake grants TDECKMAX_INTERACTIVE_WINDOW_MS of
            // keyboard-wakeable napping (each keystroke recounts it, see InputMenuApplet)
            tdeckMaxKbWakeUntilMs = millis() + TDECKMAX_INTERACTIVE_WINDOW_MS;
#if defined(MOD_I2C_TCA8418_KEYBOARD)
            // Window opens: let the TCA8418 assert INT again (KE_IEN on)
            if (inkhudI2CKeyboard)
                static_cast<TDeckMaxTKeyboard *>(inkhudI2CKeyboard)->setKeyEventInterrupt(true);
#endif
        }
        if (!awake) {
            tdeckMaxKbWakeUntilMs = 0; // moon shown = window closed, keyboard wake disarmed
#if defined(MOD_I2C_TCA8418_KEYBOARD)
            // Chip-level second barrier: with KE_IEN off the INT line physically cannot assert
            // outside the window (pocket keys also stop tugging the KB_IRQ_PIN pull-up low).
            // Key CAPTURE continues (FIFO needs no IEN); the asleep drain-and-discard is untouched.
            if (inkhudI2CKeyboard)
                static_cast<TDeckMaxTKeyboard *>(inkhudI2CKeyboard)->setKeyEventInterrupt(false);
#endif
        }
        wasAwake = awake;
        return 0;
    }
    CallbackObserver<TDeckMaxScreenWakeStamp, bool> observer{this, &TDeckMaxScreenWakeStamp::onScreenPower};
} static screenWakeStamp;

// Set at the press-DOWN dispatch (setHandlerDown): did THIS press wake the device? The truth is
// captured in the TwoButton ISR itself (Button::pressWhileAsleep) - the only point guaranteed
// to run before ANY wake processing. Two prior heuristics both failed in the field:
// - a 1s time grace also swallowed a genuine sleep press made shortly after waking
//   ("queued keystrokes" bug);
// - a 50ms wake-edge fingerprint missed the light-sleep path, where PowerFSM's LS-wake handler
//   fires EVENT_PRESS (stamping the wake edge) hundreds of ms before the TwoButton thread
//   dispatches onDown - the waking press then read as a sleep press and instantly re-slept
//   ("phantom double click" bug).
static bool s_pressWokeDevice = false;

void tdeckmaxPowerButtonDown()
{
    s_pressWokeDevice = NicheGraphics::Inputs::TwoButton::getInstance()->pressBeganAsleep(0);
}

void tdeckmaxPowerButtonShort()
{
    if (!inkhudScreenAwake)
        return; // asleep: the press itself already woke us via EVENT_PRESS; nothing more to do

    if (s_pressWokeDevice)
        return; // this release belongs to the press that woke the device - never re-sleep on it

    LOG_INFO("T-Deck Max power button: sleep");
    tdeckMaxKbWakeUntilMs = 0; // manual sleep = full sleep NOW: close the interactive-nap window
                               // so onScreenTimeout stamps the moon instead of skipping it
    powerFSM.trigger(EVENT_IDLE_TO_SLEEP); // same ON->sleep path as the quick-sleep idle watchdog
}

void tdeckmaxPowerButtonLong()
{
    // Fires while still held (2s, see setTiming in nicheGraphics.h), from awake or mid-wake.
    // Graceful shutdown: saves state and shows the shutdown screen, same as the menu "off".
    LOG_INFO("T-Deck Max power button: shutdown");
    shutdownAtMsec = millis();
}
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#if defined(ARCH_ESP32) && defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
// --- Quick sleep idle watchdog ------------------------------------------------------------
// Power saving: when the device is awake (stateON) but idle - no key/bezel/BOOT activity for
// TDECKMAX_QUICK_SLEEP_IDLE_MS, no display update queued or running, no system applet (menu /
// IME composer) holding focus, frontlight off - fire EVENT_IDLE_TO_SLEEP to cut the
// screen_on_secs dwell short. The FSM only accepts the event in stateON (transition defined
// nowhere else), so SERIAL / POWER / DARK / LS are naturally unaffected. The e-ink panel keeps
// its image through light sleep, so the user can keep reading; the cost is that the next
// interaction needs a BOOT press first (keyboard/touch are deliberately not wake sources).
// Deferring while a system applet has focus is what keeps typing and menu sessions alive:
// those fall back to the normal screen_on_secs timeout, which each key press re-stamps.
class TDeckMaxIdleSleepThread : public concurrency::OSThread
{
  public:
    TDeckMaxIdleSleepThread() : OSThread("TDMIdleSleep") {}

  protected:
    int32_t runOnce() override
    {
        constexpr int32_t POLL_MS = 500;

        // One-shot: re-assert the persisted CPU-frequency choice. MenuApplet's ctor applied it
        // during InkHUD setup, but main.cpp's setCPUFast(false) runs later in setup() and would
        // clobber a 240MHz selection. This thread's first pass runs after setup() completes.
        if (!s_cpuFastReasserted) {
            s_cpuFastReasserted = true;
            if (s_cpuFast)
                tdeckmaxSetCpuFast(true);
        }

        // USB-HOST hold-awake (bench use): light sleep clock-gates the ESP32-S3's USB-Serial-JTAG,
        // so the moment the FSM reaches LS an attached serial monitor's COM port dies mid-stream
        // and every log line after that is lost. While a USB HOST is enumerated and the screen is
        // awake, re-stamp the FSM's screen timer (EVENT_INPUT is a self-transition in both stateON
        // and statePOWER, restarting the screen_on_secs timed transition) so neither the 30s
        // screensaver dwell nor the quick-sleep watchdog below can reach light sleep: PC attached
        // = continuous log. Placed BEFORE the s_quickSleepEnabled gate on purpose - the hold must
        // also cover the natural screen_on_secs path when the quick-sleep toggle is off.
        // Gate = HWCDC::isPlugged(), an SOF-liveness check that is true ONLY with an enumerated
        // USB host - NOT raw VBUS. Charger-only sources (wall adapter, the Qi receiver on the
        // VBUS net) never send SOF, so the device quick-sleeps normally and CHARGES ASLEEP
        // (e-ink keeps its image; SY6970 needs no CPU). A deliberate power-button sleep still
        // works (once asleep we stop re-stamping), and attaching a host to a sleeping device
        // does not wake it. isPlugged() lags real plug/unplug by a few ms only - instant next
        // to this 500ms poll (the old getHasUSB gate had up-to-20s SY6970 poll latency).
        if (inkhudScreenAwake && HWCDC::isPlugged()) {
            powerFSM.trigger(EVENT_INPUT);
            return POLL_MS;
        }

        // Debug Hold (menu "Hardware" -> "Debug Hold"): same mechanics as the USB hold above -
        // re-stamp the screen timer every poll so neither the screensaver dwell nor quick sleep
        // can reach light sleep, and the interactive-nap expiry below never stamps the moon.
        // Session-only; a manual power-button sleep still wins (once asleep we stop stamping).
        if (inkhudScreenAwake && tdeckmaxDebugHold) {
            powerFSM.trigger(EVENT_INPUT);
            return POLL_MS;
        }

        // NavMap locate/trace session (see variant.h): same mechanics as the holds above.
        // GPS acquisition needs the CPU awake to pump NMEA, and trace mode needs live
        // renders; the session flag replaces NavMap's old per-tick EVENT_INPUT (which forced
        // an ON<->sleep oscillation with a synchronous full-panel render inside the FSM
        // transition - the probable trace crash). A manual power-button sleep still wins.
        if (inkhudScreenAwake && tdeckmaxGpsSessionHold) {
            powerFSM.trigger(EVENT_INPUT);
            return POLL_MS;
        }

        // USB enumeration grace: freshly woken (BOOT press; s_lastScreenWakeMs stamps the
        // asleep->awake edge) with VBUS present -> hold solidly awake for a few seconds so the
        // host can (re)enumerate the USB-Serial-JTAG at all. Napping detaches the port, so a PC
        // plugged into a SLEEPING device never enumerates and isPlugged() above can never become
        // true - the chicken-and-egg that made "plug then wake" unmonitorable/unflashable while
        // "wake then plug" worked. Once SOF flows, the isPlugged hold above takes over; on a
        // dumb charger (wall/Qi) nothing enumerates and naps resume when the grace lapses.
        // Cold boot: s_lastScreenWakeMs is 0, so the grace also covers the first seconds of
        // uptime - a freshly flashed/booted device on USB enumerates before its first nap.
        if (inkhudScreenAwake && powerStatus && powerStatus->getHasUSB() &&
            (uint32_t)(millis() - s_lastScreenWakeMs) < TDECKMAX_USB_ENUM_GRACE_MS) {
            powerFSM.trigger(EVENT_INPUT);
            return POLL_MS;
        }

        // Interactive-nap expiry: the screen stayed logically ON through the nap window (no
        // moon, keyboard armed as a wake source; onScreenTimeout skipped the indicator). Once
        // the window lapses, stamp the sleep indicator and close the window - after which
        // doLightSleep stops arming the keyboard and input goes inert, the normal asleep state.
        // Deferred while a system applet (menu / IME) holds focus or a render is pending,
        // mirroring the quick-sleep guards below. Runs regardless of the quick-sleep toggle:
        // the window model owns the moon once a window has been granted.
        if (inkhudScreenAwake && tdeckMaxKbWakeUntilMs != 0 && (int32_t)(millis() - tdeckMaxKbWakeUntilMs) >= 0) {
            auto *ih = NicheGraphics::InkHUD::InkHUD::getInstance();
            bool defer = (ih == nullptr) || ih->updatePending();
            if (!defer) {
                for (NicheGraphics::InkHUD::SystemApplet *sa : ih->systemApplets) {
                    if (sa->handleInput) {
                        defer = true;
                        break;
                    }
                }
            }
            if (!defer) {
                tdeckMaxKbWakeUntilMs = 0;
                LOG_DEBUG("T-Deck Max interactive-nap window lapsed: stamping sleep indicator");
                notifyScreenPower.notifyObservers(false); // moon + input gate (Events::onScreenPower)
                return POLL_MS;
            }
        }

        if (!s_quickSleepEnabled || !inkhudScreenAwake)
            return POLL_MS;

        // Frontlight on = the user deliberately lit the panel to read in the dark; sleeping
        // would cut it. Let the normal screen timeout handle that session.
        if (s_frontlightLevel > 0)
            return POLL_MS;

        auto *inkhud = NicheGraphics::InkHUD::InkHUD::getInstance();
        if (!inkhud)
            return POLL_MS;

        // Menu open or IME composing: never quick-sleep out from under an interactive session.
        for (NicheGraphics::InkHUD::SystemApplet *sa : inkhud->systemApplets) {
            if (sa->handleInput)
                return POLL_MS;
        }

        // "Not currently busy updating the screen": a queued render (e.g. a message applet
        // autoshow that just landed) gets to reach the glass before we settle to sleep.
        if (inkhud->updatePending())
            return POLL_MS;

#if TDECKMAX_QUICK_SLEEP_IDLE_MS > 0
        // (Guarded: with the interactive-nap design the idle threshold is 0 - nap immediately -
        // and an unsigned `x < 0` comparison would be tautologically false anyway.)
        if ((uint32_t)(millis() - tdeckMaxLastWakeActivityMs) < TDECKMAX_QUICK_SLEEP_IDLE_MS)
            return POLL_MS;
#endif

        LOG_DEBUG("T-Deck Max quick sleep: idle %ums, settling to sleep", (unsigned)TDECKMAX_QUICK_SLEEP_IDLE_MS);
        powerFSM.trigger(EVENT_IDLE_TO_SLEEP);
        return POLL_MS;
    }
};
#endif // ARCH_ESP32 && MESHTASTIC_INCLUDE_NICHE_GRAPHICS

// GPS UART TX back-power guard. The on-device power sweep measured ~22mA flowing from the
// idle-HIGH UART TX (GPS_TX_PIN / IO16) into the UNPOWERED MIA-M10Q's input-protection network
// -- MORE than the powered, acquiring module draws (~17mA). This was both the "GPS on uses less
// power than GPS off" anomaly and a large slice of the 1-day-battery result: the normal
// gps_mode==DISABLED boot state left the GPS driver's UART parked with TX high into a dead
// module, continuously.
//
// Whenever the rail goes DOWN, the TX pin must stop sourcing: grab it off the UART matrix as a
// driven-LOW GPIO (u-blox integration manual: no pin may exceed VCC+0.3V on an unpowered module;
// LOW and Hi-Z measured identical at 36mA system draw, LOW is the deterministic choice). When the
// rail comes back UP, hand the pin back to the GPS UART: setPins() re-routes the GPIO matrix and
// is documented safe before or after begin(); on a fresh ENABLED boot the driver's own begin()
// claims the pin anyway.
static void tdeckmaxGpsTxGuard(bool railOn)
{
    if (railOn) {
        Serial1.setPins(GPS_RX_PIN, GPS_TX_PIN); // GPS_SERIAL_PORT is Serial1 (GPS.cpp)
    } else {
        pinMode(GPS_TX_PIN, OUTPUT);
        digitalWrite(GPS_TX_PIN, LOW);
    }
}

// GPS rail policy (XL9555 P02 / EXPANDS_GPS_EN): powered only while gps_mode == ENABLED. Called
// from lateInitVariant() once config is loaded, and from the InkHUD menu's GPS toggle so a RUNTIME
// gps_mode change drives the rail too (it used to be boot-only: disabling GPS from the menu left
// the MIA-M10Q powered at ~25-30mA until the next reboot). We deliberately do NOT toggle this
// rail across light sleep -- cutting GPS power every 120s cycle destroys hot-start.
void tdeckmaxApplyGpsRail()
{
    const bool gpsEnabled = config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED;
    io.pinMode(EXPANDS_GPS_EN, OUTPUT);
    io.digitalWrite(EXPANDS_GPS_EN, gpsEnabled ? HIGH : LOW);
    tdeckmaxGpsTxGuard(gpsEnabled);
    LOG_INFO("T-Deck Max GPS rail (XL9555 P02) %s (gps_mode=%d)", gpsEnabled ? "ON" : "OFF", (int)config.position.gps_mode);
}

// Hard rail cut, used by two GPS.cpp paths that both run AFTER the probe's serial begin() (so
// the TX guard also undoes the probe's idle-HIGH TX into the now-dead module, the ~22mA
// back-power path):
//   - probe give-up: module never answered; GPS_OFF's power-down chain is a no-op on this board
//     (no PIN_GPS_EN / PMU / standby pin) so without this the abandoned MIA-M10Q sat in indoor
//     acquisition at ~25-30mA forever (the #1 item of the 1-day-battery audit)
//   - DISABLED boot: the probe runs WITH the rail up (lateInitVariant leaves it on) so the
//     module is properly DETECTED and disable() can deliver the u-blox soft-sleep first; then
//     this cuts the power. Detection means a later menu re-enable works without a reboot.
// tdeckmaxApplyGpsRail() (menu GPS toggle / next boot) re-applies policy.
void tdeckmaxGpsRailOff(const char *reason)
{
    io.pinMode(EXPANDS_GPS_EN, OUTPUT);
    io.digitalWrite(EXPANDS_GPS_EN, LOW);
    tdeckmaxGpsTxGuard(false);
    LOG_INFO("T-Deck Max GPS rail (XL9555 P02) OFF (%s)", reason);
}

void lateInitVariant()
{
    // GPS rail at boot: earlyInitVariant() powered the MIA-M10Q up unconditionally for a clean
    // bus bring-up. Deliberately LEAVE IT ON here even when gps_mode == DISABLED: the GPS thread's
    // boot probe (which runs after this) then detects the module properly, sends the u-blox
    // soft-sleep via disable(), and GPS.cpp cuts the rail + TX guard afterwards
    // (tdeckmaxGpsRailOff). Cutting here made every DISABLED boot probe a dead module ("not
    // detected"), leaving the driver unable to re-enable GPS without a reboot. Only NOT_PRESENT
    // (no module expected, no probe wanted) cuts immediately.
    if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT)
        tdeckmaxGpsRailOff("gps_mode NOT_PRESENT");

#if defined(ARCH_ESP32) && defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
    // Frontlight off-in-light-sleep / restore-on-wake. Registered independently of the touch
    // controller bring-up. The persisted level itself is applied by MenuApplet's constructor
    // (mirrors the T5 backlight pref), which runs during InkHUD setup.
    frontlightLightSleepObserver.sleepObserver.observe(&notifyLightSleep);
    frontlightLightSleepObserver.wakeObserver.observe(&notifyLightSleepEnd);

    // Power button: stamp asleep->awake edges for the fresh-wake grace window (see above)
    screenWakeStamp.observer.observe(&notifyScreenPower);

    // Boot counts as a wake: grant the initial interactive-nap window, so the boot screen is
    // readable while the CPU already naps (no moon) and the keyboard is live for the first
    // keystroke. Without this, the zero idle threshold would stamp the moon seconds after boot.
    tdeckMaxKbWakeUntilMs = millis() + TDECKMAX_INTERACTIVE_WINDOW_MS;
#endif

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
    // Touchscreen FULLY DISABLED on the keyboard-first InkHUD product: the bezel keys were
    // already unwired (mistouch-prone) and every touch function has a keyboard equivalent, so
    // the CST328 served nothing while still actively scanning (~2-3mA measured by the power
    // sweep, plus I2C poll traffic every wake). Hold it in RESET instead: no scanning, no
    // reports, microamp-class draw. No hyn init, no TouchScreenImpl1, no bridge, no LS
    // observers -- and nicheGraphics.h registers no TouchEnabledProvider, so InkHUD runs
    // keyboard-only. (The chip has no power rail of its own; reset-hold is the off switch.
    // BaseUI diagnostics below keeps full touch + bezel keys for hardware validation.)
    io.pinMode(EXPANDS_TOUCH_RST, OUTPUT);
    io.digitalWrite(EXPANDS_TOUCH_RST, LOW);
    LOG_INFO("T-Deck Max: CST328 touch held in reset (keyboard-only build)");
#else
    hyn_touch_attach_xl9555(&io);
    // BaseUI diagnostics keeps the bezel keys (-> InputBroker).
    hyn_touch_set_key_callback(touchKeyCallback, nullptr);
    if (hyn_touch_init()) {
#ifdef ARCH_ESP32
        touchLightSleepObserver.sleepObserver.observe(&notifyLightSleep);
        touchLightSleepObserver.wakeObserver.observe(&notifyLightSleepEnd);
#endif
        touchScreenImpl1 = new TouchScreenImpl1(EINK_WIDTH, EINK_HEIGHT, readTouch);
        touchScreenImpl1->init();
    } else {
        LOG_WARN("T-Deck Max touch init failed");
    }
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#ifndef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
    // BaseUI only: bring up the ES8311 codec for readAloud/audio features. The InkHUD product
    // build deliberately SKIPS this: nothing there ever plays audio (use_i2s_as_buzzer=false by
    // NodeDB default, chirps are no-ops, readAloud is BaseUI-only), and board.begin() takes the
    // codec out of its power-on power-down state into normal mode - a permanent ~8mA leak for
    // zero function (#2 item of the 1-day-battery audit). Unconfigured, the ES8311 stays in its
    // reset default: all analog blocks powered down.
    PinsAudioBoardES8311.addI2C(PinFunction::CODEC, Wire);
    PinsAudioBoardES8311.addI2S(PinFunction::CODEC, DAC_I2S_MCLK, DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT, DAC_I2S_DIN);

    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_LINE1;
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_44K;
    board.begin(cfg);
    board.setVolume(75);
#endif // !MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#if defined(MOD_I2C_TCA8418_KEYBOARD)
    // TCA8418 QWERTY keyboard -> InkHUD InputMenuApplet. The I2C bus (Wire) and the XL9555
    // kb-reset pulse (P11) were already brought up in earlyInitVariant(). The driver queues raw
    // positional 2-byte (modCode, keyCode) T-keyboard events; the applet does all the mapping
    // against its mirrored on-screen boards (see TDeckMaxTKeyboard.h).
    {
        auto *kb = new TDeckMaxTKeyboard();
        kb->begin(TCA8418_KB_ADDR, &Wire);
        inkhudI2CKeyboard = kb;
        // KB_IRQ_PIN (GPIO15, INPUT_PULLUP from earlyInitVariant) carries the TCA8418 INT
        // line, used ONLY as the window-gated light-sleep wake source (doLightSleep). No ISR
        // is attached - see the never-attachInterrupt storm warning earlier in this file.
        LOG_INFO("T-Deck Max: TCA8418 keyboard wired to InkHUD IME");
    }
#endif

#if defined(ARCH_ESP32) && defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
    // Quick sleep idle watchdog (OSThread self-registers with the scheduler). The enable flag
    // was already applied from the persisted TDeckMaxPrefs by MenuApplet's constructor.
    new TDeckMaxIdleSleepThread();
#endif

}

#endif
