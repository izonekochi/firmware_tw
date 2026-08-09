#pragma once

#include "TDeckMaxBoard.h"

// Display (E-Ink)
#define PIN_EINK_CS BOARD_EPD_CS
#define PIN_EINK_BUSY BOARD_EPD_BUSY
#define PIN_EINK_DC BOARD_EPD_DC
#define PIN_EINK_RES BOARD_EPD_RST
#define PIN_EINK_SCLK BOARD_EPD_SCK
#define PIN_EINK_MOSI BOARD_EPD_MOSI
// This board has a PWM frontlight, unlike older PIN_EINK_BL users where the pin can mean panel power.
#define HAS_EINK_FRONTLIGHT
#define PIN_EINK_BL BOARD_EPD_BL
#define BRIGHTNESS_DEFAULT 130

#define I2C_SDA SDA
#define I2C_SCL SCL

// CST328 touch screen (implementation in src/platform/extra_variants/t_deck_max/variant.cpp)
#define HAS_TOUCHSCREEN 1
#define CST328_PIN_INT BOARD_TOUCH_INT
#define SCREEN_TOUCH_INT BOARD_TOUCH_INT
// Keep CST328 usable while awake, but don't use its IRQ as a light-sleep wake source.
// WAKE_ON_TOUCH is deliberately left UNDEFINED: sleep.cpp only arms SCREEN_TOUCH_INT as a
// GPIO wake under `#if defined(WAKE_ON_TOUCH)`, so touch can neither wake nor reboot the
// ESP32-S3 out of the 120s light-sleep window (touch wake was observed to reset the S3).

// --- BLE-off light sleep -----------------------------------------------------------------
// USE_POWERSAVE + SLEEP_TIME arm the PowerFSM light-sleep path (mirrors t-deck-pro). The
// resolved light-sleep wake set for this variant (see src/sleep.cpp doLightSleep() -- the
// gpio_wakeup_enable() block at ~L448-479, plus enableLoraInterrupt() at L474/587-616) is:
//     * BUTTON_PIN / GPIO0 (BOOT)      -> gpio_wakeup_enable()   @ sleep.cpp:459-462
//     * LoRa DIO1 / GPIO5 (SX1262 IRQ) -> enableLoraInterrupt()  @ sleep.cpp:474 / 610,615
//     * 120s timer                     -> esp_sleep_enable_timer_wakeup() @ sleep.cpp:486
// NOTHING ELSE is armed: KB_INT (renamed to KB_IRQ_PIN below, so the arming at sleep.cpp:452
// never fires), WAKE_ON_TOUCH (undefined), and ROTARY_PRESS / INPUTDRIVER_* /
// BOARD_PCA9535_INT / PMU_IRQ (none of which are defined for T_DECK_MAX). Result: keyboard
// presses and screen touches can NEVER wake or reboot the device from light sleep. The side
// power button is on the SY6970 /QON pin (not a GPIO) and is the ship-mode off/on control.
//
// BLE-off is a RUNTIME choice, not a compile switch: standalone use = config.bluetooth.enabled
// = false and config.power.is_power_saving = true. WiFi stays COMPILED IN (do NOT add
// MESHTASTIC_EXCLUDE_WIFI) because PowerFSM's light-sleep transitions depend on it. Nothing
// in this variant forces BLE on.
#define USE_POWERSAVE
#define SLEEP_TIME 120

// GNSS
#define HAS_GPS 1
#define GPS_BAUDRATE 38400
#define GPS_RX_PIN BOARD_GPS_RXD
#define GPS_TX_PIN BOARD_GPS_TXD
#define PIN_GPS_PPS BOARD_GPS_PPS

#define BUTTON_PIN BOARD_BOOT_PIN

// Vibration motor (DRV2605L @ I2C 0x5A, LRA/ERM haptic driver).
// Motor power is gated by XL9555 P05 (EXPANDS_DRV_EN), driven HIGH in earlyInitVariant().
// main.cpp brings the driver up (drv.begin(); selectLibrary(1); INTTRIG mode) under HAS_DRV2605.
// PIN_DRV_EN is intentionally NOT defined (that path is for a direct GPIO enable) -- the rail
// is the expander pin, already asserted before drv.begin(). ExternalNotificationModule fires
// drv.go() on a message when moduleConfig.external_notification.alert_message_vibra is set
// (NodeDB installs that default for T_DECK_MAX). The motor only ever fires while awake.
#define HAS_DRV2605 1

// SPI interface SD card slot. SD rides the shared FSPI bus with LoRa/EPD, so we do NOT
// define SDCARD_USE_SPI1 (that would spin up a second HSPI bus). FSCommon.cpp setupSDCard
// needs SPI_SCK/SPI_MISO/SPI_MOSI and SDCARD_CS on the default SPI.
#define HAS_SDCARD
#define SPI_MOSI BOARD_SPI_MOSI // 33
#define SPI_SCK BOARD_SPI_SCK   // 36
#define SPI_MISO BOARD_SPI_MISO // 47
#define SDCARD_CS BOARD_SD_CS   // 48
#define SD_SPI_FREQUENCY 20000000U

// TCA8418 keyboard
#define KB_BL_PIN BOARD_KEYBOARD_LED
// Keyboard IRQ is used only while awake for lazy polling; it is deliberately NOT a
// light-sleep wake source. Named KB_IRQ_PIN (not the sleep-arming macro) so the
// deep-sleep GPIO wake arming in sleep.cpp/main-esp32.cpp never sees it.
#define KB_IRQ_PIN BOARD_KEYBOARD_INT // = 15
#define CANNED_MESSAGE_MODULE_ENABLE 1

// Audio codec ES8311
#define HAS_I2S
#define DAC_I2S_BCK BOARD_ES8311_SCLK
#define DAC_I2S_WS BOARD_ES8311_LRCK
#define DAC_I2S_DOUT BOARD_ES8311_DSDIN
#define DAC_I2S_DIN BOARD_ES8311_ASDOUT
#define DAC_I2S_MCLK BOARD_ES8311_MCLK

// Gyroscope BHI260AP
#define HAS_BHI260AP

// Battery charger SY6970 and fuel gauge BQ27220
#define HAS_PPM 1
#define XPOWERS_CHIP_SY6970
#define HAS_BQ27220 1
#define BQ27220_I2C_SDA SDA
#define BQ27220_I2C_SCL SCL
#define BQ27220_DESIGN_CAPACITY 1500

// External expansion chip XL9555
#define USE_XL9555
#define EXPANDS_MODEM_EN BOARD_XL9555_00_6609_EN
#define EXPANDS_LORA_EN BOARD_XL9555_01_LORA_EN
#define EXPANDS_GPS_EN BOARD_XL9555_02_GPS_EN
#define EXPANDS_1V8_EN BOARD_XL9555_03_1V8_EN
#define EXPANDS_LORA_SEL BOARD_XL9555_04_LORA_SEL
#define EXPANDS_DRV_EN BOARD_XL9555_05_MOTOR_EN
#define EXPANDS_AMP_EN BOARD_XL9555_06_AMPLIFIER
#define EXPANDS_TOUCH_RST BOARD_XL9555_07_TOUCH_RST
#define EXPANDS_MODEM_PWRKEY BOARD_XL9555_10_PWRKEY_EN
#define EXPANDS_KB_RST BOARD_XL9555_11_KEY_RST
#define EXPANDS_AUDIO_SEL BOARD_XL9555_12_AUDIO_SEL

// LoRa
#define USE_SX1262
#define USE_SX1268

#define LORA_SCK BOARD_LORA_SCK
#define LORA_MISO BOARD_LORA_MISO
#define LORA_MOSI BOARD_LORA_MOSI
#define LORA_CS BOARD_LORA_CS

#define LORA_DIO0 -1
#define LORA_RESET BOARD_LORA_RST
#define LORA_DIO1 BOARD_LORA_INT
#define LORA_DIO2 BOARD_LORA_BUSY
#define LORA_DIO3

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 2.4

// A7682E modem pins are defined but the expander-controlled rail stays off by default.
// BACK-POWER WARNING (same trap as the GPS UART, measured at 22mA there): while the VDD4V2
// rail is off, NO ESP32 pin into the module (MODEM_TX, MODEM_DTR) may drive HIGH -- an
// idle-HIGH UART TX feeds the dead module through its input-protection diodes. Today these
// pins are never initialized (reset-default Hi-Z), which is safe. Any future modem driver
// must power the rail BEFORE beginning the UART, and drop TX to GPIO-LOW when cutting it
// (see tdeckmaxGpsTxGuard in extra_variants/t_deck_max/variant.cpp for the pattern).
#define MODEM_RI BOARD_A7682E_RI
#define MODEM_DTR BOARD_A7682E_DTR
#define MODEM_RX BOARD_A7682E_RXD
#define MODEM_TX BOARD_A7682E_TXD

#define HAS_PHYSICAL_KEYBOARD 1

// --- Stage 6: persisted hardware toggles ------------------------------------------------
// Runtime hooks driven by the InkHUD system menu (see MenuApplet.cpp TDeckMaxPrefs). Defined
// in src/platform/extra_variants/t_deck_max/variant.cpp. Declared here so MenuApplet.cpp
// (which pulls this variant.h in via configuration.h) can call them under #if defined(T_DECK_MAX).
//   * antenna:    XL9555 P04. internal = HIGH, external = LOW.
//   * frontlight: GPIO41 PWM level (0/64/128/255); also restored after light-sleep.
//   * touch:      gates the coordinate/gesture read only; the 3 bezel keys are unaffected.
void tdeckmaxSetAntennaExternal(bool external);
bool tdeckmaxGetAntennaExternal(); // current mux state, for the InkHUD corner indicator
void tdeckmaxSetFrontlight(uint8_t level);
void tdeckmaxSetTouchEnabled(bool enabled);
bool tdeckmaxIsTouchEnabled(); // InkHUD TouchEnabledProvider (registered in nicheGraphics.h)
void tdeckmaxSetQuickSleep(bool enabled);
// Hard GPS-rail cut + UART TX back-power guard (GPS.cpp: probe give-up AND the DISABLED-boot
// path after the module has been detected and soft-slept). reason goes to the log.
void tdeckmaxGpsRailOff(const char *reason);
// CPU frequency experiment toggle (menu "CPU 240MHz"): 80MHz default vs 240MHz race-to-sleep
void tdeckmaxSetCpuFast(bool fast);
// Boot-time pref restore: records the choice WITHOUT switching the clock mid-setup; the idle
// watchdog's one-shot re-assert applies it after setup() completes (see variant.cpp)
void tdeckmaxSetCpuFastBootPref(bool fast);
// Debug Hold (menu "Hardware" -> "Debug Hold", SESSION-ONLY, never persisted): while true the
// idle watchdog holds the device fully awake (no naps, no moon) and the InkHUD SystemInfo
// applet refreshes every few seconds - a live dashboard for the charger/gauge debug rows.
extern bool tdeckmaxDebugHold;
// Vibration buzz policy (persisted in TDeckMaxPrefs, applied by MenuApplet; read by
// ExternalNotificationModule): 0 = all messages, 1 = DMs only, 2 = off.
extern uint8_t tdeckmaxVibraMode;
// Silent mode (persisted in TDeckMaxPrefs, toggled via alt+S / menu "Hardware"): super power
// saving. While asleep, NO e-ink refresh happens at all (InkHUD::requestUpdate/forceUpdate are
// dropped - no autoshow repaints, no Info-page refresh); messages are still received and
// recorded to the store, and everything shows on the next BOOT wake. Indicator = crescent moon.
extern bool tdeckmaxSilentMode;
// NavMap GPS session hold (locate/trace, set by NavMapApplet::beginGpsSession, cleared by
// endGpsSession): the idle watchdog keeps the device awake for the session - acquisition
// needs the CPU pumping NMEA, and trace needs live renders. Session-only, never persisted.
extern bool tdeckmaxGpsSessionHold;
// Phone-style BOOT power button (InkHUD builds; wired in nicheGraphics.h):
// short = wake / sleep toggle, 2s hold = graceful shutdown
void tdeckmaxPowerButtonDown(); // press-down edge: marks whether THIS press woke the device
void tdeckmaxPowerButtonShort();
void tdeckmaxPowerButtonLong();
// Drive the XL9555 GPS rail from config.position.gps_mode (boot + runtime menu toggle)
void tdeckmaxApplyGpsRail();

// Quick sleep: how long the device must be idle (no keyboard/BOOT activity) while in stateON
// before the idle watchdog drops straight to light sleep. ZERO (user design): within the
// interactive-nap window below the keyboard is a wake source, so there is no reason to sit
// awake at all - the device naps the moment the screen work is done (the watchdog still waits
// for pending renders / a LoRa TX in flight, and typing/menu sessions are protected separately
// via system-applet focus). Effective latency to nap: <=500ms poll + the e-ink refresh.
#ifndef TDECKMAX_QUICK_SLEEP_IDLE_MS
#define TDECKMAX_QUICK_SLEEP_IDLE_MS 0
#endif

// Interactive-nap window (user design): granted at boot and on every BOOT wake, RECOUNTED from
// every keystroke. While millis() is inside the window the screen stays logically ON - no moon,
// input processed - the CPU light-sleeps whenever idle (immediately, see above), and doLightSleep
// arms the TCA8418 INT line as a light-sleep wake source so a keystroke wakes the CPU and is
// handled seamlessly. When the window lapses, the idle watchdog stamps the sleep indicator and
// the keyboard wake is disarmed (pocket-safe): from then on only BOOT / LoRa / timer wake.
// Interaction therefore costs only the keystroke handling itself - never an awake dwell.
#ifndef TDECKMAX_INTERACTIVE_WINDOW_MS
#define TDECKMAX_INTERACTIVE_WINDOW_MS 15000
#endif

// USB enumeration grace: after a BOOT wake (or cold boot) with VBUS present, hold the device
// SOLIDLY awake this long so a USB host can (re)enumerate the USB-Serial-JTAG. While asleep,
// every nap detaches the port, so a PC plugged into a SLEEPING device never enumerates - and
// without SOF frames the isPlugged() hold-awake can never engage (chicken-and-egg: "plug then
// wake" was unmonitorable/unflashable while "wake then plug" worked). Once enumeration lands,
// the isPlugged hold takes over indefinitely; on a dumb charger (wall/Qi pad) no SOF appears
// and napping resumes when the grace lapses.
#ifndef TDECKMAX_USB_ENUM_GRACE_MS
#define TDECKMAX_USB_ENUM_GRACE_MS 5000
#endif

// While the screen is asleep (moon shown), InkHUD display updates requested by applets are
// DEFERRED (see Renderer::runOnce sleep gating): the wake repaint redraws everything anyway, so
// mid-nap renders were pure e-ink wear plus ~1s of extra awake time per wake window. At most one
// deferred update is released to the glass per this interval, so a glance at the sleeping device
// still shows a reasonably fresh info panel.
#ifndef TDECKMAX_SLEEP_RENDER_INTERVAL_MS
#define TDECKMAX_SLEEP_RENDER_INTERVAL_MS (10UL * 60 * 1000)
#endif
