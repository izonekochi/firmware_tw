#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

// InkHUD-specific components
// ---------------------------
#include "graphics/niche/InkHUD/InkHUD.h"

// Applets
#include "graphics/niche/InkHUD/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/DM/DMApplet.h"
#include "graphics/niche/InkHUD/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/NavMap/NavMapApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/DMChat/DMChatApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/UniChat/UniChatApplet.h"

#include "graphics/niche/InkHUD/Applets/User/SystemInfo/SystemInfoApplet.h"

// Shared NicheGraphics components
// --------------------------------
#include "graphics/niche/Drivers/EInk/GDEQ031T10.h"
#include "graphics/niche/Inputs/TwoButton.h"

#ifdef MOD_CJK_ENABLED
#include "graphics/niche/Fonts/cubicFont.h"
#endif // MOD_CJK_ENABLED

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // SPI
    // -----------------------------
    // The E-Ink panel rides the shared FSPI bus (SCK 36 / MOSI 33 / MISO 47) that the
    // LoRa radio already brought up as the global Arduino `SPI` object in main.cpp
    // (SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS) runs before setupNicheGraphics).
    // Do NOT create a second SPIClass or re-begin — UC8175 sendCommand/sendData take
    // spiLock, so the driver coexists with RadioLib on the same bus.

    // E-Ink Driver
    // -----------------------------
    Drivers::EInk *driver = new Drivers::GDEQ031T10;
    driver->begin(&SPI, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------
    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the E-Ink driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update, and how unhealthy extra FAST updates are
    inkhud->setDisplayResilience(10, 1.5);

    // Select fonts
#ifdef MOD_CJK_ENABLED
    // use the same CJK font because of the limited storage size
    InkHUD::Applet::fontLarge = InkHUD::AppletFont(cubicFont, InkHUD::AppletFont::CJK_UTF8);
    InkHUD::Applet::fontMedium = InkHUD::AppletFont(cubicFont, InkHUD::AppletFont::CJK_UTF8);
    InkHUD::Applet::fontSmall = InkHUD::AppletFont(cubicFont, InkHUD::AppletFont::CJK_UTF8);
#else  //! MOD_CJK_ENABLED
    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;
#endif // MOD_CJK_ENABLED

    // Customize default settings
    inkhud->persistence->settings.userTiles.maxCount = 2; // How many tiles can the display handle?
    // Native panel orientation is portrait 240x320. rotation 0 keeps that portrait layout.
    // (Final orientation to be confirmed on hardware; adjust here if the panel is mounted rotated.)
    inkhud->persistence->settings.rotation = 0;
    inkhud->persistence->settings.userTiles.count = 1; // One tile only by default, keep things simple for new users
    inkhud->persistence->settings.optionalMenuItems.nextTile = true;

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    //
    // Messaging consolidated into the "Chat" slots below (2026-08-06): one reusable applet type
    // that picks any channel or DM thread inside itself, with slot 0 as the single storage
    // writer. The per-channel / all-messages / latest-DM applets are retired but kept here
    // commented for easy resurrection - note that re-enabling a ThreadedMessageApplet
    // automatically reclaims its channel's storage duty from the Chats (asThreadedMessageApplet
    // discovery in UniChatApplet).
    // inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true);
    // inkhud->addApplet("DMs", new InkHUD::DMApplet);
    // inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));
    // inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
#ifdef MOD_INPUT_MENU
    // Unified chats: each slot picks any channel or DM thread inside the applet (targets
    // persist per slot); slot 0 stores ALL text messages into the (PSRAM-sized) shared store
    // regardless of menu state, the rest only read it. Chat 1+2 active out of the box; enable
    // more (and per-chat autoshow) via the menu. Registered FIRST for autoshow priority.
    for (uint8_t chatSlot = 0; chatSlot < InkHUD::UniChatApplet::MAX_CHAT_SLOTS; chatSlot++) {
        inkhud->addApplet(InkHUD::UniChatApplet::slotBaseName(chatSlot), new InkHUD::UniChatApplet(chatSlot),
                          chatSlot < 2,                     // Chat 1+2 active by default
                          chatSlot == 0,                    // Chat 1 autoshows by default
                          chatSlot == 0 ? 0 : (uint8_t)-1); // Chat 1 is tile 0's default applet
    }
    // Map applets. Favorites Map / Positions retired (user decision 2026-08-07: NavMap with
    // its tile data + node markers covers both); commented for easy resurrection.
    // inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet, true);
    // inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);
    inkhud->addApplet("Nav Map", new InkHUD::NavMapApplet, true); // Activated - manual pan/zoom, no GPS needed
#else                                                                        //! MOD_INPUT_MENU
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);        // Activated
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet);       // -
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);         // -
#endif                                                                       // MOD_INPUT_MENU
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);   // Activated, no autoshow, default on tile 0
    inkhud->addApplet("Info", new InkHUD::SystemInfoApplet, true, false, 0); // MUI-style device info page; no autoshow, tile 0

    // Dynamic per-peer DM chat slots: registered inactive/unbound, claimed at runtime by
    // incoming DMs (Events -> DMChatApplet::onIncomingDM) and released via menu "Close Chat".
    // Appended LAST so the persisted active/autoshow indices of everything above survive this
    // firmware update; the guard keeps the settings arrays in bounds regardless of how many
    // channel applets were added above.
    uint8_t dmSlotsRegistered = 0;
    for (uint8_t dmSlot = 0; dmSlot < InkHUD::DMChatApplet::MAX_SLOTS; dmSlot++) {
        if (inkhud->userApplets.size() >= InkHUD::Persistence::MAX_USERAPPLETS_GLOBAL)
            break;
        inkhud->addApplet(InkHUD::DMChatApplet::slotBaseName(dmSlot), new InkHUD::DMChatApplet(dmSlot));
        dmSlotsRegistered++;
    }
    if (dmSlotsRegistered < InkHUD::DMChatApplet::MAX_SLOTS)
        LOG_WARN("DMChat: only %u of %u slots registered (userApplets at the settings cap - too many channel applets?)",
                 (unsigned)dmSlotsRegistered, (unsigned)InkHUD::DMChatApplet::MAX_SLOTS);

    // InputMenuApplet (CJK IME + menu navigation) self-registers via WindowManager under
    // MOD_INPUT_MENU — it must NOT be added here.

    // NO TouchEnabledProvider: the touchscreen is fully disabled on this keyboard-only build
    // (CST328 held in reset by lateInitVariant; bezel keys unwired). With no provider, InkHUD
    // never places the touch OSK / TouchStatus tiles and runs pure keyboard navigation.

    // Start running InkHUD
    inkhud->begin();

    // Buttons
    // --------------------------
    // Stage 2: the only wired input is the BOOT button (BUTTON_PIN / GPIO0). The TCA8418
    // keyboard and the CST3xx bezel keys are wired to InkHUD in Stage 3.
    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // Shared NicheGraphics component

    // #0: BOOT — phone-style power button ONLY (no InkHUD navigation):
    //   short press: wake when asleep (EVENT_PRESS fires on button-down in the TwoButton driver);
    //                put the device to sleep when awake (same path as the quick-sleep watchdog)
    //   2s hold:     graceful shutdown, like a phone power button
    // The InkHUD "user button" role (short = select/advance, long = menu) lives on the MIDDLE
    // capacitive bezel key (see touchKeyCallback in the variant), with prev/next on left/right.
    buttons->setWiring(0, Inputs::TwoButton::getUserButtonPin(), true); // Internal pull up
    buttons->setTiming(0, 50, 2000); // debounce 50ms; 2s hold = shutdown (phone-like, not the 500ms default)
    buttons->setHandlerDown(0, []() { tdeckmaxPowerButtonDown(); }); // marks a waking press (release must not re-sleep)
    buttons->setHandlerShortPress(0, []() { tdeckmaxPowerButtonShort(); });
    buttons->setHandlerLongPress(0, []() { tdeckmaxPowerButtonLong(); });

    // Begin handling button events
    buttons->start();
}

#endif
