#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./Events.h"

#include "MessageStore.h"
#include "PowerFSM.h"
#include "buzz.h"
#include "gps/RTC.h"
#include "modules/ExternalNotificationModule.h"
#include "modules/TextMessageModule.h"
#include "sleep.h"

#include "./Applet.h"
#include "./Applets/User/DMChat/DMChatApplet.h" // dynamic per-peer chat routing (onReceiveTextMessage)
#include "./SystemApplet.h"
#include "graphics/niche/Utils/FlashData.h"

using namespace NicheGraphics;

#if defined(T_DECK_MAX)
// T-Deck Max InkHUD sleep-UX shared screen-awake flag (extern-declared in InkHUD.h).
// Defaults true: the device boots into stateON, and Events::onScreenPower keeps it in sync thereafter.
bool inkhudScreenAwake = true;
#endif

namespace
{
// When touch long-press opens menu, some panels report a delayed release/tap
// if the finger stays down briefly. Keep this long enough to cover that release.
constexpr uint32_t TOUCH_MENU_OPEN_TAP_SUPPRESS_MS = 1200;

inline void noteInkHUDUserInteraction()
{
    // Keep power state and screen-timeout behavior in sync with InkHUD input activity.
#if defined(T_DECK_MAX)
    // Sleep-UX: keyboard/touch/bezel input must never wake the screen from sleep; it may only
    // re-stamp the 30s ON timer while ALREADY awake (EVENT_INPUT has DARK/LS/NB -> ON transitions
    // that would otherwise wake it). The gate also covers a stale input during a packet-wake nap.
    if (inkhudScreenAwake)
        powerFSM.trigger(EVENT_INPUT);
#else
    powerFSM.trigger(EVENT_INPUT);
#endif
}
} // namespace

InkHUD::Events::Events()
{
    // Get convenient references
    inkhud = InkHUD::getInstance();
    settings = &inkhud->persistence->settings;
}

void InkHUD::Events::begin()
{
    // Register our callbacks for the various events

    deepSleepObserver.observe(&notifyDeepSleep);
    rebootObserver.observe(&notifyReboot);
    textMessageObserver.observe(textMessageModule);
#if !MESHTASTIC_EXCLUDE_ADMIN
    adminMessageObserver.observe((Observable<AdminModule_ObserverData *> *)adminModule);
#endif
#ifdef ARCH_ESP32
    lightSleepObserver.observe(&notifyLightSleep);
#endif
#if defined(T_DECK_MAX)
    screenPowerObserver.observe(&notifyScreenPower);
#endif
}

void InkHUD::Events::onButtonShort()
{
    noteInkHUDUserInteraction();

    // Audio feedback (via buzzer)
    // Short tone
    playChirp();
    // Cancel any beeping, buzzing, blinking
    // Some button handling suppressed if we are dismissing an external notification (see below)
    bool dismissedExt = dismissExternalNotification();

    // Check which system applet wants to handle the button press (if any)
    SystemApplet *consumer = nullptr;
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->handleInput) {
            consumer = sa;
            break;
        }
    }

    // If no system applet is handling input, default behavior instead is to cycle applets
    // or open menu if joystick is enabled
    if (consumer) {
        consumer->onButtonShortPress();
    } else if (!dismissedExt) { // Don't change applet if this button press silenced the external notification module
        Applet *userConsumer = inkhud->getActiveApplet();

        if (userConsumer != nullptr && userConsumer->isInputSubscribed(Applet::BUTTON_SHORT))
            userConsumer->onButtonShortPress();
        else {
            if (!settings->joystick.enabled)
                inkhud->nextApplet();
            else
                inkhud->openMenu();
        }
    }
}

void InkHUD::Events::onButtonLong()
{
    noteInkHUDUserInteraction();

    // Audio feedback (via buzzer)
    // Slightly longer than playChirp
    playBoop();

    // Check which system applet wants to handle the button press (if any)
    SystemApplet *consumer = nullptr;
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->handleInput) {
            consumer = sa;
            break;
        }
    }

    // If no system applet is handling input, default behavior instead is to open the menu
    if (consumer)
        consumer->onButtonLongPress();
    else {
        Applet *userConsumer = inkhud->getActiveApplet();

        if (userConsumer != nullptr && userConsumer->isInputSubscribed(Applet::BUTTON_LONG))
            userConsumer->onButtonLongPress();
        else
            inkhud->openMenu();
    }
}

void InkHUD::Events::onExitShort()
{
    // Preserve legacy behavior on non-touch builds:
    // EXIT input is only active when joystick mode is enabled.
    // Touch-capable builds intentionally bypass this joystick gate.
    if (!settings->joystick.enabled && !inkhud->hasTouchEnabledProvider()) {
        return;
    }

    noteInkHUDUserInteraction();

    // Audio feedback (via buzzer)
    // Short tone
    playChirp();
    // Cancel any beeping, buzzing, blinking
    // Some button handling suppressed if we are dismissing an external notification module (see below)
    bool dismissedExt = dismissExternalNotification();

    // Check which system applet wants to handle the button press (if any)
    SystemApplet *consumer = nullptr;
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->handleInput) {
            consumer = sa;
            break;
        }
    }

    // Always let active system applets consume EXIT/HOME input (menu close, keyboard cancel, etc),
    // including on touch-first nodes where joystick mode is disabled.
    if (consumer) {
        consumer->onExitShort();
        return;
    }

    // Touch-capable InkHUD nodes use EXIT/HOME as a quick app switcher launcher.
    if (!dismissedExt && inkhud->hasTouchEnabledProvider()) {
        inkhud->openAppSwitcher();
        return;
    }

    if (!dismissedExt) {
        Applet *userConsumer = inkhud->getActiveApplet();

        if (userConsumer != nullptr && userConsumer->isInputSubscribed(Applet::EXIT_SHORT)) {
            userConsumer->onExitShort();
        } else if (settings->joystick.enabled) {
            // Preserve existing joystick behavior when no applet handles EXIT.
            inkhud->nextTile();
        }
    }
}

void InkHUD::Events::onExitLong()
{
    // Preserve legacy behavior on non-touch builds:
    // EXIT input is only active when joystick mode is enabled.
    // Touch-capable builds intentionally bypass this joystick gate.
    if (!settings->joystick.enabled && !inkhud->hasTouchEnabledProvider()) {
        return;
    }

    noteInkHUDUserInteraction();

    // Audio feedback (via buzzer)
    // Slightly longer than playChirp
    playBoop();

    // Check which system applet wants to handle the button press (if any)
    SystemApplet *consumer = nullptr;
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->handleInput) {
            consumer = sa;
            break;
        }
    }

    // Always allow system applets to consume EXIT/HOME long-press.
    if (consumer) {
        consumer->onExitLong();
    } else {
        Applet *userConsumer = inkhud->getActiveApplet();

        if (userConsumer != nullptr && userConsumer->isInputSubscribed(Applet::EXIT_LONG))
            userConsumer->onExitLong();
        // Nothing uses exit long yet
    }
}

void InkHUD::Events::onNavUp()
{
    if (settings->joystick.enabled)
        onTouchNavUp();
}

void InkHUD::Events::onNavDown()
{
    if (settings->joystick.enabled)
        onTouchNavDown();
}

void InkHUD::Events::onNavLeft()
{
    if (settings->joystick.enabled)
        onTouchNavLeft();
}

void InkHUD::Events::onNavRight()
{
    if (settings->joystick.enabled)
        onTouchNavRight();
}

void InkHUD::Events::onTouchNavUp()
{
    noteInkHUDUserInteraction();

    // Audio feedback (via buzzer)
    // Short tone
    playChirp();
    // Cancel any beeping, buzzing, blinking
    // Some button handling suppressed if we are dismissing an external notification (see below)
    bool dismissedExt = dismissExternalNotification();

    // Check which system applet wants to handle the button press (if any)
    SystemApplet *consumer = nullptr;
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->handleInput) {
            consumer = sa;
            break;
        }
    }

    if (consumer)
        consumer->onNavUp();
    else if (!dismissedExt) {
        Applet *userConsumer = inkhud->getActiveApplet();

        if (userConsumer != nullptr && userConsumer->isInputSubscribed(Applet::NAV_UP))
            userConsumer->onNavUp();
    }
}

void InkHUD::Events::onTouchNavDown()
{
    noteInkHUDUserInteraction();

    // Audio feedback (via buzzer)
    // Short tone
    playChirp();
    // Cancel any beeping, buzzing, blinking
    // Some button handling suppressed if we are dismissing an external notification (see below)
    bool dismissedExt = dismissExternalNotification();

    // Check which system applet wants to handle the button press (if any)
    SystemApplet *consumer = nullptr;
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->handleInput) {
            consumer = sa;
            break;
        }
    }

    if (consumer)
        consumer->onNavDown();
    else if (!dismissedExt) {
        Applet *userConsumer = inkhud->getActiveApplet();

        if (userConsumer != nullptr && userConsumer->isInputSubscribed(Applet::NAV_DOWN))
            userConsumer->onNavDown();
    }
}

void InkHUD::Events::onTouchNavLeft()
{
    noteInkHUDUserInteraction();

    // Audio feedback (via buzzer)
    // Short tone
    playChirp();
    // Cancel any beeping, buzzing, blinking
    // Some button handling suppressed if we are dismissing an external notification (see below)
    bool dismissedExt = dismissExternalNotification();

    // Check which system applet wants to handle the button press (if any)
    SystemApplet *consumer = nullptr;
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->handleInput) {
            consumer = sa;
            break;
        }
    }

    // If no system applet is handling input, default behavior instead is to cycle applets
    if (consumer)
        consumer->onNavLeft();
    else if (!dismissedExt) { // Don't change applet if this button press silenced the external notification module
        Applet *userConsumer = inkhud->getActiveApplet();

        if (userConsumer != nullptr && userConsumer->isInputSubscribed(Applet::NAV_LEFT))
            userConsumer->onNavLeft();
        else
            inkhud->prevApplet();
    }
}

void InkHUD::Events::onTouchNavRight()
{
    noteInkHUDUserInteraction();

    // Audio feedback (via buzzer)
    // Short tone
    playChirp();
    // Cancel any beeping, buzzing, blinking
    // Some button handling suppressed if we are dismissing an external notification (see below)
    bool dismissedExt = dismissExternalNotification();

    // Check which system applet wants to handle the button press (if any)
    SystemApplet *consumer = nullptr;
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->handleInput) {
            consumer = sa;
            break;
        }
    }

    // If no system applet is handling input, default behavior instead is to cycle applets
    if (consumer)
        consumer->onNavRight();
    else if (!dismissedExt) { // Don't change applet if this button press silenced the external notification module
        Applet *userConsumer = inkhud->getActiveApplet();

        if (userConsumer != nullptr && userConsumer->isInputSubscribed(Applet::NAV_RIGHT))
            userConsumer->onNavRight();
        else
            inkhud->nextApplet();
    }
}

void InkHUD::Events::onTouchTap(uint16_t x, uint16_t y, bool longPress)
{
    const bool touchEnabledBuild = inkhud->hasTouchEnabledProvider();

    // A long-press used to open the menu can be followed by a synthetic/queued tap at release.
    // Ignore that brief follow-up window so touch-opened menus do not auto-select an item.
    if (touchEnabledBuild && !longPress && suppressTouchTapUntilMs != 0) {
        if ((int32_t)(millis() - suppressTouchTapUntilMs) < 0) {
            noteInkHUDUserInteraction();
            return;
        }
        suppressTouchTapUntilMs = 0;
    }

    // Give system applets (menu, keyboard, etc) first chance to consume direct touch input.
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->handleInput && sa->onTouchPoint(x, y, longPress)) {
            noteInkHUDUserInteraction();
            return;
        }
    }

    // In split layouts, tapping a different tile changes focus.
    // Consume this tap so selection does not also trigger button fallback behavior.
    if (inkhud->selectTileAt(x, y)) {
        noteInkHUDUserInteraction();
        return;
    }

    // Allow foreground user applet to consume direct touch input if it wants.
    Applet *userConsumer = inkhud->getActiveApplet();
    if (userConsumer != nullptr && userConsumer->onTouchPoint(x, y, longPress)) {
        noteInkHUDUserInteraction();
        return;
    }

    // Fallback to existing button semantics so non-touch-aware applets keep working unchanged.
    if (longPress) {
        onButtonLong();

        // Only arm suppression if the long-press actually opened menu foreground.
        SystemApplet *menu = inkhud->getSystemApplet("Menu");
        if (touchEnabledBuild && menu && menu->isForeground()) {
            suppressTouchTapUntilMs = millis() + TOUCH_MENU_OPEN_TAP_SUPPRESS_MS;
        }
    } else
        onButtonShort();
}

void InkHUD::Events::onFreeText(char c)
{
    noteInkHUDUserInteraction();

    // Trigger the first system applet that wants to handle the new character
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->handleFreeText) {
            sa->onFreeText(c);
            break;
        }
    }
}

void InkHUD::Events::onFreeTextDone()
{
    noteInkHUDUserInteraction();

    // Trigger the first system applet that wants to handle it
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->handleFreeText) {
            sa->onFreeTextDone();
            break;
        }
    }
}

void InkHUD::Events::onFreeTextCancel()
{
    noteInkHUDUserInteraction();

    // Trigger the first system applet that wants to handle it
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->handleFreeText) {
            sa->onFreeTextCancel();
            break;
        }
    }
}

// Callback for deepSleepObserver
// Returns 0 to signal that we agree to sleep now
int InkHUD::Events::beforeDeepSleep(void *unused)
{
    // Undo any transient InputMenu tile-split before we persist settings, so a shutdown mid-split doesn't save the
    // temporary 2-tile count as the user's layout. Idempotent (no-op unless a split is active); tiles are still
    // alive here, so the merge is safe.
    inkhud->restoreFromMenuSplit();

    // If a previous display update is in progress, wait for it to complete.
    inkhud->awaitUpdate();

    // Notify all applets that we're shutting down
    for (Applet *ua : inkhud->userApplets) {
        ua->onDeactivate();
        ua->onShutdown();
    }
    for (SystemApplet *sa : inkhud->systemApplets) {
        // Note: no onDeactivate. System applets are always active.
        sa->onShutdown();
    }

    // User has successful executed a safe shutdown
    // We don't need to nag at boot anymore
    settings->tips.safeShutdownSeen = true;

    inkhud->persistence->saveSettings();
    inkhud->persistence->saveLatestMessage();

    // LogoApplet::onShutdown attempted to heal the display by drawing a "shutting down" screen twice,
    // then prepared a final powered-off screen for us, which shows device shortname.
    // We're updating to show that one now.

    inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FULL, true, false);
    delay(1000); // Cooldown, before potentially yanking display power

    // InkHUD shutdown complete
    // Firmware shutdown continues for several seconds more; flash write still pending
    playShutdownMelody();

    return 0; // We agree: deep sleep now
}

// Display an intermediate screen while configuration changes are applied
void InkHUD::Events::applyingChanges()
{
    // Bring the logo applet forward with a temporary message
    for (SystemApplet *sa : inkhud->systemApplets) {
        sa->onApplyingChanges();
    }
}

// Callback for rebootObserver
// Same as shutdown, without drawing the logoApplet
// Makes sure we don't lose message history / InkHUD config
int InkHUD::Events::beforeReboot(void *unused)
{
    // Undo any transient InputMenu tile-split before persisting settings (see beforeDeepSleep). Idempotent.
    inkhud->restoreFromMenuSplit();

    // Notify all applets that we're "shutting down"
    // They don't need to know that it's really a reboot
    for (Applet *a : inkhud->userApplets) {
        a->onDeactivate();
        a->onShutdown();
    }
    for (SystemApplet *sa : inkhud->systemApplets) {
        // Note: no onDeactivate. System applets are always active.
        sa->onReboot();
    }

    // Save settings to flash, or erase if factory reset in progress
    if (!eraseOnReboot) {
        inkhud->persistence->saveSettings();
        inkhud->persistence->saveLatestMessage();
    } else {
        NicheGraphics::clearFlashData();
        messageStore.clearAllMessages(); // also wipe the shared message store
    }

    // Note: no forceUpdate call here
    // We don't have any final screen to draw, although LogoApplet::onReboot did already display a "rebooting" screen

    return 0; // No special status to report. Ignored anyway by this Observable
}

// Callback when a new text message is received
// Caches the most recently received message, for use by applets
// Rx does not trigger a save to flash, however the data *will* be saved alongside other during shutdown, etc.
// Note: this is intentionally separate from device-state message fields.
int InkHUD::Events::onReceiveTextMessage(const meshtastic_MeshPacket *packet)
{
    // Short circuit: don't store outgoing messages
    if (getFrom(packet) == nodeDB->getNodeNum())
        return 0;

    if (!messageStore.shouldStorePacket(*packet))
        return 0;

    bool isBroadcastMsg = isBroadcast(packet->to);
    inkhud->persistence->latestMessage.wasBroadcast = isBroadcastMsg;

    if (!isBroadcastMsg) {
        // DMs never pass through ThreadedMessageApplet, so add them to the global store here
        // so they survive reboots. Derive the latestMessage cache entry from the stored result.
        const StoredMessage *stored = messageStore.tryAddFromPacket(*packet);
        if (!stored)
            return 0;
        inkhud->persistence->latestMessage.dm = *stored;
        // Dynamic per-peer chat windows: bind/claim a DMChat slot for this sender and bring it
        // on screen. Central routing here (not per-applet observers) so a NEW peer can claim a
        // slot even while that slot's applet is inactive. No-op when no slots are registered.
        DMChatApplet::onIncomingDM(stored->sender);
    } else {
        // Broadcasts are added to the global store by ThreadedMessageApplet::handleReceived().
        // Here we only update the latestMessage cache used by AllMessageApplet / NotificationApplet.
        StoredMessage &sm = inkhud->persistence->latestMessage.broadcast;
        sm.sender = packet->from;
        sm.timestamp = getValidTime(RTCQuality::RTCQualityDevice, true);
        sm.channelIndex = packet->channel;
        const char *payload = reinterpret_cast<const char *>(packet->decoded.payload.bytes);
        size_t storedLen = packet->decoded.payload.size;
        if (storedLen >= MAX_MESSAGE_SIZE)
            storedLen = MAX_MESSAGE_SIZE - 1;
        sm.textOffset = MessageStore::storeText(payload, storedLen);
        sm.textLength = static_cast<uint16_t>(storedLen);
    }

    return 0; // Tell caller to continue notifying other observers. (No reason to abort this event)
}

int InkHUD::Events::onAdminMessage(AdminModule_ObserverData *data)
{
    switch (data->request->which_payload_variant) {
    // Factory reset
    // Two possible messages. One preserves BLE bonds, other wipes. Both should clear InkHUD data.
    case meshtastic_AdminMessage_factory_reset_device_tag:
    case meshtastic_AdminMessage_factory_reset_config_tag:
        eraseOnReboot = true;
        *data->result = AdminMessageHandleResult::HANDLED;
        break;

    default:
        break;
    }

    return 0; // Tell caller to continue notifying other observers. (No reason to abort this event)
}

#ifdef ARCH_ESP32
// Callback for lightSleepObserver
// Make sure the display is not partway through an update when we begin light sleep
// This is because some displays require active input from us to terminate the update process, and protect the panel hardware
int InkHUD::Events::beforeLightSleep(void *unused)
{
    inkhud->awaitUpdate();
    return 0; // No special status to report. Ignored anyway by this Observable
}
#endif

#if defined(T_DECK_MAX)
// Callback for screenPowerObserver (fired from PowerFSM via notifyScreenPower).
// awake==true : PowerFSM entered stateON/statePOWER (the device woke). Mark awake and force a full
//               re-render so the "asleep" indicator is cleared.
// awake==false: the screen_on_secs timer expired (ON/POWER -> DARK) and light sleep is imminent.
//               Mark asleep and SYNCHRONOUSLY (async=false) refresh so the "asleep" indicator is
//               stamped on the panel before we sleep.
int InkHUD::Events::onScreenPower(bool awake)
{
    // Edge-triggered only. stateON's onEnter notifies on every re-entry (each keystroke/bezel
    // press is an EVENT_INPUT ON->ON self-transition), and the wake render below is a forced
    // full-buffer FAST refresh - without this guard every key press would flash the panel and
    // burn FAST-refresh debt toward an unwanted mid-typing FULL flash.
    if (awake == inkhudScreenAwake)
        return 0;

    inkhudScreenAwake = awake;

    if (awake) {
        // Force a full re-render (all=true) so the persistent framebuffer is cleared and the current
        // applet is fully redrawn - this erases the "asleep" moon glyph that BatteryIconApplet
        // stamped into its top-right corner tile before sleep, and repaints the battery there
        // (if enabled). Async is fine here: waking is not time-critical.
        inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FAST, true, true);
    } else {
        // Settling to sleep: stamp the "asleep" indicator SYNCHRONOUSLY (async=false) so it lands on
        // the panel before the imminent light sleep. In silent mode this is the LAST render until
        // the next wake (the InkHUD facade drops all asleep-time updates), so it must bypass the
        // gate - it is the render that puts the silent-moon + lock on the panel.
        inkhud->silentStampBypass = true;
        inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FAST, false, false);
        inkhud->silentStampBypass = false;
    }

    return 0; // Tell caller to continue notifying other observers
}
#endif

// Silence all ongoing beeping, blinking, buzzing, coming from the external notification module
// Returns true if an external notification was active, and we dismissed it
// Button handling changes depending on our result
bool InkHUD::Events::dismissExternalNotification()
{
    // Abort if not using external notifications
    if (!moduleConfig.external_notification.enabled)
        return false;

    // Abort if nothing to dismiss
    if (!externalNotificationModule->nagging())
        return false;

    // Stop the beep buzz blink
    externalNotificationModule->stopNow();

    // Inform that we did indeed dismiss an external notification
    return true;
}

#endif
