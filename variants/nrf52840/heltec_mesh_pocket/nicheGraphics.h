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
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"

// Shared NicheGraphics components
// --------------------------------
#include "graphics/niche/Drivers/EInk/LCMEN2R13ECC1.h"
#include "graphics/niche/Inputs/TwoButton.h"

#ifdef MOD_CJK_ENABLED
#include "graphics/niche/Fonts/cubicFont.h"
#endif //MOD_CJK_ENABLED

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // SPI
    // -----------------------------

    // For NRF52 platforms, SPI pins are defined in variant.h
    SPI1.begin();

    // E-Ink Driver
    // -----------------------------

    Drivers::EInk *driver = new Drivers::LCMEN2R13ECC1;
    driver->begin(&SPI1, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the E-Ink driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    // Set how unhealthy additional FAST updates beyond this number are
    inkhud->setDisplayResilience(10, 1.5);

    // Select fonts
#ifdef MOD_CJK_ENABLED
    // use the same CJK font because of the limited storage size
    InkHUD::Applet::fontLarge = InkHUD::AppletFont(cubicFont, InkHUD::AppletFont::CJK_UTF8);
    InkHUD::Applet::fontMedium = InkHUD::AppletFont(cubicFont, InkHUD::AppletFont::CJK_UTF8);
    InkHUD::Applet::fontSmall = InkHUD::AppletFont(cubicFont, InkHUD::AppletFont::CJK_UTF8);
#else //!MOD_CJK_ENABLED
    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1253;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1253;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1253;
#endif //MOD_CJK_ENABLED

    // Customize default settings
    inkhud->persistence->settings.userTiles.maxCount = 2; // How many tiles can the display handle?
    inkhud->persistence->settings.rotation = 3;           // 270 degrees clockwise
    inkhud->persistence->settings.userTiles.count = 1;    // One tile only by default, keep things simple for new users
    inkhud->persistence->settings.optionalMenuItems.nextTile = true;

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true); // Activated, autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet);                              // -
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));        // -
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));        // -
#ifdef MOD_INPUT_MENU
    if (channels.getNumChannels() > 2 && channels.getByIndex(2).has_settings)
        inkhud->addApplet("Channel 2", new InkHUD::ThreadedMessageApplet(2));    // -
    if (channels.getNumChannels() > 3 && channels.getByIndex(3).has_settings)
        inkhud->addApplet("Channel 3", new InkHUD::ThreadedMessageApplet(3));    // -
    if (channels.getNumChannels() > 4 && channels.getByIndex(4).has_settings)
        inkhud->addApplet("Channel 4", new InkHUD::ThreadedMessageApplet(4));    // -
    if (channels.getNumChannels() > 5 && channels.getByIndex(5).has_settings)
        inkhud->addApplet("Channel 5", new InkHUD::ThreadedMessageApplet(5));    // -
    if (channels.getNumChannels() > 6 && channels.getByIndex(6).has_settings)
        inkhud->addApplet("Channel 6", new InkHUD::ThreadedMessageApplet(6));    // -
    if (channels.getNumChannels() > 7 && channels.getByIndex(7).has_settings)
        inkhud->addApplet("Channel 7", new InkHUD::ThreadedMessageApplet(7));    // -
#else //!MOD_INPUT_MENU
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);           // Activated
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet);          // -
#endif //MOD_INPUT_MENU
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);            // -
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);         // Activated, no autoshow, default on tile 0

    // Start running InkHUD
    inkhud->begin();

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance(); // Shared NicheGraphics component

    // #0: Main User Button
    buttons->setWiring(0, Inputs::TwoButton::getUserButtonPin());
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

    // Begin handling button events
    buttons->start();
}

#endif
